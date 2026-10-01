#include "src/platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>

/* --- Signal-Safe Interruption & Resize Flags --- */

static volatile sig_atomic_t g_interrupted = 0;
static volatile sig_atomic_t g_term_resized = 0;

static void posix_sig_handler(int sig) {
  if (sig == SIGWINCH) {
    g_term_resized = 1;
  } else if (sig == SIGINT || sig == SIGTERM) {
    g_interrupted = 1;
  }
}

/* --- Terminal State Encapsulation --- */

static struct termios orig_termios;
static int termios_saved = 0;
static int g_initialized = 0;
static int g_cleaned_up = 0;

int platform_terminal_init(platform_term_caps_t *caps) {
  g_cleaned_up = 0;
  g_interrupted = 0;
  g_term_resized = 0;

  int stdout_tty = isatty(STDOUT_FILENO);
  int stdin_tty = isatty(STDIN_FILENO);
  int is_tty = stdout_tty && stdin_tty;
  int supports_vt = 0;
  int supports_mouse = 0;

  if (is_tty) {
    const char *term = getenv("TERM");
    if (term && term[0] && strcmp(term, "dumb") != 0) {
      supports_vt = 1;
      /* Mouse tracking (SGR 1006) requires modern/xterm terminal capabilities */
      if (strcmp(term, "vt100") != 0 &&
          strcmp(term, "vt52") != 0 &&
          strcmp(term, "vt220") != 0 &&
          strncmp(term, "vanilla", 7) != 0) {
        supports_mouse = 1;
      }
    }
  }

  if (caps) {
    caps->is_tty = is_tty;
    caps->supports_vt = supports_vt;
    caps->supports_mouse = supports_mouse;
  }

  /* Register signal handlers for interrupt and resize (flags only) */
  signal(SIGINT, posix_sig_handler);
  signal(SIGTERM, posix_sig_handler);
#ifdef SIGWINCH
  signal(SIGWINCH, posix_sig_handler);
#endif

  /* Register atexit safeguard */
  if (!g_initialized) {
    atexit(platform_terminal_cleanup);
    g_initialized = 1;
  }

  /* Enter raw/non-canonical mode on stdin if interactive */
  if (stdin_tty && tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
    termios_saved = 1;
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  }

  /* Hide cursor (?25l), enable mouse drag tracking (?1002h, ?1006h), clear screen */
  if (supports_vt) {
    if (supports_mouse) {
      const char init_seq[] = "\033[?25l\033[?1002h\033[?1006h\033[2J";
      ssize_t ret = write(STDOUT_FILENO, init_seq, sizeof(init_seq) - 1);
      (void)ret;
    } else {
      const char init_seq[] = "\033[?25l\033[2J";
      ssize_t ret = write(STDOUT_FILENO, init_seq, sizeof(init_seq) - 1);
      (void)ret;
    }
  }

  return 0;
}

void platform_terminal_cleanup(void) {
  if (!g_initialized || g_cleaned_up)
    return;
  g_cleaned_up = 1;

  if (termios_saved) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    termios_saved = 0;
  }

  /* Disable mouse tracking (?1002l, ?1006l) and restore cursor (?25h) */
  const char cleanup_seq[] = "\033[?1002l\033[?1006l\033[?25h";
  ssize_t ret = write(STDOUT_FILENO, cleanup_seq, sizeof(cleanup_seq) - 1);
  (void)ret;
}

void platform_get_term_size(int *rows, int *cols) {
  if (rows) *rows = 0;
  if (cols) *cols = 0;
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    if (rows && ws.ws_row > 0)
      *rows = ws.ws_row;
    if (cols && ws.ws_col > 0)
      *cols = ws.ws_col;
  }
}

int platform_check_resize(void) {
  if (g_term_resized) {
    g_term_resized = 0;
    return 1;
  }
  return 0;
}

void platform_sleep_frame(unsigned int usec) {
  usleep(usec);
}

int platform_write_output(const char *buf, size_t len) {
  if (!buf || len == 0)
    return 0;
  return (int)write(STDOUT_FILENO, buf, len);
}

int platform_is_interrupted(void) {
  return g_interrupted != 0;
}

/* --- Input Parsing State Machine --- */

static char s_ibuf[128];
static int s_ibuf_len = 0;
static int s_mouse_dragging = 0;
static int s_mouse_last_x = 0;
static int s_mouse_last_y = 0;

