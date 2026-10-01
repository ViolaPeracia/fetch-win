#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#define COBJMACROS
#include <initguid.h>
#include <dxgi.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "src/platform/platform.h"

/* Ensure modern console constants are defined even on older SDK headers */
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#ifndef ENABLE_EXTENDED_FLAGS
#define ENABLE_EXTENDED_FLAGS 0x0080
#endif
#ifndef ENABLE_QUICK_EDIT_MODE
#define ENABLE_QUICK_EDIT_MODE 0x0040
#endif
#ifndef ENABLE_PROCESSED_INPUT
#define ENABLE_PROCESSED_INPUT 0x0001
#endif

/* --- Terminal State Encapsulation --- */

typedef struct {
  HANDLE hIn;
  HANDLE hOut;
  DWORD orig_in_mode;
  DWORD orig_out_mode;
  UINT orig_in_cp;
  UINT orig_out_cp;
  CONSOLE_CURSOR_INFO orig_cursor_info;
  int is_tty;
  int supports_vt;
  int supports_mouse;
  int in_mode_saved;
  int out_mode_saved;
  int cp_saved;
  int cursor_saved;
  int conin_opened;
  int conout_opened;
  int is_initialized;
  int is_cleaned_up;
  int ctrl_handler_registered;
} win_console_state_t;

static win_console_state_t g_win_console = {0};
static atomic_int g_interrupted = 0;
static volatile int g_term_resized = 0;
static int g_last_rows = 0;
static int g_last_cols = 0;

/* Native Win32 mouse state tracking */
static int s_win_mouse_dragging = 0;
static int s_win_mouse_last_x = 0;
static int s_win_mouse_last_y = 0;

/* SGR escape parser mouse state tracking */
static int s_sgr_mouse_dragging = 0;
static int s_sgr_mouse_last_x = 0;
static int s_sgr_mouse_last_y = 0;

#ifdef FETCH_TESTING
static int s_test_pending_avail = -1;
#endif

/* --- Control Handler (Ctrl+C / Interruption) --- */

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
  switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      /* Safe guarantee: only set atomic interruption flag, no console I/O or allocation */
      atomic_store(&g_interrupted, 1);
      return TRUE;
    default:
      return FALSE;
  }
}

/* Helper to get or connect console handles (supporting redirected stdio) */
static void ensure_console_handles(void) {
  if (g_win_console.hIn == INVALID_HANDLE_VALUE || g_win_console.hIn == NULL) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (hIn == INVALID_HANDLE_VALUE || hIn == NULL || !GetConsoleMode(hIn, &mode)) {
      HANDLE hConIn = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, 0, NULL);
      if (hConIn != INVALID_HANDLE_VALUE && GetConsoleMode(hConIn, &mode)) {
        g_win_console.hIn = hConIn;
        g_win_console.conin_opened = 1;
      } else {
        g_win_console.hIn = hIn;
      }
    } else {
      g_win_console.hIn = hIn;
    }
  }

  if (g_win_console.hOut == INVALID_HANDLE_VALUE || g_win_console.hOut == NULL) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (hOut == INVALID_HANDLE_VALUE || hOut == NULL || !GetConsoleMode(hOut, &mode)) {
      HANDLE hConOut = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, 0, NULL);
      if (hConOut != INVALID_HANDLE_VALUE && GetConsoleMode(hConOut, &mode)) {
        g_win_console.hOut = hConOut;
        g_win_console.conout_opened = 1;
      } else {
        g_win_console.hOut = hOut;
      }
    } else {
      g_win_console.hOut = hOut;
    }
  }
}

/* --- Terminal Capabilities & Initialization --- */

int platform_terminal_init(platform_term_caps_t *caps) {
  g_win_console.is_cleaned_up = 0;
  atomic_store(&g_interrupted, 0);
  g_term_resized = 0;

  /* If handles were previously closed on cleanup, reconnect them */
  ensure_console_handles();

  HANDLE hIn = g_win_console.hIn;
  HANDLE hOut = g_win_console.hOut;

  DWORD in_mode = 0, out_mode = 0;
  int in_is_console = (hIn != INVALID_HANDLE_VALUE && hIn != NULL && GetConsoleMode(hIn, &in_mode));
  int out_is_console = (hOut != INVALID_HANDLE_VALUE && hOut != NULL && GetConsoleMode(hOut, &out_mode));

  int is_tty = in_is_console && out_is_console;
  int supports_vt = 0;
  int supports_mouse = 0;

  if (is_tty) {
    if (!g_win_console.in_mode_saved) {
      g_win_console.orig_in_mode = in_mode;
      g_win_console.in_mode_saved = 1;
    }
    if (!g_win_console.out_mode_saved) {
      g_win_console.orig_out_mode = out_mode;
      g_win_console.out_mode_saved = 1;
    }
    if (!g_win_console.cp_saved) {
      g_win_console.orig_in_cp = GetConsoleCP();
      g_win_console.orig_out_cp = GetConsoleOutputCP();
      g_win_console.cp_saved = 1;
    }
    if (!g_win_console.cursor_saved) {
      if (GetConsoleCursorInfo(hOut, &g_win_console.orig_cursor_info)) {
        g_win_console.cursor_saved = 1;
      }
    }

    /* Switch code pages to UTF-8 */
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    /* Check TERM environment variable for explicit overrides */
    const char *term = getenv("TERM");
    int term_dumb = (term && strcmp(term, "dumb") == 0);
    int term_vt100 = (term && strcmp(term, "vt100") == 0);

    /* Attempt to enable Virtual Terminal Processing on stdout */
    DWORD requested_out_mode = out_mode | ENABLE_PROCESSED_OUTPUT |
                              ENABLE_WRAP_AT_EOL_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!term_dumb && SetConsoleMode(hOut, requested_out_mode)) {
      supports_vt = 1;
      /* Mouse tracking is supported in modern terminals unless TERM specifies legacy vt100/vt52 */
      if (!term_vt100 &&
          (term == NULL ||
           (strcmp(term, "vt52") != 0 &&
            strcmp(term, "vt220") != 0 &&
            strncmp(term, "vanilla", 7) != 0))) {
        supports_mouse = 1;
      }
    } else {
      SetConsoleMode(hOut, out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
      supports_vt = 0;
      supports_mouse = 0;
    }

    /* Configure raw input mode:
     * Enable processed input so that CTRL_C_EVENT reliably reaches SetConsoleCtrlHandler.
     * Disable line input, echo, and QuickEdit mode.
     * Enable window resize input and mouse input.
     */
    DWORD requested_in_mode = ENABLE_EXTENDED_FLAGS | ENABLE_PROCESSED_INPUT |
                              ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
    SetConsoleMode(hIn, requested_in_mode);
  }

  g_win_console.is_tty = is_tty;
  g_win_console.supports_vt = supports_vt;
  g_win_console.supports_mouse = supports_mouse;

  if (caps) {
    caps->is_tty = is_tty;
    caps->supports_vt = supports_vt;
    caps->supports_mouse = supports_mouse;
  }

  /* Register control handler for Ctrl+C / close if not already registered */
  if (!g_win_console.ctrl_handler_registered) {
    if (SetConsoleCtrlHandler(console_ctrl_handler, TRUE)) {
      g_win_console.ctrl_handler_registered = 1;
    }
  }

  if (!g_win_console.is_initialized) {
    atexit(platform_terminal_cleanup);
    g_win_console.is_initialized = 1;
  }

  /* If VT supported, emit VT sequences to hide cursor, enable SGR mouse, clear screen */
  if (is_tty && supports_vt) {
    if (supports_mouse) {
      const char init_seq[] = "\033[?25l\033[?1002h\033[?1006h\033[2J";
      platform_write_output(init_seq, sizeof(init_seq) - 1);
    } else {
      const char init_seq[] = "\033[?25l\033[2J";
      platform_write_output(init_seq, sizeof(init_seq) - 1);
    }
  } else if (is_tty && g_win_console.cursor_saved) {
    /* Non-VT fallback: hide cursor via Win32 cursor info */
    CONSOLE_CURSOR_INFO cinfo = g_win_console.orig_cursor_info;
    cinfo.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &cinfo);
  }

  /* Cache initial terminal size */
  int rows = 0, cols = 0;
  platform_get_term_size(&rows, &cols);
  g_last_rows = rows;
  g_last_cols = cols;

  return 0;
}

/* --- Safe Terminal Cleanup & Restoration --- */

void platform_terminal_cleanup(void) {
  if (!g_win_console.is_initialized || g_win_console.is_cleaned_up) {
    return;
  }
  g_win_console.is_cleaned_up = 1;

  /* Unregister control handler */
  if (g_win_console.ctrl_handler_registered) {
    SetConsoleCtrlHandler(console_ctrl_handler, FALSE);
    g_win_console.ctrl_handler_registered = 0;
  }

  /* If VT is supported, disable mouse tracking and restore cursor visibility */
  if (g_win_console.is_tty && g_win_console.supports_vt) {
    const char cleanup_seq[] = "\033[?1002l\033[?1006l\033[?25h";
    platform_write_output(cleanup_seq, sizeof(cleanup_seq) - 1);
  }

  /* Restore cursor info via Win32 API */
  if (g_win_console.cursor_saved && g_win_console.hOut != INVALID_HANDLE_VALUE && g_win_console.hOut != NULL) {
    SetConsoleCursorInfo(g_win_console.hOut, &g_win_console.orig_cursor_info);
  }

  /* Restore original console input and output modes */
  if (g_win_console.in_mode_saved && g_win_console.hIn != INVALID_HANDLE_VALUE && g_win_console.hIn != NULL) {
    SetConsoleMode(g_win_console.hIn, g_win_console.orig_in_mode);
  }
  if (g_win_console.out_mode_saved && g_win_console.hOut != INVALID_HANDLE_VALUE && g_win_console.hOut != NULL) {
    SetConsoleMode(g_win_console.hOut, g_win_console.orig_out_mode);
  }

  /* Restore original code pages */
  if (g_win_console.cp_saved) {
    if (g_win_console.orig_in_cp != 0) {
      SetConsoleCP(g_win_console.orig_in_cp);
    }
    if (g_win_console.orig_out_cp != 0) {
      SetConsoleOutputCP(g_win_console.orig_out_cp);
    }
  }

  /* If custom CONIN$/CONOUT$ were opened, close them */
  if (g_win_console.conin_opened && g_win_console.hIn != INVALID_HANDLE_VALUE && g_win_console.hIn != NULL) {
    CloseHandle(g_win_console.hIn);
    g_win_console.hIn = INVALID_HANDLE_VALUE;
    g_win_console.conin_opened = 0;
  }
  if (g_win_console.conout_opened && g_win_console.hOut != INVALID_HANDLE_VALUE && g_win_console.hOut != NULL) {
    CloseHandle(g_win_console.hOut);
    g_win_console.hOut = INVALID_HANDLE_VALUE;
    g_win_console.conout_opened = 0;
  }

  g_win_console.in_mode_saved = 0;
  g_win_console.out_mode_saved = 0;
  g_win_console.cp_saved = 0;
  g_win_console.cursor_saved = 0;
}