platform_input_event_t platform_parse_input_chunk(const char *buf, size_t len,
                                                  platform_mouse_event_t *mouse_event,
                                                  size_t *consumed) {
  if (consumed) *consumed = 0;
  if (!buf || len == 0) return INPUT_NONE;

  /* Any non-escape character is treated as an exit keypress */
  if (buf[0] != '\033') {
    if (consumed) *consumed = 1;
    return INPUT_EXIT_KEY;
  }

  /* Must have at least "\033[<" (3 bytes) to be an SGR mouse sequence */
  if (len < 3) {
    return INPUT_NONE; /* Incomplete sequence */
  }

  if (buf[1] == '[' && buf[2] == '<') {
    size_t j = 3;
    int btn = 0, mx = 0, my = 0;

    while (j < len && buf[j] >= '0' && buf[j] <= '9') {
      btn = btn * 10 + (buf[j++] - '0');
    }
    if (j >= len) return INPUT_NONE;
    if (buf[j] == ';') j++;

    while (j < len && buf[j] >= '0' && buf[j] <= '9') {
      mx = mx * 10 + (buf[j++] - '0');
    }
    if (j >= len) return INPUT_NONE;
    if (buf[j] == ';') j++;

    while (j < len && buf[j] >= '0' && buf[j] <= '9') {
      my = my * 10 + (buf[j++] - '0');
    }
    if (j >= len) return INPUT_NONE;

    char trail = buf[j++];
    if (trail != 'M' && trail != 'm') {
      if (consumed) *consumed = j;
      return INPUT_NONE;
    }

    if (consumed) *consumed = j;

    if (btn == 0 && trail == 'M') {
      s_mouse_dragging = 1;
      s_mouse_last_x = mx;
      s_mouse_last_y = my;
      if (mouse_event) {
        mouse_event->btn = 0;
        mouse_event->x = mx;
        mouse_event->y = my;
        mouse_event->dx = 0;
        mouse_event->dy = 0;
      }
      return INPUT_NONE;
    } else if (btn == 32 && trail == 'M' && s_mouse_dragging) {
      int dx = mx - s_mouse_last_x;
      int dy = my - s_mouse_last_y;
      s_mouse_last_x = mx;
      s_mouse_last_y = my;
      if (mouse_event) {
        mouse_event->btn = 32;
        mouse_event->x = mx;
        mouse_event->y = my;
        mouse_event->dx = dx;
        mouse_event->dy = dy;
      }
      return INPUT_MOUSE_DRAG;
    } else if (btn == 0 && trail == 'm') {
      s_mouse_dragging = 0;
      if (mouse_event) {
        mouse_event->btn = 0;
        mouse_event->x = mx;
        mouse_event->y = my;
        mouse_event->dx = 0;
        mouse_event->dy = 0;
      }
      return INPUT_MOUSE_UP;
    }
    return INPUT_NONE;
  }

  /* Unrecognized escape sequence - skip past terminal letter */
  size_t i = 1;
  if (len > 1 && buf[1] == '[') {
    i = 2;
    while (i < len && (buf[i] < 0x40 || buf[i] > 0x7E)) i++;
    if (i < len) i++;
  } else {
    while (i < len && buf[i] < 0x40) i++;
    if (i < len) i++;
  }
  if (consumed) *consumed = i;
  return INPUT_NONE;
}

#ifdef FETCH_TESTING
static int s_test_pending_avail = -1;
#endif

platform_input_event_t platform_poll_input(platform_mouse_event_t *mouse_event) {
  struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};

#ifdef FETCH_TESTING
  int has_test_pending = (s_test_pending_avail >= 0);
  int test_avail = s_test_pending_avail;
#else
  int has_test_pending = 0;
  int test_avail = -1;
#endif

  while (s_ibuf_len > 0 || has_test_pending || poll(&pfd, 1, 0) > 0) {
    if (s_ibuf_len == 0) {
      int avail = 0;
      if (has_test_pending) {
        avail = test_avail;
      } else if (ioctl(STDIN_FILENO, FIONREAD, &avail) < 0 || avail <= 0) {
        break;
      }
      if (avail <= 0) break;
      if (avail == 1) {
        /*
         * Hard requirement: input passthrough.
         * A single pending byte is a regular keypress.
         * Exit immediately without reading/consuming the byte,
         * leaving it in the kernel tty buffer for the calling shell.
         */
        return INPUT_EXIT_KEY;
      }

      int to_read = avail < (int)sizeof(s_ibuf) ? avail : (int)sizeof(s_ibuf);
      int n = read(STDIN_FILENO, s_ibuf, to_read);
      if (n <= 0) {
        return INPUT_EXIT_KEY;
      }
      s_ibuf_len = n;

      if (s_ibuf[0] != '\033') {
        s_ibuf_len = 0;
        return INPUT_EXIT_KEY;
      }
    } else {
      if (poll(&pfd, 1, 0) > 0 && s_ibuf_len < (int)sizeof(s_ibuf)) {
        int n = read(STDIN_FILENO, s_ibuf + s_ibuf_len, sizeof(s_ibuf) - s_ibuf_len);
        if (n > 0) s_ibuf_len += n;
      }
    }

    size_t consumed = 0;
    platform_input_event_t ev = platform_parse_input_chunk(s_ibuf, (size_t)s_ibuf_len, mouse_event, &consumed);

    if (consumed > 0) {
      if (consumed < (size_t)s_ibuf_len) {
        memmove(s_ibuf, s_ibuf + consumed, s_ibuf_len - consumed);
        s_ibuf_len -= (int)consumed;
      } else {
        s_ibuf_len = 0;
      }
    }

    if (ev != INPUT_NONE) {
      return ev;
    }
  }

  return INPUT_NONE;
}