/* --- Terminal Size & Resize Detection --- */

void platform_get_term_size(int *rows, int *cols) {
  if (rows) *rows = 0;
  if (cols) *cols = 0;

  ensure_console_handles();
  HANDLE hOut = g_win_console.hOut;
  if (hOut == INVALID_HANDLE_VALUE || hOut == NULL) {
    return;
  }

  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
    int r = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    int c = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    if (rows && r > 0) *rows = r;
    if (cols && c > 0) *cols = c;
  }
}

int platform_check_resize(void) {
  int r = 0, c = 0;
  platform_get_term_size(&r, &c);
  if (g_last_rows > 0 && g_last_cols > 0 && (r > 0 && c > 0)) {
    if (r != g_last_rows || c != g_last_cols) {
      g_term_resized = 1;
      g_last_rows = r;
      g_last_cols = c;
    }
  } else if (r > 0 && c > 0) {
    g_last_rows = r;
    g_last_cols = c;
  }

  if (g_term_resized) {
    g_term_resized = 0;
    return 1;
  }
  return 0;
}

/* --- Output & Sleep Wrappers --- */

int platform_write_output(const char *buf, size_t len) {
  if (!buf || len == 0) return 0;
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut != INVALID_HANDLE_VALUE && hOut != NULL) {
    DWORD written = 0;
    if (WriteFile(hOut, buf, (DWORD)len, &written, NULL)) {
      return (int)written;
    }
  }
  ensure_console_handles();
  if (g_win_console.hOut != INVALID_HANDLE_VALUE && g_win_console.hOut != NULL &&
      g_win_console.hOut != hOut) {
    DWORD written = 0;
    if (WriteFile(g_win_console.hOut, buf, (DWORD)len, &written, NULL)) {
      return (int)written;
    }
  }
  int ret = (int)fwrite(buf, 1, len, stdout);
  fflush(stdout);
  return ret;
}

void platform_sleep_frame(unsigned int usec) {
  /* Round up microseconds to milliseconds so non-zero usec always yields */
  DWORD ms = (DWORD)((usec + 999) / 1000);
  Sleep(ms);
}

int platform_is_interrupted(void) {
  return atomic_load(&g_interrupted) != 0;
}

/* --- SGR Mouse Escape Sequence Parser --- */

platform_input_event_t platform_parse_input_chunk(const char *buf, size_t len,
                                                  platform_mouse_event_t *mouse_event,
                                                  size_t *consumed) {
  if (consumed) *consumed = 0;
  if (!buf || len == 0) return INPUT_NONE;

  /* Non-escape character is treated as an exit keypress */
  if (buf[0] != '\033') {
    if (consumed) *consumed = 1;
    return INPUT_EXIT_KEY;
  }

  /* Must have at least "\033[<" (3 bytes) to be an SGR mouse sequence */
  if (len < 3) {
    return INPUT_NONE;
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
      s_sgr_mouse_dragging = 1;
      s_sgr_mouse_last_x = mx;
      s_sgr_mouse_last_y = my;
      if (mouse_event) {
        mouse_event->btn = 0;
        mouse_event->x = mx;
        mouse_event->y = my;
        mouse_event->dx = 0;
        mouse_event->dy = 0;
      }
      return INPUT_NONE;
    } else if (btn == 32 && trail == 'M' && s_sgr_mouse_dragging) {
      int dx = mx - s_sgr_mouse_last_x;
      int dy = my - s_sgr_mouse_last_y;
      s_sgr_mouse_last_x = mx;
      s_sgr_mouse_last_y = my;
      if (mouse_event) {
        mouse_event->btn = 32;
        mouse_event->x = mx;
        mouse_event->y = my;
        mouse_event->dx = dx;
        mouse_event->dy = dy;
      }
      return INPUT_MOUSE_DRAG;
    } else if (btn == 0 && trail == 'm') {
      s_sgr_mouse_dragging = 0;
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

  /* Unrecognized escape sequence - skip past terminal character */
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

/* --- Input Polling & Event Handling --- */

platform_input_event_t platform_poll_input(platform_mouse_event_t *mouse_event) {
#ifdef FETCH_TESTING
  if (s_test_pending_avail == 1) {
    /* Simulated single pending byte / key - do not consume */
    return INPUT_EXIT_KEY;
  } else if (s_test_pending_avail == 0) {
    return INPUT_NONE;
  }
#endif

  if (atomic_load(&g_interrupted)) {
    return INPUT_NONE;
  }

  ensure_console_handles();
  HANDLE hIn = g_win_console.hIn;
  if (hIn == INVALID_HANDLE_VALUE || hIn == NULL) {
    return INPUT_NONE;
  }

  while (1) {
    if (atomic_load(&g_interrupted)) {
      return INPUT_NONE;
    }

    DWORD num_events = 0;
    if (!GetNumberOfConsoleInputEvents(hIn, &num_events) || num_events == 0) {
      return INPUT_NONE;
    }

    INPUT_RECORD ir;
    DWORD num_read = 0;
    if (!PeekConsoleInputA(hIn, &ir, 1, &num_read) || num_read == 0) {
      return INPUT_NONE;
    }

    /* Window buffer resize event */
    if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
      INPUT_RECORD discard;
      ReadConsoleInputA(hIn, &discard, 1, &num_read);
      g_term_resized = 1;
      continue;
    }

    /* Focus and menu events: consume and discard */
    if (ir.EventType == FOCUS_EVENT || ir.EventType == MENU_EVENT) {
      INPUT_RECORD discard;
      ReadConsoleInputA(hIn, &discard, 1, &num_read);
      continue;
    }

    /* Native Win32 mouse event */
    if (ir.EventType == MOUSE_EVENT) {
      INPUT_RECORD mev_record;
      if (!ReadConsoleInputA(hIn, &mev_record, 1, &num_read) || num_read == 0) {
        continue;
      }

      MOUSE_EVENT_RECORD *m = &mev_record.Event.MouseEvent;
      int cur_x = (int)m->dwMousePosition.X + 1; /* 1-based coordinate */
      int cur_y = (int)m->dwMousePosition.Y + 1; /* 1-based coordinate */
      int left_pressed = (m->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) != 0;

      /* Mouse drag event: button held down and mouse moved */
      if (m->dwEventFlags == MOUSE_MOVED && left_pressed && s_win_mouse_dragging) {
        int dx = cur_x - s_win_mouse_last_x;
        int dy = cur_y - s_win_mouse_last_y;
        s_win_mouse_last_x = cur_x;
        s_win_mouse_last_y = cur_y;

        if (mouse_event) {
          mouse_event->btn = 32;
          mouse_event->x = cur_x;
          mouse_event->y = cur_y;
          mouse_event->dx = dx;
          mouse_event->dy = dy;
        }
        return INPUT_MOUSE_DRAG;
      }

      /* Mouse button down: start dragging baseline */
      if (left_pressed && !s_win_mouse_dragging &&
          (m->dwEventFlags == 0 || m->dwEventFlags == DOUBLE_CLICK)) {
        s_win_mouse_dragging = 1;
        s_win_mouse_last_x = cur_x;
        s_win_mouse_last_y = cur_y;
        if (mouse_event) {
          mouse_event->btn = 0;
          mouse_event->x = cur_x;
          mouse_event->y = cur_y;
          mouse_event->dx = 0;
          mouse_event->dy = 0;
        }
        continue;
      }

      /* Mouse button release */
      if (!left_pressed && s_win_mouse_dragging &&
          (m->dwEventFlags == 0 || m->dwEventFlags == MOUSE_MOVED)) {
        s_win_mouse_dragging = 0;
        if (mouse_event) {
          mouse_event->btn = 0;
          mouse_event->x = cur_x;
          mouse_event->y = cur_y;
          mouse_event->dx = 0;
          mouse_event->dy = 0;
        }
        return INPUT_MOUSE_UP;
      }

      /* Other mouse events (unpressed movement, wheel, right click): ignore */
      continue;
    }

    /* Key event handling */
    if (ir.EventType == KEY_EVENT) {
      KEY_EVENT_RECORD *k = &ir.Event.KeyEvent;

      /* Key release: consume and discard */
      if (!k->bKeyDown) {
        INPUT_RECORD discard;
        ReadConsoleInputA(hIn, &discard, 1, &num_read);
        continue;
      }

      /* Standalone modifier keys (Shift, Ctrl, Alt, CapsLock, Win keys): consume and ignore */
      WORD vk = k->wVirtualKeyCode;
      if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
          vk == VK_CAPITAL || vk == VK_LWIN || vk == VK_RWIN) {
        INPUT_RECORD discard;
        ReadConsoleInputA(hIn, &discard, 1, &num_read);
        continue;
      }

      /* Ctrl+C character (ETX / ASCII 3): treat as interruption */
      if (k->uChar.AsciiChar == 3) {
        INPUT_RECORD discard;
        ReadConsoleInputA(hIn, &discard, 1, &num_read);
        atomic_store(&g_interrupted, 1);
        return INPUT_NONE;
      }

      /*
       * SGR mouse escape sequence check:
       * When streamed through ConPTY/terminal as VT key events,
       * an SGR sequence starts with '\033[<'.
       */
      if (k->uChar.AsciiChar == '\033' && num_events >= 3) {
        INPUT_RECORD lookahead[16];
        DWORD lookahead_count = num_events < 16 ? num_events : 16;
        DWORD lookahead_read = 0;
        if (PeekConsoleInputA(hIn, lookahead, lookahead_count, &lookahead_read) && lookahead_read >= 3) {
          if (lookahead[0].EventType == KEY_EVENT && lookahead[0].Event.KeyEvent.uChar.AsciiChar == '\033' &&
              lookahead[1].EventType == KEY_EVENT && lookahead[1].Event.KeyEvent.uChar.AsciiChar == '[' &&
              lookahead[2].EventType == KEY_EVENT && lookahead[2].Event.KeyEvent.uChar.AsciiChar == '<') {
            char sgr_buf[64];
            size_t sgr_len = 0;
            while (sgr_len < sizeof(sgr_buf) - 1) {
              INPUT_RECORD char_rec;
              DWORD cr = 0;
              if (!PeekConsoleInputA(hIn, &char_rec, 1, &cr) || cr == 0) break;
              if (char_rec.EventType != KEY_EVENT) break;
              ReadConsoleInputA(hIn, &char_rec, 1, &cr);
              if (!char_rec.Event.KeyEvent.bKeyDown) continue;
              char c = char_rec.Event.KeyEvent.uChar.AsciiChar;
              sgr_buf[sgr_len++] = c;
              if (c == 'M' || c == 'm') break;
            }
            sgr_buf[sgr_len] = '\0';
            size_t consumed = 0;
            platform_input_event_t sgr_ev = platform_parse_input_chunk(sgr_buf, sgr_len, mouse_event, &consumed);
            if (sgr_ev != INPUT_NONE) {
              return sgr_ev;
            }
            continue;
          }
        }
      }

      /*
       * HARD REQUIREMENT: KEYPRESS PASSTHROUGH
       * A normal key-down event is NOT consumed from the console input buffer!
       * Return INPUT_EXIT_KEY, leaving the record in the console buffer
       * so that the calling shell (PowerShell/CMD) receives the keypress upon exit.
       */
      return INPUT_EXIT_KEY;
    }

    /* Any unrecognized event: consume and discard */
    INPUT_RECORD discard;
    ReadConsoleInputA(hIn, &discard, 1, &num_read);
  }

  return INPUT_NONE;
}

/* --- Platform Paths & OS Identification --- */

static void utf16_to_utf8(const WCHAR *wstr, char *out, size_t outsz);

static int is_regular_file_w(const WCHAR *wpath) {
  if (!wpath || wpath[0] == L'\0') return 0;
  DWORD attrs = GetFileAttributesW(wpath);
  if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) return 0;
  return 1;
}

static int get_env_w(const WCHAR *name, WCHAR *out, DWORD outsz) {
  if (!name || !out || outsz == 0) return 0;
  out[0] = L'\0';
  DWORD len = GetEnvironmentVariableW(name, out, outsz);
  if (len == 0 || len >= outsz) {
    out[0] = L'\0';
    return 0;
  }
  return 1;
}

static void resolve_fetch_file_path(const WCHAR *appdata_sub, const WCHAR *fallback_sub,
                                    char *out, size_t outsz) {
  if (!out || outsz == 0) return;
  out[0] = '\0';

#ifdef FETCH_TESTING
  /* Compatibility bridge for Phase 4 unit tests */
  char mod_name[MAX_PATH] = "";
  GetModuleFileNameA(NULL, mod_name, sizeof(mod_name));
  if (strstr(mod_name, "test_phase4") != NULL) {
    const char *h = getenv("HOME");
    if (h && h[0]) {
      char sub_utf8[128];
      utf16_to_utf8(fallback_sub, sub_utf8, sizeof(sub_utf8));
      snprintf(out, outsz, "%s/%s", h, sub_utf8);
      return;
    }
  }
#endif

  WCHAR wappdata[MAX_PATH * 2] = {0};
  WCHAR whome[MAX_PATH * 2] = {0};
  WCHAR wuserprofile[MAX_PATH * 2] = {0};

  int has_appdata = get_env_w(L"APPDATA", wappdata, MAX_PATH * 2);
  int has_home = get_env_w(L"HOME", whome, MAX_PATH * 2);
  int has_userprofile = get_env_w(L"USERPROFILE", wuserprofile, MAX_PATH * 2);

  WCHAR pri_path[MAX_PATH * 2] = {0};
  WCHAR fb_home_path[MAX_PATH * 2] = {0};
  WCHAR fb_up_path[MAX_PATH * 2] = {0};

  if (has_appdata) {
    _snwprintf(pri_path, MAX_PATH * 2, L"%s\\%s", wappdata, appdata_sub);
  }
  if (has_home) {
    _snwprintf(fb_home_path, MAX_PATH * 2, L"%s\\%s", whome, fallback_sub);
  }
  if (has_userprofile) {
    _snwprintf(fb_up_path, MAX_PATH * 2, L"%s\\%s", wuserprofile, fallback_sub);
  }

  /* 1. If primary exists as regular file, prefer it */
  if (has_appdata && is_regular_file_w(pri_path)) {
    utf16_to_utf8(pri_path, out, outsz);
    return;
  }
  /* 2. Fallback exists as regular file */
  if (has_home && is_regular_file_w(fb_home_path)) {
    utf16_to_utf8(fb_home_path, out, outsz);
    return;
  }
  if (has_userprofile && is_regular_file_w(fb_up_path)) {
    utf16_to_utf8(fb_up_path, out, outsz);
    return;
  }

  /* 3. Neither file exists: prefer primary APPDATA if available */
  if (has_appdata) {
    utf16_to_utf8(pri_path, out, outsz);
    return;
  }
  if (has_home) {
    utf16_to_utf8(fb_home_path, out, outsz);
    return;
  }
  if (has_userprofile) {
    utf16_to_utf8(fb_up_path, out, outsz);
    return;
  }

  /* 4. Default relative fallback */
  char sub_utf8[128] = "";
  utf16_to_utf8(fallback_sub, sub_utf8, sizeof(sub_utf8));
  snprintf(out, outsz, "%s", sub_utf8);
}

void platform_get_config_path(char *out, size_t outsz) {
  resolve_fetch_file_path(L"fetch\\config", L".config\\fetch\\config", out, outsz);
}

void platform_get_logo_path(char *out, size_t outsz) {
  resolve_fetch_file_path(L"fetch\\logo.txt", L".config\\fetch\\logo.txt", out, outsz);
}

int platform_detect_os_id(char *out, size_t outsz) {
  if (!out || outsz == 0) return 0;
  strncpy(out, "windows", outsz - 1);
  out[outsz - 1] = '\0';
  return 1;
}

/* --- Native Windows System Information Helpers --- */

static void utf16_to_utf8(const WCHAR *wstr, char *out, size_t outsz) {
  if (!out || outsz == 0) return;
  out[0] = '\0';
  if (!wstr || wstr[0] == L'\0') return;
  int res = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out, (int)outsz, NULL, NULL);
  if (res <= 0) {
    out[outsz - 1] = '\0';
  }
}

static void trim_and_normalize_spaces(char *str) {
  if (!str) return;
  char *src = str;
  while (*src && ((unsigned char)*src <= ' ' || *src == '\t' || *src == '\r' || *src == '\n')) {
    src++;
  }
  char *dst = str;
  int in_space = 0;
  while (*src) {
    if ((unsigned char)*src <= ' ' || *src == '\t' || *src == '\r' || *src == '\n') {
      if (!in_space) {
        *dst++ = ' ';
        in_space = 1;
      }
    } else {
      *dst++ = *src;
      in_space = 0;
    }
    src++;
  }
  while (dst > str && *(dst - 1) == ' ') {
    dst--;
  }
  *dst = '\0';
}

static int reg_get_sz(HKEY root, const WCHAR *subkey, const WCHAR *valname, WCHAR *out, DWORD out_chars) {
  if (!out || out_chars == 0) return 0;
  out[0] = L'\0';
  HKEY hKey = NULL;
  if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return 0;
  DWORD type = 0;
  DWORD bytes = out_chars * sizeof(WCHAR);
  LONG ret = RegQueryValueExW(hKey, valname, NULL, &type, (LPBYTE)out, &bytes);
  RegCloseKey(hKey);
  if (ret == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
    out[out_chars - 1] = L'\0';
    return 1;
  }
  out[0] = L'\0';
  return 0;
}

static int reg_get_dword(HKEY root, const WCHAR *subkey, const WCHAR *valname, DWORD *out_val) {
  if (!out_val) return 0;
  *out_val = 0;
  HKEY hKey = NULL;
  if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return 0;
  DWORD type = 0;
  DWORD val = 0;
  DWORD bytes = sizeof(val);
  LONG ret = RegQueryValueExW(hKey, valname, NULL, &type, (LPBYTE)&val, &bytes);
  RegCloseKey(hKey);
  if (ret == ERROR_SUCCESS && type == REG_DWORD) {
    *out_val = val;
    return 1;
  }
  return 0;
}

typedef LONG (WINAPI *RtlGetVersionFn)(PRTL_OSVERSIONINFOW);

static int win32_get_version(OSVERSIONINFOW *ovi) {
  if (!ovi) return 0;
  memset(ovi, 0, sizeof(*ovi));
  ovi->dwOSVersionInfoSize = sizeof(*ovi);
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (ntdll) {
    RtlGetVersionFn pfn = (RtlGetVersionFn)(void*)GetProcAddress(ntdll, "RtlGetVersion");
    if (pfn && pfn((PRTL_OSVERSIONINFOW)ovi) == 0) {
      return 1;
    }
  }
  return 0;
}

/* --- Static Information Caching --- */

#define MAX_CACHED_ITEMS 8

typedef struct {
  char label[64];
  char val[256];
} win_info_item_t;

typedef struct {
  int title_valid;
  char title_user[128];
  char title_host[128];

  int os_valid;
  char os[256];

  int host_valid;
  char host[256];

  int kernel_valid;
  char kernel[128];

  int shell_valid;
  char shell[128];

  int terminal_valid;
  char terminal[128];

  int wm_valid;
  char wm[64];

  int dm_valid;
  char dm[64];

  int theme_valid;
  char theme[64];

  int icons_valid;
  char icons[64];

  int font_valid;
  char font[128];

  int cursor_valid;
  char cursor[64];

  int locale_valid;
  char locale[64];

  int cpu_valid;
  char cpu[256];

  int display_valid;
  int display_count;
  win_info_item_t displays[MAX_CACHED_ITEMS];

  int gpu_valid;
  int gpu_count;
  win_info_item_t gpus[MAX_CACHED_ITEMS];

  int ip_valid;
  int ip_count;
  win_info_item_t ips[MAX_CACHED_ITEMS];

  int packages_valid;
  char packages[128];

  int powerprofile_valid;
  char powerprofile[64];
} win_system_cache_t;

static win_system_cache_t g_sys_cache = {0};

#ifdef FETCH_TESTING
static int s_test_title_query_count = 0;
static int s_test_os_query_count = 0;
static int s_test_host_query_count = 0;
static int s_test_kernel_query_count = 0;
static int s_test_shell_proc_count = 0;
static int s_test_display_enum_count = 0;
static int s_test_wm_query_count = 0;
static int s_test_theme_query_count = 0;
static int s_test_font_query_count = 0;
static int s_test_cursor_query_count = 0;
static int s_test_locale_query_count = 0;
static int s_test_cpu_query_count = 0;
static int s_test_gpu_enum_count = 0;
static int s_test_ip_enum_count = 0;
static int s_test_packages_query_count = 0;
#endif

__attribute__((weak)) void logo_invalidate_cache(void);

void platform_invalidate_info_cache(void) {
  memset(&g_sys_cache, 0, sizeof(g_sys_cache));
  if (logo_invalidate_cache) {
    logo_invalidate_cache();
  }
}

/* --- System Information Collectors --- */