/* --- Platform Paths & OS Detection --- */

void platform_get_config_path(char *out, size_t outsz) {
  if (!out || outsz == 0) return;
  const char *xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0]) {
    snprintf(out, outsz, "%s/fetch/config", xdg);
    return;
  }
  const char *home = getenv("HOME");
  if (home && home[0]) {
    snprintf(out, outsz, "%s/.config/fetch/config", home);
    return;
  }
#ifdef _WIN32
  const char *userprofile = getenv("USERPROFILE");
  if (userprofile && userprofile[0]) {
    snprintf(out, outsz, "%s/.config/fetch/config", userprofile);
    return;
  }
#endif
  snprintf(out, outsz, ".config/fetch/config");
}

void platform_get_logo_path(char *out, size_t outsz) {
  if (!out || outsz == 0) return;
  const char *xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0]) {
    snprintf(out, outsz, "%s/fetch/logo.txt", xdg);
    return;
  }
  const char *home = getenv("HOME");
  if (home && home[0]) {
    snprintf(out, outsz, "%s/.config/fetch/logo.txt", home);
    return;
  }
#ifdef _WIN32
  const char *userprofile = getenv("USERPROFILE");
  if (userprofile && userprofile[0]) {
    snprintf(out, outsz, "%s/.config/fetch/logo.txt", userprofile);
    return;
  }
#endif
  snprintf(out, outsz, ".config/fetch/logo.txt");
}

int platform_detect_os_id(char *out, size_t outsz) {
  if (!out || outsz == 0) return 0;
  out[0] = '\0';
  FILE *fp = fopen("/etc/os-release", "r");
  if (fp) {
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
      if (strncmp(buf, "ID=", 3) == 0) {
        char *val = buf + 3;
        int len = (int)strlen(val);
        while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r'))
          val[--len] = '\0';
        if (*val == '\'' || *val == '"') val++;
        len = (int)strlen(val);
        if (len > 0 && (val[len - 1] == '\'' || val[len - 1] == '"'))
          val[--len] = '\0';
        strncpy(out, val, outsz - 1);
        out[outsz - 1] = '\0';
        fclose(fp);
        return 1;
      }
    }
    fclose(fp);
  }
  return 0;
}

/* --- System Information Stubs (Full POSIX gather extraction is Phase 6+) --- */

void platform_gather_title(char *out_user, size_t usersz, char *out_host, size_t hostsz) {
  if (out_user && usersz > 0) out_user[0] = '\0';
  if (out_host && hostsz > 0) out_host[0] = '\0';
}
void platform_gather_os(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_host(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_kernel(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_uptime(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_packages(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_shell(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_display(platform_emit_info_cb emit_cb) { (void)emit_cb; }
void platform_gather_wm(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_displaymanager(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_theme(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_icons(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_font(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_cursor(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_terminal(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_cpu(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_gpu(platform_emit_info_cb emit_cb) { (void)emit_cb; }
void platform_gather_memory(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_swap(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_disk(const char *path, char *out, size_t outsz) { (void)path; if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_ip(platform_emit_info_cb emit_cb) { (void)emit_cb; }
void platform_gather_battery(char *out_label, size_t labelsz, char *out_val, size_t valsz) {
  if (out_label && labelsz > 0) out_label[0] = '\0';
  if (out_val && valsz > 0) out_val[0] = '\0';
}
void platform_gather_locale(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_gather_powerprofile(char *out, size_t outsz) { if (out && outsz > 0) out[0] = '\0'; }
void platform_invalidate_info_cache(void) {}

#ifdef FETCH_TESTING
/* --- Test Injections --- */

void platform_reset_input_state_for_test(void) {
  s_ibuf_len = 0;
  s_mouse_dragging = 0;
  s_mouse_last_x = 0;
  s_mouse_last_y = 0;
}

void platform_set_interrupted_for_test(int val) {
  g_interrupted = val;
}

void platform_set_resized_for_test(int val) {
  g_term_resized = val;
}

void platform_set_pending_bytes_for_test(int count) {
  s_test_pending_avail = count;
}

int platform_get_pending_bytes_for_test(void) {
  return s_test_pending_avail;
}

int platform_is_ctrl_handler_registered_for_test(void) {
  return 0;
}

int platform_get_query_count_for_test(const char *name) {
  (void)name;
  return 0;
}

int platform_is_field_cached_for_test(const char *name) {
  (void)name;
  return 0;
}

void platform_reset_query_counts_for_test(void) {}
#endif