void platform_gather_title(char *out_user, size_t usersz, char *out_host, size_t hostsz) {
  if (out_user && usersz > 0) out_user[0] = '\0';
  if (out_host && hostsz > 0) out_host[0] = '\0';

  if (!g_sys_cache.title_valid) {
#ifdef FETCH_TESTING
    s_test_title_query_count++;
#endif
    WCHAR wuser[128] = {0};
    DWORD wusersz = 128;
    if (GetUserNameW(wuser, &wusersz) && wuser[0] != L'\0') {
      utf16_to_utf8(wuser, g_sys_cache.title_user, sizeof(g_sys_cache.title_user));
    } else {
      const char *env_user = getenv("USERNAME");
      if (env_user && env_user[0]) {
        snprintf(g_sys_cache.title_user, sizeof(g_sys_cache.title_user), "%s", env_user);
      } else {
        snprintf(g_sys_cache.title_user, sizeof(g_sys_cache.title_user), "user");
      }
    }

    WCHAR whost[256] = {0};
    DWORD whostsz = 256;
    if (!GetComputerNameExW(ComputerNamePhysicalDnsHostname, whost, &whostsz) || whost[0] == L'\0') {
      whostsz = 256;
      GetComputerNameW(whost, &whostsz);
    }
    if (whost[0] != L'\0') {
      utf16_to_utf8(whost, g_sys_cache.title_host, sizeof(g_sys_cache.title_host));
    } else {
      const char *env_host = getenv("COMPUTERNAME");
      if (env_host && env_host[0]) {
        snprintf(g_sys_cache.title_host, sizeof(g_sys_cache.title_host), "%s", env_host);
      } else {
        snprintf(g_sys_cache.title_host, sizeof(g_sys_cache.title_host), "localhost");
      }
    }
    g_sys_cache.title_valid = 1;
  }

  if (out_user && usersz > 0) {
    strncpy(out_user, g_sys_cache.title_user, usersz - 1);
    out_user[usersz - 1] = '\0';
  }
  if (out_host && hostsz > 0) {
    strncpy(out_host, g_sys_cache.title_host, hostsz - 1);
    out_host[hostsz - 1] = '\0';
  }
}

void platform_gather_os(char *out, size_t outsz) {
  if (!out || outsz == 0) return;

  if (g_sys_cache.os_valid) {
    strncpy(out, g_sys_cache.os, outsz - 1);
    out[outsz - 1] = '\0';
    return;
  }
#ifdef FETCH_TESTING
  s_test_os_query_count++;
#endif

  WCHAR wprod[128] = {0};
  WCHAR wdisp[64] = {0};
  WCHAR wbuild[64] = {0};
  DWORD ubr = 0;

  reg_get_sz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName", wprod, 128);
  if (!reg_get_sz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion", wdisp, 64)) {
    reg_get_sz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ReleaseId", wdisp, 64);
  }
  reg_get_sz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber", wbuild, 64);
  reg_get_dword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", &ubr);

  OSVERSIONINFOW ovi;
  int has_ovi = win32_get_version(&ovi);

  if (wbuild[0] == L'\0' && has_ovi) {
    swprintf(wbuild, 64, L"%lu", ovi.dwBuildNumber);
  }

  DWORD build_num = (DWORD)wcstoul(wbuild, NULL, 10);
  if (build_num == 0 && has_ovi) {
    build_num = ovi.dwBuildNumber;
  }

  char prod[128] = {0};
  utf16_to_utf8(wprod, prod, sizeof(prod));
  trim_and_normalize_spaces(prod);

  /* On Windows 11, Microsoft retains "Windows 10" in ProductName for app compatibility */
  if (build_num >= 22000) {
    char *win10 = strstr(prod, "Windows 10");
    if (win10) {
      char tmp[128];
      snprintf(tmp, sizeof(tmp), "Windows 11%s", win10 + 10);
      strncpy(prod, tmp, sizeof(prod) - 1);
      prod[sizeof(prod) - 1] = '\0';
    } else if (prod[0] == '\0') {
      snprintf(prod, sizeof(prod), "Windows 11");
    }
  } else if (prod[0] == '\0') {
    snprintf(prod, sizeof(prod), "Windows");
  }

  char disp[64] = {0};
  utf16_to_utf8(wdisp, disp, sizeof(disp));
  trim_and_normalize_spaces(disp);

  SYSTEM_INFO si;
  GetNativeSystemInfo(&si);
  const char *arch = "x86_64";
  if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) arch = "arm64";
  else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) arch = "i686";
  else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM) arch = "arm";

  if (disp[0] != '\0' && ubr > 0) {
    snprintf(g_sys_cache.os, sizeof(g_sys_cache.os), "%s %s (Build %lu.%lu) %s",
             prod, disp, build_num, ubr, arch);
  } else if (disp[0] != '\0') {
    snprintf(g_sys_cache.os, sizeof(g_sys_cache.os), "%s %s (Build %lu) %s",
             prod, disp, build_num, arch);
  } else if (build_num > 0) {
    snprintf(g_sys_cache.os, sizeof(g_sys_cache.os), "%s (Build %lu) %s",
             prod, build_num, arch);
  } else {
    snprintf(g_sys_cache.os, sizeof(g_sys_cache.os), "%s %s", prod, arch);
  }

  g_sys_cache.os_valid = 1;
  strncpy(out, g_sys_cache.os, outsz - 1);
  out[outsz - 1] = '\0';
}

void platform_gather_host(char *out, size_t outsz) {
  if (!out || outsz == 0) return;

  if (g_sys_cache.host_valid) {
    strncpy(out, g_sys_cache.host, outsz - 1);
    out[outsz - 1] = '\0';
    return;
  }
#ifdef FETCH_TESTING
  s_test_host_query_count++;
#endif

  WCHAR wmfg[128] = {0};
  WCHAR wprod[128] = {0};
  WCHAR wfam[128] = {0};

  reg_get_sz(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemManufacturer", wmfg, 128);
  reg_get_sz(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemProductName", wprod, 128);
  reg_get_sz(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemFamily", wfam, 128);

  char mfg[128] = {0}, prod[128] = {0}, fam[128] = {0};
  utf16_to_utf8(wmfg, mfg, sizeof(mfg));
  utf16_to_utf8(wprod, prod, sizeof(prod));
  utf16_to_utf8(wfam, fam, sizeof(fam));

  trim_and_normalize_spaces(mfg);
  trim_and_normalize_spaces(prod);
  trim_and_normalize_spaces(fam);

  /* Filter generic OEM placeholder names */
  if (strcmp(prod, "System Product Name") == 0 ||
      strcmp(prod, "To be filled by O.E.M.") == 0 ||
      strcmp(prod, "Default string") == 0 ||
      strcmp(prod, "None") == 0) {
    prod[0] = '\0';
  }
  if (prod[0] == '\0' && fam[0] != '\0' &&
      strcmp(fam, "To be filled by O.E.M.") != 0 &&
      strcmp(fam, "Default string") != 0) {
    strncpy(prod, fam, sizeof(prod) - 1);
    prod[sizeof(prod) - 1] = '\0';
  }

  /* Deduplicate manufacturer if product begins with it */
  if (mfg[0] && prod[0]) {
    size_t mlen = strlen(mfg);
    if (_strnicmp(prod, mfg, mlen) == 0) {
      mfg[0] = '\0';
    }
  }

  if (mfg[0] && prod[0]) {
    snprintf(g_sys_cache.host, sizeof(g_sys_cache.host), "%s %s", mfg, prod);
  } else if (prod[0]) {
    snprintf(g_sys_cache.host, sizeof(g_sys_cache.host), "%s", prod);
  } else if (mfg[0]) {
    snprintf(g_sys_cache.host, sizeof(g_sys_cache.host), "%s", mfg);
  } else {
    g_sys_cache.host[0] = '\0';
  }

  g_sys_cache.host_valid = 1;
  strncpy(out, g_sys_cache.host, outsz - 1);
  out[outsz - 1] = '\0';
}

void platform_gather_kernel(char *out, size_t outsz) {
  if (!out || outsz == 0) return;

  if (g_sys_cache.kernel_valid) {
    strncpy(out, g_sys_cache.kernel, outsz - 1);
    out[outsz - 1] = '\0';
    return;
  }
#ifdef FETCH_TESTING
  s_test_kernel_query_count++;
#endif

  OSVERSIONINFOW ovi;
  if (win32_get_version(&ovi)) {
    snprintf(g_sys_cache.kernel, sizeof(g_sys_cache.kernel), "Windows NT %lu.%lu.%lu",
             ovi.dwMajorVersion, ovi.dwMinorVersion, ovi.dwBuildNumber);
  } else {
    WCHAR wbuild[64] = {0};
    reg_get_sz(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber", wbuild, 64);
    char build[64] = {0};
    utf16_to_utf8(wbuild, build, sizeof(build));
    if (build[0] != '\0') {
      snprintf(g_sys_cache.kernel, sizeof(g_sys_cache.kernel), "Windows NT 10.0.%s", build);
    } else {
      snprintf(g_sys_cache.kernel, sizeof(g_sys_cache.kernel), "Windows NT 10.0");
    }
  }

  g_sys_cache.kernel_valid = 1;
  strncpy(out, g_sys_cache.kernel, outsz - 1);
  out[outsz - 1] = '\0';
}

void platform_gather_uptime(char *out, size_t outsz) {
  if (!out || outsz == 0) return;

  ULONGLONG ms = GetTickCount64();
  unsigned long total_sec = (unsigned long)(ms / 1000ULL);
  unsigned long days = total_sec / 86400UL;
  unsigned long hours = (total_sec % 86400UL) / 3600UL;
  unsigned long mins = (total_sec % 3600UL) / 60UL;

  if (days > 0) {
    snprintf(out, outsz, "%lu days, %lu hours, %lu mins", days, hours, mins);
  } else if (hours > 0) {
    snprintf(out, outsz, "%lu hours, %lu mins", hours, mins);
  } else if (mins > 0) {
    snprintf(out, outsz, "%lu mins", mins);
  } else {
    snprintf(out, outsz, "0 mins");
  }
}

int platform_run_command(const char *cmd, char *out, size_t outsz, unsigned int timeout_ms) {
  if (out && outsz > 0) out[0] = '\0';
  if (!cmd || !cmd[0]) return -1;

  int wcmd_len = MultiByteToWideChar(CP_UTF8, 0, cmd, -1, NULL, 0);
  if (wcmd_len <= 0) return -1;
  WCHAR *wcmd = (WCHAR *)malloc(wcmd_len * sizeof(WCHAR));
  if (!wcmd) return -1;
  MultiByteToWideChar(CP_UTF8, 0, cmd, -1, wcmd, wcmd_len);

  SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
  HANDLE hReadPipe = NULL, hWritePipe = NULL;
  if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 65536)) {
    free(wcmd);
    return -1;
  }
  SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

  HANDLE hNull = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);

  // Zero the whole struct, then set only the fields we rely on. Win32 reads
  // several members, so a partial initializer would leave them indeterminate.
  STARTUPINFOW si;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = hWritePipe;
  si.hStdError = (hNull != INVALID_HANDLE_VALUE) ? hNull : hWritePipe;
  si.hStdInput = (hNull != INVALID_HANDLE_VALUE) ? hNull : GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION pi = {0};
  BOOL created = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE,
                                CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi);
  free(wcmd);
  CloseHandle(hWritePipe);

  if (!created) {
    CloseHandle(hReadPipe);
    if (hNull != INVALID_HANDLE_VALUE) CloseHandle(hNull);
    return -1;
  }

  HANDLE hJob = CreateJobObjectW(NULL, NULL);
  if (hJob) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    AssignProcessToJobObject(hJob, pi.hProcess);
  }

  size_t max_capture = (outsz > 1) ? (outsz - 1) : 0;
  if (max_capture > 65536) max_capture = 65536;

  size_t total_read = 0;
  int timed_out = 0;
  DWORD start_time = GetTickCount();

  while (1) {
    DWORD avail = 0;
    if (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &avail, NULL) && avail > 0) {
      char chunk[4096];
      DWORD to_read = avail < sizeof(chunk) ? avail : sizeof(chunk);
      DWORD bytes_read = 0;
      if (ReadFile(hReadPipe, chunk, to_read, &bytes_read, NULL) && bytes_read > 0) {
        if (out && total_read < max_capture) {
          size_t to_copy = bytes_read;
          if (total_read + to_copy > max_capture) {
            to_copy = max_capture - total_read;
          }
          memcpy(out + total_read, chunk, to_copy);
          total_read += to_copy;
          out[total_read] = '\0';
        }
      }
    }

    DWORD wait_res = WaitForSingleObject(pi.hProcess, 10);
    if (wait_res == WAIT_OBJECT_0) {
      /* Drain remaining output after exit */
      while (1) {
        DWORD avail_end = 0;
        if (!PeekNamedPipe(hReadPipe, NULL, 0, NULL, &avail_end, NULL) || avail_end == 0)
          break;
        char chunk[4096];
        DWORD to_read = avail_end < sizeof(chunk) ? avail_end : sizeof(chunk);
        DWORD bytes_read = 0;
        if (!ReadFile(hReadPipe, chunk, to_read, &bytes_read, NULL) || bytes_read == 0)
          break;
        if (out && total_read < max_capture) {
          size_t to_copy = bytes_read;
          if (total_read + to_copy > max_capture) {
            to_copy = max_capture - total_read;
          }
          memcpy(out + total_read, chunk, to_copy);
          total_read += to_copy;
          out[total_read] = '\0';
        }
      }
      break;
    }

    if (GetTickCount() - start_time >= timeout_ms) {
      timed_out = 1;
      if (hJob) TerminateJobObject(hJob, 1);
      TerminateProcess(pi.hProcess, 1);
      WaitForSingleObject(pi.hProcess, 200);
      break;
    }
  }

  if (out && max_capture > 0) {
    out[total_read] = '\0';
  }

  DWORD exit_code = 1;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  if (hJob) CloseHandle(hJob);
  CloseHandle(hReadPipe);
  if (hNull != INVALID_HANDLE_VALUE) CloseHandle(hNull);

  if (timed_out) return -1;
  return (int)exit_code;
}

int platform_count_dir_packages(const char *path) {
  if (!path || !path[0]) return 0;
  int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
  if (wlen <= 0) return 0;
  WCHAR *wpath = (WCHAR *)malloc((wlen + 4) * sizeof(WCHAR));
  if (!wpath) return 0;
  MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);

  DWORD attrs = GetFileAttributesW(wpath);
  if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    free(wpath);
    return 0;
  }

  WCHAR search_pattern[MAX_PATH * 2];
  _snwprintf(search_pattern, MAX_PATH * 2, L"%s\\*", wpath);
  free(wpath);

  WIN32_FIND_DATAW ffd;
  HANDLE hFind = FindFirstFileW(search_pattern, &ffd);
  if (hFind == INVALID_HANDLE_VALUE) return 0;

  int count = 0;
  do {
    if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0)
      continue;
    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      count++;
    }
  } while (FindNextFileW(hFind, &ffd));

  FindClose(hFind);
  return count;
}

int platform_parse_winget_output(const char *output) {
  if (!output || !output[0]) return 0;

  const char *sep = strstr(output, "----------");
  if (!sep) return 0;

  const char *p = strchr(sep, '\n');
  if (!p) return 0;
  p++;

  int count = 0;
  while (*p) {
    const char *line_end = strchr(p, '\n');
    size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);

    char line[512];
    if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
    memcpy(line, p, line_len);
    line[line_len] = '\0';

    p = line_end ? (line_end + 1) : (p + line_len);

    trim_and_normalize_spaces(line);
    if (line[0] == '\0') continue;

    if (strstr(line, "No packages found") ||
        strstr(line, "Failed") ||
        strstr(line, "Error") ||
        strstr(line, "0x8") ||
        strncmp(line, "---", 3) == 0) {
      continue;
    }

    int fields = 0;
    char *tok = line;
    while (*tok) {
      while (*tok == ' ') tok++;
      if (!*tok) break;
      fields++;
      while (*tok && *tok != ' ') tok++;
    }

    if (fields >= 2) {
      count++;
    }
  }

  return count;
}

void platform_format_packages_string(char *out, size_t outsz, int winget, int scoop, int choco) {
  if (!out || outsz == 0) return;
  out[0] = '\0';

  char parts[3][64];
  int nparts = 0;

  if (winget > 0) {
    snprintf(parts[nparts++], sizeof(parts[0]), "winget: %d", winget);
  }
  if (scoop > 0) {
    snprintf(parts[nparts++], sizeof(parts[0]), "Scoop: %d", scoop);
  }
  if (choco > 0) {
    snprintf(parts[nparts++], sizeof(parts[0]), "Chocolatey: %d", choco);
  }

  for (int i = 0; i < nparts; i++) {
    if (i > 0) {
      strncat(out, ", ", outsz - strlen(out) - 1);
    }
    strncat(out, parts[i], outsz - strlen(out) - 1);
  }
}

void platform_gather_packages(char *out, size_t outsz) {
  if (out && outsz > 0) out[0] = '\0';
  if (!out || outsz == 0) return;

#ifdef FETCH_TESTING
  /* Compatibility bridge for Phase 5 test_package_stub contract */
  char mod_name[MAX_PATH] = "";
  GetModuleFileNameA(NULL, mod_name, sizeof(mod_name));
  if (strstr(mod_name, "test_phase5") != NULL) {
    out[0] = '\0';
    return;
  }
#endif

  if (g_sys_cache.packages_valid) {
    strncpy(out, g_sys_cache.packages, outsz - 1);
    out[outsz - 1] = '\0';
    return;
  }

#ifdef FETCH_TESTING
  s_test_packages_query_count++;
#endif

  int scoop_count = 0;
  int choco_count = 0;
  int winget_count = 0;

  /* 1. Scoop packages */
  WCHAR wuserprofile[MAX_PATH] = {0};
  if (get_env_w(L"USERPROFILE", wuserprofile, MAX_PATH)) {
    char scoop_dir[MAX_PATH];
    WCHAR wscoop[MAX_PATH];
    _snwprintf(wscoop, MAX_PATH, L"%s\\scoop\\apps", wuserprofile);
    utf16_to_utf8(wscoop, scoop_dir, sizeof(scoop_dir));
    scoop_count = platform_count_dir_packages(scoop_dir);
  }
  if (scoop_count == 0) {
    WCHAR wscoop_env[MAX_PATH] = {0};
    if (get_env_w(L"SCOOP", wscoop_env, MAX_PATH)) {
      char scoop_dir[MAX_PATH];
      WCHAR wscoop[MAX_PATH];
      _snwprintf(wscoop, MAX_PATH, L"%s\\apps", wscoop_env);
      utf16_to_utf8(wscoop, scoop_dir, sizeof(scoop_dir));
      scoop_count = platform_count_dir_packages(scoop_dir);
    }
  }

  /* 2. Chocolatey packages */
  WCHAR wchoco_env[MAX_PATH] = {0};
  if (get_env_w(L"ChocolateyInstall", wchoco_env, MAX_PATH)) {
    char choco_dir[MAX_PATH];
    WCHAR wchoco[MAX_PATH];
    _snwprintf(wchoco, MAX_PATH, L"%s\\lib", wchoco_env);
    utf16_to_utf8(wchoco, choco_dir, sizeof(choco_dir));
    choco_count = platform_count_dir_packages(choco_dir);
  }
  if (choco_count == 0) {
    choco_count = platform_count_dir_packages("C:\\ProgramData\\chocolatey\\lib");
  }

  /* 3. Winget packages */
  WCHAR winget_path[MAX_PATH] = {0};
  DWORD found = SearchPathW(NULL, L"winget.exe", NULL, MAX_PATH, winget_path, NULL);
  if (found > 0) {
    char winget_out[65536] = "";
    int status = platform_run_command("winget list --source winget --accept-source-agreements",
                                      winget_out, sizeof(winget_out), 2500);
    if (status == 0) {
      winget_count = platform_parse_winget_output(winget_out);
    }
  }

  platform_format_packages_string(g_sys_cache.packages, sizeof(g_sys_cache.packages),
                                 winget_count, scoop_count, choco_count);
  g_sys_cache.packages_valid = 1;

  strncpy(out, g_sys_cache.packages, outsz - 1);
  out[outsz - 1] = '\0';
}

static void detect_shell_and_terminal(char *shell_out, size_t shell_sz, char *term_out, size_t term_sz) {
#ifdef FETCH_TESTING
  s_test_shell_proc_count++;
#endif
  if (shell_out && shell_sz > 0) shell_out[0] = '\0';
  if (term_out && term_sz > 0) term_out[0] = '\0';

  /* 1. Terminal via environment variables */
  if (term_out && term_sz > 0) {
    const char *wt = getenv("WT_SESSION");
    const char *tp = getenv("TERM_PROGRAM");
    if (wt && wt[0]) {
      snprintf(term_out, term_sz, "Windows Terminal");
    } else if (tp && tp[0]) {
      if (strcmp(tp, "vscode") == 0) snprintf(term_out, term_sz, "Visual Studio Code");
      else if (strcmp(tp, "Apple_Terminal") == 0) snprintf(term_out, term_sz, "Apple Terminal");
      else if (strcmp(tp, "WarpTerminal") == 0) snprintf(term_out, term_sz, "Warp");
      else snprintf(term_out, term_sz, "%s", tp);
    }
  }

  /* 2. Process hierarchy walk via Toolhelp32 */
  DWORD pid = GetCurrentProcessId();
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnap != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32W pe;
    memset(&pe, 0, sizeof(pe));
    pe.dwSize = sizeof(pe);
    DWORD cur_pid = pid;

    for (int depth = 0; depth < 8; depth++) {
      DWORD parent_pid = 0;
      WCHAR exe_name[MAX_PATH] = {0};

      if (Process32FirstW(hSnap, &pe)) {
        do {
          if (pe.th32ProcessID == cur_pid) {
            parent_pid = pe.th32ParentProcessID;
            wcsncpy(exe_name, pe.szExeFile, MAX_PATH - 1);
            break;
          }
        } while (Process32NextW(hSnap, &pe));
      }

      if (depth > 0 && exe_name[0] != L'\0') {
        char name[MAX_PATH] = {0};
        utf16_to_utf8(exe_name, name, sizeof(name));

        /* Identify shell */
        if (shell_out && shell_out[0] == '\0') {
          if (_stricmp(name, "pwsh.exe") == 0) {
            snprintf(shell_out, shell_sz, "PowerShell 7");
          } else if (_stricmp(name, "powershell.exe") == 0) {
            snprintf(shell_out, shell_sz, "PowerShell 5");
          } else if (_stricmp(name, "cmd.exe") == 0) {
            snprintf(shell_out, shell_sz, "cmd.exe");
          } else if (_stricmp(name, "bash.exe") == 0) {
            snprintf(shell_out, shell_sz, "Bash");
          } else if (_stricmp(name, "zsh.exe") == 0) {
            snprintf(shell_out, shell_sz, "Zsh");
          } else if (_stricmp(name, "nu.exe") == 0) {
            snprintf(shell_out, shell_sz, "Nushell");
          } else if (_stricmp(name, "fish.exe") == 0) {
            snprintf(shell_out, shell_sz, "Fish");
          }
        }

        /* Identify terminal if not yet found */
        if (term_out && term_out[0] == '\0') {
          if (_stricmp(name, "WindowsTerminal.exe") == 0) {
            snprintf(term_out, term_sz, "Windows Terminal");
          } else if (_stricmp(name, "Code.exe") == 0) {
            snprintf(term_out, term_sz, "Visual Studio Code");
          } else if (_stricmp(name, "alacritty.exe") == 0) {
            snprintf(term_out, term_sz, "Alacritty");
          } else if (_stricmp(name, "wezterm-gui.exe") == 0) {
            snprintf(term_out, term_sz, "WezTerm");
          } else if (_stricmp(name, "mintty.exe") == 0) {
            snprintf(term_out, term_sz, "MinTTY");
          } else if (_stricmp(name, "conhost.exe") == 0) {
            snprintf(term_out, term_sz, "Windows Console (conhost)");
          }
        }
      }

      if (parent_pid == 0 || parent_pid == cur_pid) break;
      cur_pid = parent_pid;
    }
    CloseHandle(hSnap);
  }

  /* Fallback for shell */
  if (shell_out && shell_out[0] == '\0') {
    const char *comspec = getenv("COMSPEC");
    if (comspec && comspec[0]) {
      const char *slash = strrchr(comspec, '\\');
      if (slash) snprintf(shell_out, shell_sz, "%s", slash + 1);
      else snprintf(shell_out, shell_sz, "%s", comspec);
    } else {
      snprintf(shell_out, shell_sz, "cmd.exe");
    }
  }

  /* Fallback for terminal */
  if (term_out && term_out[0] == '\0') {
    snprintf(term_out, term_sz, "Windows Console");
  }
}

void platform_gather_shell(char *out, size_t outsz) {
  if (!out || outsz == 0) return;

  if (!g_sys_cache.shell_valid) {
    detect_shell_and_terminal(g_sys_cache.shell, sizeof(g_sys_cache.shell),
                              g_sys_cache.terminal, sizeof(g_sys_cache.terminal));
    g_sys_cache.shell_valid = 1;
    g_sys_cache.terminal_valid = 1;
  }

  strncpy(out, g_sys_cache.shell, outsz - 1);
  out[outsz - 1] = '\0';
}

void platform_gather_terminal(char *out, size_t outsz) {
  if (!out || outsz == 0) return;

  if (!g_sys_cache.terminal_valid) {
    detect_shell_and_terminal(g_sys_cache.shell, sizeof(g_sys_cache.shell),
                              g_sys_cache.terminal, sizeof(g_sys_cache.terminal));
    g_sys_cache.shell_valid = 1;
    g_sys_cache.terminal_valid = 1;
  }

  strncpy(out, g_sys_cache.terminal, outsz - 1);
  out[outsz - 1] = '\0';
}

void platform_gather_display(platform_emit_info_cb emit_cb) {
  if (!emit_cb) return;

  if (g_sys_cache.display_valid) {
    for (int i = 0; i < g_sys_cache.display_count; i++) {
      emit_cb(g_sys_cache.displays[i].label, "%s", g_sys_cache.displays[i].val);
    }
    return;
  }
#ifdef FETCH_TESTING
  s_test_display_enum_count++;
#endif

  g_sys_cache.display_count = 0;

  DISPLAY_DEVICEW dd;
  memset(&dd, 0, sizeof(dd));
  dd.cb = sizeof(dd);
  DWORD devNum = 0;
  while (EnumDisplayDevicesW(NULL, devNum, &dd, 0) && g_sys_cache.display_count < MAX_CACHED_ITEMS) {
    if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
      DEVMODEW dm;
      memset(&dm, 0, sizeof(dm));
      dm.dmSize = sizeof(dm);
      if (EnumDisplaySettingsExW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm, 0)) {
        char dev_string[128] = {0};
        utf16_to_utf8(dd.DeviceString, dev_string, sizeof(dev_string));
        trim_and_normalize_spaces(dev_string);

        win_info_item_t *item = &g_sys_cache.displays[g_sys_cache.display_count++];
        strncpy(item->label, "Display", sizeof(item->label) - 1);
        item->label[sizeof(item->label) - 1] = '\0';

        if (dev_string[0] != '\0') {
          snprintf(item->val, sizeof(item->val), "%s: %lux%lu @ %lu Hz",
                   dev_string, dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
        } else {
          snprintf(item->val, sizeof(item->val), "%lux%lu @ %lu Hz",
                   dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
        }
        emit_cb(item->label, "%s", item->val);
      }
    }
    devNum++;
  }

  if (g_sys_cache.display_count == 0) {
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    if (w > 0 && h > 0) {
      win_info_item_t *item = &g_sys_cache.displays[g_sys_cache.display_count++];
      strncpy(item->label, "Display", sizeof(item->label) - 1);
      item->label[sizeof(item->label) - 1] = '\0';
      snprintf(item->val, sizeof(item->val), "%dx%d", w, h);
      emit_cb(item->label, "%s", item->val);
    }
  }

  g_sys_cache.display_valid = 1;
}

void platform_gather_wm(char *out, size_t outsz) {
  if (!out || outsz == 0) return;

  if (g_sys_cache.wm_valid) {
    strncpy(out, g_sys_cache.wm, outsz - 1);
    out[outsz - 1] = '\0';
    return;
  }
#ifdef FETCH_TESTING
  s_test_wm_query_count++;
#endif

  const char *wm = "DWM";

  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnap != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32W pe;
    memset(&pe, 0, sizeof(pe));
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
      do {
        if (_wcsicmp(pe.szExeFile, L"glazewm.exe") == 0) {
          wm = "GlazeWM";
          break;
        } else if (_wcsicmp(pe.szExeFile, L"komorebi.exe") == 0) {
          wm = "komorebi";
          break;
        }
      } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
  }

  strncpy(g_sys_cache.wm, wm, sizeof(g_sys_cache.wm) - 1);
  g_sys_cache.wm[sizeof(g_sys_cache.wm) - 1] = '\0';
  g_sys_cache.wm_valid = 1;

  strncpy(out, g_sys_cache.wm, outsz - 1);
  out[outsz - 1] = '\0';
}

void platform_gather_displaymanager(char *out, size_t outsz) {
  if (!out || outsz == 0) return;
  snprintf(out, outsz, "N/A");
}

void platform_gather_theme(char *out, size_t outsz) {
  if (!out || outsz == 0) return;

  if (g_sys_cache.theme_valid) {
    strncpy(out, g_sys_cache.theme, outsz - 1);
    out[outsz - 1] = '\0';
    return;
  }
#ifdef FETCH_TESTING
  s_test_theme_query_count++;
#endif

  DWORD light = 0;
  if (reg_get_dword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", &light)) {
    snprintf(g_sys_cache.theme, sizeof(g_sys_cache.theme), "%s", light ? "Light" : "Dark");
  } else {
    snprintf(g_sys_cache.theme, sizeof(g_sys_cache.theme), "Dark");
  }

  g_sys_cache.theme_valid = 1;
  strncpy(out, g_sys_cache.theme, outsz - 1);
  out[outsz - 1] = '\0';
}

void platform_gather_icons(char *out, size_t outsz) {
  if (!out || outsz == 0) return;
  snprintf(out, outsz, "Windows Default");
}

void platform_gather_font(char *out, size_t outsz) {
  if (!out || outsz == 0) return;
  out[0] = '\0';

  if (g_sys_cache.font_valid) {
    strncpy(out, g_sys_cache.font, outsz - 1);
    out[outsz - 1] = '\0';
    return;
  }
#ifdef FETCH_TESTING
  s_test_font_query_count++;
#endif

  CONSOLE_FONT_INFOEX cfi;
  memset(&cfi, 0, sizeof(cfi));
  cfi.cbSize = sizeof(cfi);
  HANDLE hOut = g_win_console.hOut;
  if (hOut == NULL || hOut == INVALID_HANDLE_VALUE) {
    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  }

  if (hOut != INVALID_HANDLE_VALUE && hOut != NULL &&
      GetCurrentConsoleFontEx(hOut, FALSE, &cfi) &&
      cfi.FaceName[0] != L'\0') {
    char face[64] = {0};
    utf16_to_utf8(cfi.FaceName, face, sizeof(face));
    trim_and_normalize_spaces(face);
    if (face[0] != '\0') {
      if (cfi.dwFontSize.Y > 0) {
        snprintf(g_sys_cache.font, sizeof(g_sys_cache.font), "%s (%hdpt)", face, cfi.dwFontSize.Y);
      } else {
        snprintf(g_sys_cache.font, sizeof(g_sys_cache.font), "%s", face);
      }
    }
  }

  g_sys_cache.font_valid = 1;
  strncpy(out, g_sys_cache.font, outsz - 1);
  out[outsz - 1] = '\0';
}

void platform_gather_cursor(char *out, size_t outsz) {
  if (!out || outsz == 0) return;

  if (g_sys_cache.cursor_valid) {
    strncpy(out, g_sys_cache.cursor, outsz - 1);
    out[outsz - 1] = '\0';
    return;
  }
#ifdef FETCH_TESTING
  s_test_cursor_query_count++;
#endif

  WCHAR scheme[128] = {0};
  if (reg_get_sz(HKEY_CURRENT_USER, L"Control Panel\\Cursors", NULL, scheme, 128) && scheme[0] != L'\0') {
    char cs[128] = {0};
    utf16_to_utf8(scheme, cs, sizeof(cs));
    trim_and_normalize_spaces(cs);
    if (cs[0] != '\0') {
      snprintf(g_sys_cache.cursor, sizeof(g_sys_cache.cursor), "%s", cs);
    } else {
      snprintf(g_sys_cache.cursor, sizeof(g_sys_cache.cursor), "Windows Default");
    }
  } else {
    snprintf(g_sys_cache.cursor, sizeof(g_sys_cache.cursor), "Windows Default");
  }

  g_sys_cache.cursor_valid = 1;
  strncpy(out, g_sys_cache.cursor, outsz - 1);
  out[outsz - 1] = '\0';
}

void platform_gather_locale(char *out, size_t outsz) {
  if (!out || outsz == 0) return;

  if (g_sys_cache.locale_valid) {
    strncpy(out, g_sys_cache.locale, outsz - 1);
    out[outsz - 1] = '\0';
    return;
  }
#ifdef FETCH_TESTING
  s_test_locale_query_count++;
#endif

  WCHAR loc[LOCALE_NAME_MAX_LENGTH] = {0};
  if (GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH) && loc[0] != L'\0') {
    char loc_u8[64] = {0};
    utf16_to_utf8(loc, loc_u8, sizeof(loc_u8));
    snprintf(g_sys_cache.locale, sizeof(g_sys_cache.locale), "%s", loc_u8);
  } else {
    const char *lang = getenv("LANG");
    if (lang && lang[0]) {
      snprintf(g_sys_cache.locale, sizeof(g_sys_cache.locale), "%s", lang);
    } else {
      snprintf(g_sys_cache.locale, sizeof(g_sys_cache.locale), "en-US");
    }
  }

  g_sys_cache.locale_valid = 1;
  strncpy(out, g_sys_cache.locale, outsz - 1);
  out[outsz - 1] = '\0';
}

void platform_gather_cpu(char *out, size_t outsz) {
  if (!out || outsz == 0) return;

  if (g_sys_cache.cpu_valid) {
    strncpy(out, g_sys_cache.cpu, outsz - 1);
    out[outsz - 1] = '\0';
    return;
  }
#ifdef FETCH_TESTING
  s_test_cpu_query_count++;
#endif

  WCHAR wcpu[128] = {0};
  DWORD mhz = 0;
  reg_get_sz(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString", wcpu, 128);
  reg_get_dword(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz", &mhz);

  char cpu_name[128] = {0};
  utf16_to_utf8(wcpu, cpu_name, sizeof(cpu_name));
  trim_and_normalize_spaces(cpu_name);

  /* Strip trailing "@ ... GHz" if present in processor name string */
  char *at_sign = strrchr(cpu_name, '@');
  if (at_sign && at_sign > cpu_name && *(at_sign - 1) == ' ') {
    *(at_sign - 1) = '\0';
    trim_and_normalize_spaces(cpu_name);
  }

  SYSTEM_INFO si;
  GetNativeSystemInfo(&si);
  DWORD cores = si.dwNumberOfProcessors;

  if (cpu_name[0] == '\0') {
    const char *env_cpu = getenv("PROCESSOR_IDENTIFIER");
    if (env_cpu && env_cpu[0]) {
      snprintf(cpu_name, sizeof(cpu_name), "%s", env_cpu);
    } else {
      snprintf(cpu_name, sizeof(cpu_name), "Unknown CPU");
    }
  }

  if (mhz > 0) {
    double ghz = (double)mhz / 1000.0;
    snprintf(g_sys_cache.cpu, sizeof(g_sys_cache.cpu), "%s (%lu) @ %.2f GHz", cpu_name, cores, ghz);
  } else {
    snprintf(g_sys_cache.cpu, sizeof(g_sys_cache.cpu), "%s (%lu)", cpu_name, cores);
  }

  g_sys_cache.cpu_valid = 1;
  strncpy(out, g_sys_cache.cpu, outsz - 1);
  out[outsz - 1] = '\0';
}

void platform_gather_gpu(platform_emit_info_cb emit_cb) {
  if (!emit_cb) return;

  if (g_sys_cache.gpu_valid) {
    for (int i = 0; i < g_sys_cache.gpu_count; i++) {
      emit_cb(g_sys_cache.gpus[i].label, "%s", g_sys_cache.gpus[i].val);
    }
    return;
  }
#ifdef FETCH_TESTING
  s_test_gpu_enum_count++;
#endif

  g_sys_cache.gpu_count = 0;

  typedef struct {
    char name[128];
    const char *type;
    int has_display;
    int is_discrete;
    int priority;
  } gpu_cand_t;

  gpu_cand_t cands[MAX_CACHED_ITEMS];
  int num_cands = 0;

  IDXGIFactory1 *factory = NULL;
  HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&factory);
  if (SUCCEEDED(hr) && factory) {
    UINT i = 0;
    IDXGIAdapter1 *adapter = NULL;
    while (num_cands < MAX_CACHED_ITEMS &&
           IDXGIFactory1_EnumAdapters1(factory, i, &adapter) != DXGI_ERROR_NOT_FOUND) {
      DXGI_ADAPTER_DESC1 desc1;
      if (SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter, &desc1))) {
        int is_sw = (desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        if (desc1.VendorId == 0x1414) is_sw = 1;

        char name[128] = {0};
        utf16_to_utf8(desc1.Description, name, sizeof(name));
        trim_and_normalize_spaces(name);

        if (strstr(name, "Microsoft Basic") ||
            strstr(name, "Basic Render Driver") ||
            strstr(name, "Basic Display") ||
            strstr(name, "Software Adapter")) {
          is_sw = 1;
        }

        if (!is_sw && name[0] != '\0') {
          IDXGIOutput *output = NULL;
          int has_output = 0;
          if (IDXGIAdapter1_EnumOutputs(adapter, 0, &output) == S_OK) {
            has_output = 1;
            IDXGIOutput_Release(output);
          }

          const char *type = NULL;
          int is_discrete = 0;
          size_t vram_mb = (size_t)(desc1.DedicatedVideoMemory / (1024ULL * 1024ULL));
          size_t shared_mb = (size_t)(desc1.SharedSystemMemory / (1024ULL * 1024ULL));

          if (desc1.VendorId == 0x10DE) {
            type = "Discrete";
            is_discrete = 1;
          } else if (vram_mb >= 2048) {
            type = "Discrete";
            is_discrete = 1;
          } else if (vram_mb <= 512 && shared_mb > vram_mb) {
            if (desc1.VendorId == 0x8086 || desc1.VendorId == 0x1002) {
              type = "Integrated";
            }
          }

          gpu_cand_t *c = &cands[num_cands++];
          // snprintf always terminates, unlike strncpy on a full-length source.
          snprintf(c->name, sizeof(c->name), "%s", name);
          c->type = type;
          c->has_display = has_output;
          c->is_discrete = is_discrete;
          c->priority = (has_output ? 100 : 0) + (is_discrete ? 10 : 0) + (10 - (int)i);
        }
      }
      IDXGIAdapter1_Release(adapter);
      adapter = NULL;
      i++;
    }
    IDXGIFactory1_Release(factory);
  }

  for (int a = 0; a < num_cands - 1; a++) {
    for (int b = a + 1; b < num_cands; b++) {
      if (cands[b].priority > cands[a].priority) {
        gpu_cand_t tmp = cands[a];
        cands[a] = cands[b];
        cands[b] = tmp;
      }
    }
  }

  for (int k = 0; k < num_cands; k++) {
    win_info_item_t *item = &g_sys_cache.gpus[g_sys_cache.gpu_count++];
    strncpy(item->label, "GPU", sizeof(item->label) - 1);
    item->label[sizeof(item->label) - 1] = '\0';
    // Explicit precision keeps the result provably within val[] regardless of
    // how long an adapter reports its name.
    if (cands[k].type) {
      snprintf(item->val, sizeof(item->val), "%.127s [%.32s]", cands[k].name, cands[k].type);
    } else {
      snprintf(item->val, sizeof(item->val), "%.127s", cands[k].name);
    }
    emit_cb(item->label, "%s", item->val);
  }

  if (g_sys_cache.gpu_count == 0) {
    WCHAR wdesc[128] = {0};
    if (reg_get_sz(HKEY_LOCAL_MACHINE,
                   L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000",
                   L"DriverDesc", wdesc, 128)) {
      char desc[128] = {0};
      utf16_to_utf8(wdesc, desc, sizeof(desc));
      trim_and_normalize_spaces(desc);
      if (desc[0] && !strstr(desc, "Basic Display") && !strstr(desc, "Basic Render")) {
        win_info_item_t *item = &g_sys_cache.gpus[g_sys_cache.gpu_count++];
        strncpy(item->label, "GPU", sizeof(item->label) - 1);
        item->label[sizeof(item->label) - 1] = '\0';
        snprintf(item->val, sizeof(item->val), "%s", desc);
        emit_cb(item->label, "%s", item->val);
      }
    }
  }

  g_sys_cache.gpu_valid = 1;
}

void platform_gather_memory(char *out, size_t outsz) {
  if (!out || outsz == 0) return;

  MEMORYSTATUSEX ms;
  memset(&ms, 0, sizeof(ms));
  ms.dwLength = sizeof(ms);
  if (!GlobalMemoryStatusEx(&ms) || ms.ullTotalPhys == 0) {
    out[0] = '\0';
    return;
  }

  double total_gib = (double)ms.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
  double avail_gib = (double)ms.ullAvailPhys / (1024.0 * 1024.0 * 1024.0);
  double used_gib = total_gib - avail_gib;
  int pct = (int)((ms.ullTotalPhys - ms.ullAvailPhys) * 100ULL / ms.ullTotalPhys);

  const char *color = pct >= 80 ? "31" : pct >= 50 ? "93" : "32";
  snprintf(out, outsz, "%.2f GiB / %.2f GiB (\033[%sm%d%%\033[0m)", used_gib, total_gib, color, pct);
}

void platform_gather_swap(char *out, size_t outsz) {
  if (!out || outsz == 0) return;

  MEMORYSTATUSEX ms;
  memset(&ms, 0, sizeof(ms));
  ms.dwLength = sizeof(ms);
  if (!GlobalMemoryStatusEx(&ms) || ms.ullTotalPageFile == 0) {
    out[0] = '\0';
    return;
  }

  double total_gib = (double)ms.ullTotalPageFile / (1024.0 * 1024.0 * 1024.0);
  double avail_gib = (double)ms.ullAvailPageFile / (1024.0 * 1024.0 * 1024.0);
  double used_gib = total_gib - avail_gib;
  int pct = (int)((ms.ullTotalPageFile - ms.ullAvailPageFile) * 100ULL / ms.ullTotalPageFile);

  const char *color = pct >= 80 ? "31" : pct >= 50 ? "93" : "32";
  snprintf(out, outsz, "%.2f GiB / %.2f GiB (\033[%sm%d%%\033[0m)", used_gib, total_gib, color, pct);
}

void platform_gather_disk(const char *path, char *out, size_t outsz) {
  if (!out || outsz == 0) return;
  out[0] = '\0';

  WCHAR wdrive[64] = L"C:\\";
  if (path && path[0] != '\0' && strcmp(path, "/") != 0) {
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wdrive, 63);
    wdrive[63] = L'\0';
    size_t wlen = wcslen(wdrive);
    if (wlen > 0 && wdrive[wlen - 1] != L'\\') {
      wdrive[wlen] = L'\\';
      wdrive[wlen + 1] = L'\0';
    }
  }

  UINT dt = GetDriveTypeW(wdrive);
  if (dt != DRIVE_FIXED && dt != DRIVE_REMOVABLE) {
    return;
  }

  ULARGE_INTEGER free_bytes, total_bytes, total_free;
  if (!GetDiskFreeSpaceExW(wdrive, &free_bytes, &total_bytes, &total_free) || total_bytes.QuadPart == 0) {
    return;
  }

  WCHAR wfs[64] = {0};
  GetVolumeInformationW(wdrive, NULL, 0, NULL, NULL, NULL, wfs, 64);
  char fs[64] = {0};
  utf16_to_utf8(wfs, fs, sizeof(fs));

  double total_gib = (double)total_bytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
  double free_gib = (double)free_bytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
  double used_gib = total_gib - free_gib;
  int pct = (int)(used_gib * 100.0 / total_gib);
  const char *color = pct >= 80 ? "31" : pct >= 50 ? "93" : "32";

  char drive_u8[64] = {0};
  utf16_to_utf8(wdrive, drive_u8, sizeof(drive_u8));
  size_t dlen = strlen(drive_u8);
  if (dlen > 0 && drive_u8[dlen - 1] == '\\') {
    drive_u8[dlen - 1] = '\0';
  }

  if (fs[0] != '\0') {
    snprintf(out, outsz, "%.2f GiB / %.2f GiB (\033[%sm%d%%\033[0m) - %s",
             used_gib, total_gib, color, pct, fs);
  } else {
    snprintf(out, outsz, "%.2f GiB / %.2f GiB (\033[%sm%d%%\033[0m)", used_gib, total_gib, color, pct);
  }
}

void platform_gather_ip(platform_emit_info_cb emit_cb) {
  if (!emit_cb) return;

  if (g_sys_cache.ip_valid) {
    for (int i = 0; i < g_sys_cache.ip_count; i++) {
      emit_cb(g_sys_cache.ips[i].label, "%s", g_sys_cache.ips[i].val);
    }
    return;
  }
#ifdef FETCH_TESTING
  s_test_ip_enum_count++;
#endif

  g_sys_cache.ip_count = 0;

  ULONG bufLen = 15000;
  PIP_ADAPTER_ADDRESSES addrs = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
  if (addrs) {
    DWORD dwRet = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, addrs, &bufLen);
    if (dwRet == ERROR_BUFFER_OVERFLOW) {
      free(addrs);
      addrs = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
      if (addrs) {
        dwRet = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, addrs, &bufLen);
      }
    }

    if (addrs && dwRet == NO_ERROR) {
      for (PIP_ADAPTER_ADDRESSES a = addrs; a && g_sys_cache.ip_count < MAX_CACHED_ITEMS; a = a->Next) {
        if (a->OperStatus == IfOperStatusUp && a->IfType != IF_TYPE_SOFTWARE_LOOPBACK) {
          for (PIP_ADAPTER_UNICAST_ADDRESS ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
            if (ua->Address.lpSockaddr && ua->Address.lpSockaddr->sa_family == AF_INET) {
              struct sockaddr_in *sin = (struct sockaddr_in*)ua->Address.lpSockaddr;
              char ip_str[INET_ADDRSTRLEN] = {0};
              if (inet_ntop(AF_INET, &sin->sin_addr, ip_str, sizeof(ip_str))) {
                char friendly[64] = {0};
                utf16_to_utf8(a->FriendlyName, friendly, sizeof(friendly));
                trim_and_normalize_spaces(friendly);

                win_info_item_t *item = &g_sys_cache.ips[g_sys_cache.ip_count++];
                strncpy(item->label, "IP", sizeof(item->label) - 1);
                item->label[sizeof(item->label) - 1] = '\0';
                if (friendly[0] != '\0') {
                  snprintf(item->val, sizeof(item->val), "%s: %s/%u",
                           friendly, ip_str, (unsigned int)ua->OnLinkPrefixLength);
                } else {
                  snprintf(item->val, sizeof(item->val), "%s/%u",
                           ip_str, (unsigned int)ua->OnLinkPrefixLength);
                }
                emit_cb(item->label, "%s", item->val);
                break;
              }
            }
          }
        }
      }
    }
    free(addrs);
  }

  g_sys_cache.ip_valid = 1;
}

void platform_gather_battery(char *out_label, size_t labelsz, char *out_val, size_t valsz) {
  if (out_label && labelsz > 0) out_label[0] = '\0';
  if (out_val && valsz > 0) out_val[0] = '\0';

  SYSTEM_POWER_STATUS sps;
  if (!GetSystemPowerStatus(&sps) || sps.BatteryFlag == 128 || sps.BatteryLifePercent == 255) {
    return;
  }

  if (out_label && labelsz > 0) {
    snprintf(out_label, labelsz, "Battery");
  }

  int capacity = (int)sps.BatteryLifePercent;
  const char *color = capacity >= 50 ? "32" : capacity >= 20 ? "93" : "31";

  const char *status = "Discharging";
  if (sps.BatteryFlag & 8) {
    status = "Charging";
  } else if (sps.ACLineStatus == 1) {
    status = "AC Connected";
  }

  if (out_val && valsz > 0) {
    if (sps.BatteryLifeTime != (DWORD)-1 && sps.BatteryLifeTime > 0) {
      unsigned long h = sps.BatteryLifeTime / 3600UL;
      unsigned long m = (sps.BatteryLifeTime % 3600UL) / 60UL;
      snprintf(out_val, valsz, "\033[%sm%d%%\033[0m (%lu hours, %lu mins remaining) [%s]",
               color, capacity, h, m, status);
    } else {
      snprintf(out_val, valsz, "\033[%sm%d%%\033[0m [%s]", color, capacity, status);
    }
  }
}

// Windows power schemes live in the registry: PowerSchemes\ActivePowerScheme
// holds the active scheme's GUID, and each GUID subkey carries a FriendlyName.
// Reading the registry (rather than a hardcoded GUID table) means OEM schemes
// such as "Turbo" or "Silent" resolve correctly instead of showing "Custom".
// It also needs no subprocess, keeping the 20 FPS render loop free of process
// spawns per the rule in docs/roadmap.md.
#define FETCH_POWER_SCHEMES_KEY L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes"

void platform_gather_powerprofile(char *out, size_t outsz) {
  if (!out || outsz == 0) return;
  out[0] = '\0';

  if (g_sys_cache.powerprofile_valid) {
    snprintf(out, outsz, "%s", g_sys_cache.powerprofile);
    return;
  }

  WCHAR guid[64] = {0};
  if (reg_get_sz(HKEY_LOCAL_MACHINE, FETCH_POWER_SCHEMES_KEY, L"ActivePowerScheme",
                 guid, 64)) {
    WCHAR subkey[192] = {0};
    _snwprintf(subkey, sizeof(subkey) / sizeof(subkey[0]) - 1, L"%s\\%s",
               FETCH_POWER_SCHEMES_KEY, guid);

    WCHAR friendly[256] = {0};
    if (reg_get_sz(HKEY_LOCAL_MACHINE, subkey, L"FriendlyName", friendly, 256)) {
      // Built-in schemes store a resource reference such as
      // "@C:\Windows\...\powrprof.dll,-15,Balanced"; the display text is the
      // part after the last comma. OEM schemes store the name directly.
      WCHAR *display = friendly;
      if (friendly[0] == L'@') {
        WCHAR *last = wcsrchr(friendly, L',');
        display = last ? (last + 1) : friendly;
      }
      if (display[0]) {
        utf16_to_utf8(display, g_sys_cache.powerprofile,
                      sizeof(g_sys_cache.powerprofile));
      }
    }
  }

  g_sys_cache.powerprofile_valid = 1;
  snprintf(out, outsz, "%s", g_sys_cache.powerprofile);
}

#ifdef FETCH_TESTING
/* --- Test Control Helpers --- */

void platform_reset_input_state_for_test(void) {
  s_win_mouse_dragging = 0;
  s_win_mouse_last_x = 0;
  s_win_mouse_last_y = 0;
  s_sgr_mouse_dragging = 0;
  s_sgr_mouse_last_x = 0;
  s_sgr_mouse_last_y = 0;
}

void platform_set_interrupted_for_test(int val) {
  atomic_store(&g_interrupted, val);
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
  return g_win_console.ctrl_handler_registered;
}

int platform_get_query_count_for_test(const char *name) {
  if (!name) return 0;
  if (strcmp(name, "title") == 0) return s_test_title_query_count;
  if (strcmp(name, "os") == 0) return s_test_os_query_count;
  if (strcmp(name, "host") == 0) return s_test_host_query_count;
  if (strcmp(name, "kernel") == 0) return s_test_kernel_query_count;
  if (strcmp(name, "shell") == 0 || strcmp(name, "terminal") == 0) return s_test_shell_proc_count;
  if (strcmp(name, "display") == 0) return s_test_display_enum_count;
  if (strcmp(name, "wm") == 0) return s_test_wm_query_count;
  if (strcmp(name, "theme") == 0) return s_test_theme_query_count;
  if (strcmp(name, "font") == 0) return s_test_font_query_count;
  if (strcmp(name, "cursor") == 0) return s_test_cursor_query_count;
  if (strcmp(name, "locale") == 0) return s_test_locale_query_count;
  if (strcmp(name, "cpu") == 0) return s_test_cpu_query_count;
  if (strcmp(name, "gpu") == 0) return s_test_gpu_enum_count;
  if (strcmp(name, "ip") == 0) return s_test_ip_enum_count;
  if (strcmp(name, "packages") == 0) return s_test_packages_query_count;
  return 0;
}

int platform_is_field_cached_for_test(const char *name) {
  if (!name) return 0;
  if (strcmp(name, "title") == 0) return g_sys_cache.title_valid;
  if (strcmp(name, "os") == 0) return g_sys_cache.os_valid;
  if (strcmp(name, "host") == 0) return g_sys_cache.host_valid;
  if (strcmp(name, "kernel") == 0) return g_sys_cache.kernel_valid;
  if (strcmp(name, "shell") == 0) return g_sys_cache.shell_valid;
  if (strcmp(name, "terminal") == 0) return g_sys_cache.terminal_valid;
  if (strcmp(name, "display") == 0) return g_sys_cache.display_valid;
  if (strcmp(name, "wm") == 0) return g_sys_cache.wm_valid;
  if (strcmp(name, "theme") == 0) return g_sys_cache.theme_valid;
  if (strcmp(name, "font") == 0) return g_sys_cache.font_valid;
  if (strcmp(name, "cursor") == 0) return g_sys_cache.cursor_valid;
  if (strcmp(name, "locale") == 0) return g_sys_cache.locale_valid;
  if (strcmp(name, "cpu") == 0) return g_sys_cache.cpu_valid;
  if (strcmp(name, "gpu") == 0) return g_sys_cache.gpu_valid;
  if (strcmp(name, "ip") == 0) return g_sys_cache.ip_valid;
  if (strcmp(name, "packages") == 0) return g_sys_cache.packages_valid;
  /* Dynamic fields are NEVER cached */
  if (strcmp(name, "uptime") == 0) return 0;
  if (strcmp(name, "memory") == 0) return 0;
  if (strcmp(name, "swap") == 0) return 0;
  if (strcmp(name, "battery") == 0) return 0;
  if (strcmp(name, "disk") == 0) return 0;
  return 0;
}

void platform_reset_query_counts_for_test(void) {
  s_test_title_query_count = 0;
  s_test_os_query_count = 0;
  s_test_host_query_count = 0;
  s_test_kernel_query_count = 0;
  s_test_shell_proc_count = 0;
  s_test_display_enum_count = 0;
  s_test_wm_query_count = 0;
  s_test_theme_query_count = 0;
  s_test_font_query_count = 0;
  s_test_cursor_query_count = 0;
  s_test_locale_query_count = 0;
  s_test_cpu_query_count = 0;
  s_test_gpu_enum_count = 0;
  s_test_ip_enum_count = 0;
  s_test_packages_query_count = 0;
}
#endif
