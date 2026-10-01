#include "src/logo/logo.h"
#include "src/core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

// Global logo instance
fetch_logo_t g_logo;

// Backward-compatibility global variables
char logo_data[MAX_LOGO_ROWS][512];
char logo_cells[MAX_LOGO_ROWS][MAX_LOGO_COLS][5];
int logo_cell_color[MAX_LOGO_ROWS][MAX_LOGO_COLS];
int logo_cell_counts[MAX_LOGO_ROWS];
int logo_rows = 0;
int logo_cols = 0;
int logo_has_ansi = 0;
char file_distro[64] = "";
char distro_id_like[64] = "";
const char *color_outer = "\033[1;35m";
const char *color_inner = "\033[1;37m";
#ifdef _WIN32
#include <windows.h>
__attribute__((weak)) int platform_run_command(const char *cmd, char *out, size_t outsz, unsigned int timeout_ms);
#endif

__attribute__((weak)) void platform_get_logo_path(char *out, size_t outsz);

static const char *gentoo_ascii[] = {
    "         -/oyddmdhs+:.            ",
    "     -odNMMMMMMMMNNmhy+-`         ",
    "   -yNMMMMMMMMMMMNNNmmdhy+-       ",
    " `omMMMMMMMMMMMMNmdmmmmddhhy/`    ",
    " omMMMMMMMMMMMNhhyyyohmdddhhhdo`  ",
    ".ydMMMMMMMMMMdhs++so/smdddhhhhdm+`",
    " oyhdmNMMMMMMMNdyooydMddddhhhhyhNd.",
    "  :oyhhdNNMMMMMMMNNMMMdddhhhhhyymMh",
    "    .:+sydNMMMMMNNMMMMdddhhhhhhmMmy",
    "       /mMMMMMMNNNMMMdddhhhhhmMNhs:",
    "    `oNMMMMMMMNNNMMMddddhhdmMNhs+` ",
    "  `sNMMMMMMMMNNNMMMdddddmNMmhs/.   ",
    " /NMMMMMMMMNNNNMMMdddmNMNdso:`     ",
    "+MMMMMMMNNNNNMMMMdMNMNdso/-        ",
    "yMMNNNNNNNMMMMMNNMmhs+/-`          ",
    "/hMMNNNNNNNNMNdhs++/-`             ",
    "`/ohdmmddhys+++/:.`                ",
    "  `-//////:--.                     ",
};

static const char *windows_ascii[] = {
    "/////////////////  /////////////////",
    "/////////////////  /////////////////",
    "/////////////////  /////////////////",
    "/////////////////  /////////////////",
    "/////////////////  /////////////////",
    "/////////////////  /////////////////",
    "/////////////////  /////////////////",
    "/////////////////  /////////////////",
    "",
    "/////////////////  /////////////////",
    "/////////////////  /////////////////",
    "/////////////////  /////////////////",
    "/////////////////  /////////////////",
    "/////////////////  /////////////////",
    "/////////////////  /////////////////",
    "/////////////////  /////////////////",
    "/////////////////  /////////////////",
};

int logo_load_builtin(fetch_logo_t *logo, const char *name) {
  if (!logo || !name || !name[0]) return 0;
  if (strcasecmp(name, "gentoo") == 0) {
    logo_init(logo);
    int count = sizeof(gentoo_ascii) / sizeof(gentoo_ascii[0]);
    logo->rows = count;
    for (int i = 0; i < count; i++) {
      int len = strlen(gentoo_ascii[i]);
      memcpy(logo->data[i], gentoo_ascii[i], len + 1);
    }
    strncpy(logo->distro, "gentoo", sizeof(logo->distro) - 1);
    return 1;
  }
  if (strcasecmp(name, "windows") == 0 ||
      strcasecmp(name, "win") == 0 ||
      strcasecmp(name, "win11") == 0 ||
      strcasecmp(name, "win10") == 0) {
    logo_init(logo);
    int count = sizeof(windows_ascii) / sizeof(windows_ascii[0]);
    logo->rows = count;
    for (int i = 0; i < count; i++) {
      int len = strlen(windows_ascii[i]);
      memcpy(logo->data[i], windows_ascii[i], len + 1);
    }
    strncpy(logo->distro, "windows", sizeof(logo->distro) - 1);
    return 1;
  }
  return 0;
}

void logo_init(fetch_logo_t *logo) {
  if (!logo) return;
  memset(logo, 0, sizeof(*logo));
}

void logo_sync_to_globals(const fetch_logo_t *logo) {
  if (!logo) return;
  logo_rows = logo->rows;
  logo_cols = logo->cols;
  logo_has_ansi = logo->has_ansi;
  for (int r = 0; r < MAX_LOGO_ROWS; r++) {
    logo_cell_counts[r] = logo->cell_counts[r];
    memcpy(logo_data[r], logo->data[r], sizeof(logo_data[r]));
    for (int c = 0; c < MAX_LOGO_COLS; c++) {
      memcpy(logo_cells[r][c], logo->cells[r][c], sizeof(logo_cells[r][c]));
      logo_cell_color[r][c] = logo->cell_color[r][c];
    }
  }
  strncpy(file_distro, logo->distro, sizeof(file_distro) - 1);
  file_distro[sizeof(file_distro) - 1] = '\0';
  strncpy(distro_id_like, logo->distro_id_like, sizeof(distro_id_like) - 1);
  distro_id_like[sizeof(distro_id_like) - 1] = '\0';
}

void logo_sync_from_globals(fetch_logo_t *logo) {
  if (!logo) return;
  logo->rows = logo_rows;
  logo->cols = logo_cols;
  logo->has_ansi = logo_has_ansi;
  for (int r = 0; r < MAX_LOGO_ROWS; r++) {
    logo->cell_counts[r] = logo_cell_counts[r];
    memcpy(logo->data[r], logo_data[r], sizeof(logo->data[r]));
    for (int c = 0; c < MAX_LOGO_COLS; c++) {
      memcpy(logo->cells[r][c], logo_cells[r][c], sizeof(logo->cells[r][c]));
      logo->cell_color[r][c] = logo_cell_color[r][c];
    }
  }
  strncpy(logo->distro, file_distro, sizeof(logo->distro) - 1);
  logo->distro[sizeof(logo->distro) - 1] = '\0';
  strncpy(logo->distro_id_like, distro_id_like, sizeof(logo->distro_id_like) - 1);
  logo->distro_id_like[sizeof(logo->distro_id_like) - 1] = '\0';
}

void logo_load_default(fetch_logo_t *logo) {
  if (!logo) return;
  logo_load_builtin(logo, "gentoo");
}

void logo_get_default_path(char *out, size_t outsz) {
  if (!out || outsz == 0) return;
  if (platform_get_logo_path) {
    platform_get_logo_path(out, outsz);
    if (out[0]) return;
  }
  const char *home = getenv("HOME");
  if (home && home[0]) {
    snprintf(out, outsz, "%s/.config/fetch/logo.txt", home);
  } else {
    out[0] = '\0';
  }
}

int logo_load_file(fetch_logo_t *logo, const char *path) {
  if (!logo) return 0;
  char default_path[512];
  if (!path || !path[0]) {
    logo_get_default_path(default_path, sizeof(default_path));
    path = default_path;
  }
  if (!path || !path[0])
    return 0;

  FILE *fp = fopen(path, "r");
  if (!fp)
    return 0;

  logo->rows = 0;
  logo->distro[0] = '\0';

  char buf[512];
  while (logo->rows < MAX_LOGO_ROWS && fgets(buf, sizeof(buf), fp)) {
    int len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
      buf[--len] = '\0';
    if (logo->rows == 0 && strncmp(buf, "# distro:", 9) == 0) {
      char *val = buf + 9;
      while (*val == ' ')
        val++;
      strncpy(logo->distro, val, sizeof(logo->distro) - 1);
      logo->distro[sizeof(logo->distro) - 1] = '\0';
      continue;
    }
    if (len == 0 && logo->rows == 0)
      continue;
    memcpy(logo->data[logo->rows], buf, len + 1);
    logo->rows++;
  }
  fclose(fp);
  while (logo->rows > 0 && logo->data[logo->rows - 1][0] == '\0')
    logo->rows--;
  return logo->rows > 0;
}

static int s_ff_checked = 0;
static int s_ff_available = 0;

void logo_invalidate_cache(void) {
  s_ff_checked = 0;
  s_ff_available = 0;
}

static int is_fastfetch_available(void) {
#ifdef _WIN32
  if (!s_ff_checked) {
    WCHAR path[MAX_PATH];
    s_ff_available = (SearchPathW(NULL, L"fastfetch.exe", NULL, MAX_PATH, path, NULL) > 0);
    s_ff_checked = 1;
  }
  return s_ff_available;
#else
  return 1;
#endif
}

// Try loading a logo from fastfetch colored output
static int load_logo_ff_colored(fetch_logo_t *logo, const char *name) {
  if (!is_fastfetch_available())
    return 0;

  const char *ff_name = name;
  if (strcasecmp(name, "windows") == 0)
    ff_name = "win10";

#ifdef _WIN32
  char cmd[512];
  snprintf(cmd, sizeof(cmd),
           "cmd.exe /c \"fastfetch --show-errors -c none -l %s -s break --pipe false 2>&1\"", ff_name);

  if (platform_run_command) {
    char out_buf[65536] = "";
    int status = platform_run_command(cmd, out_buf, sizeof(out_buf), 2500);
    if (status != 0 || out_buf[0] == '\0')
      return 0;

    if (strstr(out_buf, "Failed to resolve logo source") != NULL)
      return 0;

    char *p = out_buf;
    while (logo->rows < MAX_LOGO_ROWS && *p) {
      char *nl = strchr(p, '\n');
      size_t line_len = nl ? (size_t)(nl - p) : strlen(p);
      char buf[512];
      if (line_len >= sizeof(buf)) line_len = sizeof(buf) - 1;
      memcpy(buf, p, line_len);
      buf[line_len] = '\0';
      p = nl ? (nl + 1) : (p + line_len);

      int len = line_len;
      while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';

      int truncated = 0;
      int last_sgr_end = -1;
      for (int i = 0; i < len - 2; i++) {
        if (is_cursor_escape(&buf[i])) {
          int cut = i;
          if (last_sgr_end >= 0)
            cut = last_sgr_end;
          buf[cut] = '\0';
          len = cut;
          truncated = 1;
          break;
        }
        if (buf[i] == '\033' && buf[i + 1] == '[') {
          int j = i + 2;
          while (buf[j] && ((buf[j] >= '0' && buf[j] <= '9') || buf[j] == ';'))
            j++;
          if (buf[j] == 'm') {
            last_sgr_end = j + 1;
            i = j;
          }
        }
      }

      if (len == 0 && logo->rows == 0)
        continue;
      if (len == 0 && truncated)
        break;

      memcpy(logo->data[logo->rows], buf, len + 1);
      logo->rows++;
    }

    while (logo->rows > 0 && logo->data[logo->rows - 1][0] == '\0')
      logo->rows--;
    return logo->rows > 0;
  }
  return 0;
#else
  char cmd[512];
  snprintf(cmd, sizeof(cmd),
           "fastfetch --show-errors -c none -l %s -s break --pipe false 2>&1", ff_name);
  FILE *fp = popen(cmd, "r");
  if (!fp)
    return 0;

  char buf[512];
  while (logo->rows < MAX_LOGO_ROWS && fgets(buf, sizeof(buf), fp)) {
    if (strstr(buf, "Failed to resolve logo source") != NULL) {
      pclose(fp);
      return 0;
    }
    int len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
      buf[--len] = '\0';

    // Find last SGR escape end before any cursor movement (marks end of logo content)
    int truncated = 0;
    int last_sgr_end = -1;
    for (int i = 0; i < len - 2; i++) {
      if (is_cursor_escape(&buf[i])) {
        int cut = i;
        // If we found a previous complete SGR, cut after it (keep the color reset)
        if (last_sgr_end >= 0)
          cut = last_sgr_end;
        buf[cut] = '\0';
        len = cut;
        truncated = 1;
        break;
      }
      // Track end positions of SGR sequences
      if (buf[i] == '\033' && buf[i + 1] == '[') {
        int j = i + 2;
        while (buf[j] && ((buf[j] >= '0' && buf[j] <= '9') || buf[j] == ';'))
          j++;
        if (buf[j] == 'm') {
          last_sgr_end = j + 1;
          i = j;
        }
      }
    }

    if (len == 0 && logo->rows == 0)
      continue;
    if (len == 0 && truncated)
      break;

    memcpy(logo->data[logo->rows], buf, len + 1);
    logo->rows++;
  }
  pclose(fp);

  while (logo->rows > 0 && logo->data[logo->rows - 1][0] == '\0')
    logo->rows--;
  return logo->rows > 0;
#endif
}

// Fallback: load from --print-logos (no colors, but works on older fastfetch)
static int load_logo_ff_plain(fetch_logo_t *logo, const char *name) {
  if (!is_fastfetch_available())
    return 0;

  const char *ff_name = name;
  if (strcasecmp(name, "windows") == 0)
    ff_name = "win10";

#ifdef _WIN32
  if (platform_run_command) {
    char out_buf[65536] = "";
    int status = platform_run_command("fastfetch -c none --print-logos", out_buf, sizeof(out_buf), 2500);
    if (status != 0 || out_buf[0] == '\0')
      return 0;

    int found = 0;
    int name_len = strlen(ff_name);
    char *p = out_buf;

    while (*p) {
      char *nl = strchr(p, '\n');
      size_t line_len = nl ? (size_t)(nl - p) : strlen(p);
      char buf[512];
      if (line_len >= sizeof(buf)) line_len = sizeof(buf) - 1;
      memcpy(buf, p, line_len);
      buf[line_len] = '\0';
      p = nl ? (nl + 1) : (p + line_len);

      int len = line_len;
      while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';

      if (!found) {
        if (len > 0 && len <= name_len + 1 && buf[len - 1] == ':') {
          buf[len - 1] = '\0';
          if (strcasecmp(buf, ff_name) == 0)
            found = 1;
        }
        continue;
      }

      if (len > 1 && len < 40 && buf[len - 1] == ':' && logo->rows > 0 &&
          ((buf[0] >= 'A' && buf[0] <= 'Z') || (buf[0] >= 'a' && buf[0] <= 'z'))) {
        int is_header = 1;
        for (int i = 0; i < len; i++) {
          if (buf[i] == '\033') {
            is_header = 0;
            break;
          }
        }
        if (is_header)
          break;
      }

      if (logo->rows >= MAX_LOGO_ROWS)
        break;

      memcpy(logo->data[logo->rows], buf, len + 1);
      logo->rows++;
    }

    while (logo->rows > 0 && logo->data[logo->rows - 1][0] == '\0')
      logo->rows--;
    return logo->rows > 0;
  }
  return 0;
#else
  FILE *fp = popen("fastfetch -c none --print-logos 2>/dev/null", "r");
  if (!fp)
    return 0;

  char buf[512];
  int found = 0;
  int name_len = strlen(ff_name);

  while (fgets(buf, sizeof(buf), fp)) {
    int len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
      buf[--len] = '\0';

    if (!found) {
      if (len > 0 && len <= name_len + 1 && buf[len - 1] == ':') {
        buf[len - 1] = '\0';
        if (strcasecmp(buf, ff_name) == 0)
          found = 1;
      }
      continue;
    }

    // Detect next logo header
    if (len > 1 && len < 40 && buf[len - 1] == ':' && logo->rows > 0 &&
        ((buf[0] >= 'A' && buf[0] <= 'Z') ||
         (buf[0] >= 'a' && buf[0] <= 'z'))) {
      int is_header = 1;
      for (int i = 0; i < len; i++) {
        if (buf[i] == '\033') {
          is_header = 0;
          break;
        }
      }
      if (is_header)
        break;
    }

    if (logo->rows >= MAX_LOGO_ROWS)
      break;

    memcpy(logo->data[logo->rows], buf, len + 1);
    logo->rows++;
  }
  pclose(fp);

  while (logo->rows > 0 && logo->data[logo->rows - 1][0] == '\0')
    logo->rows--;
  return logo->rows > 0;
#endif
}

int logo_load_fastfetch(fetch_logo_t *logo, const char *name) {
  if (!logo || !name) return 0;
  // Try colored output first (modern fastfetch)
  if (load_logo_ff_colored(logo, name))
    return 1;
  // Fall back to --print-logos (older fastfetch, no colors)
  return load_logo_ff_plain(logo, name);
}

void logo_process_row(fetch_logo_t *logo, int row) {
  if (!logo || row < 0 || row >= logo->rows) return;
  const char *p = logo->data[row];
  int col = 0;
  int cur_color = 0;
  while (*p && col < MAX_LOGO_COLS) {
    // Parse ANSI escapes for color info
    if (p[0] == '\033' && p[1] == '[') {
      int i = 2;
      // Extract foreground color from SGR params
      int num = 0, has_num = 0;
      while (p[i] && ((p[i] >= '0' && p[i] <= '9') || p[i] == ';')) {
        if (p[i] >= '0' && p[i] <= '9') {
          num = num * 10 + (p[i] - '0');
          has_num = 1;
        } else if (p[i] == ';') {
          if (has_num && ((num >= 30 && num <= 37) || num == 39 ||
                          (num >= 90 && num <= 97)))
            cur_color = num;
          if (has_num && num == 1)
            ; // bold flag — ignored; colors are always output bold
          if (has_num && (num == 0 || num == 22))
            cur_color = 0;
          num = 0;
          has_num = 0;
        }
        i++;
      }
      if (has_num &&
          ((num >= 30 && num <= 37) || num == 39 || (num >= 90 && num <= 97)))
        cur_color = num;
      if (has_num && num == 0)
        cur_color = 0;
      if (p[i])
        i++;
      if (cur_color > 0)
        logo->has_ansi = 1;
      p += i;
      continue;
    }
    int len = utf8_char_len((unsigned char)*p);
    int actual = 0;
    while (actual < len && p[actual])
      actual++;
    memcpy(logo->cells[row][col], p, actual);
    logo->cells[row][col][actual] = '\0';
    logo->cell_color[row][col] = cur_color;
    col++;
    p += actual;
  }
  logo->cell_counts[row] = col;
  if (col > logo->cols)
    logo->cols = col;
}

void logo_process(fetch_logo_t *logo) {
  if (!logo) return;
  logo->cols = 0;
  for (int r = 0; r < logo->rows; r++)
    logo_process_row(logo, r);
}

void logo_set_distro_colors(const char *distro, const char **out_outer, const char **out_inner) {
  const char *outer = "\033[1;35m"; // default bold magenta
  const char *inner = "\033[1;37m"; // default bold white

  if (!distro || !distro[0]) {
    if (out_outer) *out_outer = outer;
    if (out_inner) *out_inner = inner;
    return;
  }

  if (strcasecmp(distro, "gentoo") == 0) {
    outer = "\033[1;35m";
    inner = "\033[1;37m";
  } else if (strcasecmp(distro, "arch") == 0) {
    outer = "\033[1;36m";
    inner = "\033[1;36m";
  } else if (strcasecmp(distro, "ubuntu") == 0) {
    outer = "\033[1;31m";
    inner = "\033[1;37m";
  } else if (strcasecmp(distro, "debian") == 0) {
    outer = "\033[1;31m";
    inner = "\033[1;37m";
  } else if (strcasecmp(distro, "asahi") == 0 ||
             strcasecmp(distro, "asahi2") == 0 ||
             strcasecmp(distro, "fedora-asahi-remix") == 0) {
    outer = "\033[1;31m"; // bold red
    inner = "\033[1;37m"; // bold white
  } else if (strcasecmp(distro, "fedora") == 0 ||
             strncasecmp(distro, "fedora-", 7) == 0) {
    outer = "\033[1;34m";
    inner = "\033[1;37m";
  } else if (strcasecmp(distro, "nixos") == 0) {
    outer = "\033[1;34m";
    inner = "\033[1;36m";
  } else if (strcasecmp(distro, "void") == 0) {
    outer = "\033[1;32m";
    inner = "\033[1;32m";
  } else if (strcasecmp(distro, "alpine") == 0) {
    outer = "\033[1;34m";
    inner = "\033[1;37m";
  } else if (strcasecmp(distro, "opensuse-tumbleweed") == 0 ||
             strcasecmp(distro, "opensuse-leap") == 0 ||
             strcasecmp(distro, "opensuse") == 0) {
    outer = "\033[1;32m";
    inner = "\033[1;37m";
  } else if (strcasecmp(distro, "macos") == 0) {
    outer = "\033[1;36m";
    inner = "\033[1;37m";
  } else if (strcasecmp(distro, "windows") == 0 ||
             strcasecmp(distro, "win") == 0 ||
             strcasecmp(distro, "win11") == 0 ||
             strcasecmp(distro, "win10") == 0) {
    outer = "\033[1;34m"; // bold blue (3D depth/sides)
    inner = "\033[1;36m"; // bold cyan (front face)
  }

  if (out_outer) *out_outer = outer;
  if (out_inner) *out_inner = inner;
}

// Parse a value from os-release, stripping quotes and newlines
#if !defined(_WIN32)
static int parse_os_release_val(const char *buf, int prefix_len, char *out,
                                int maxlen) {
  int len = strlen(buf);
  char tmp[256];
  if (len - prefix_len >= (int)sizeof(tmp))
    return 0;
  memcpy(tmp, buf + prefix_len, len - prefix_len + 1);
  len = strlen(tmp);
  while (len > 0 && (tmp[len - 1] == '\n' || tmp[len - 1] == '\r'))
    tmp[--len] = '\0';
  char *val = tmp;
  if (*val == '"')
    val++;
  len = strlen(val);
  if (len > 0 && val[len - 1] == '"')
    val[--len] = '\0';
  if (len > 0 && len < maxlen) {
    memcpy(out, val, len + 1);
    return 1;
  }
  return 0;
}
#endif

// Distro detection shells out to fastfetch and /etc/os-release, neither of
// which is meaningful on Windows (the logo is always the built-in Win mark).
#if !defined(_WIN32)
static int detect_distro_fastfetch(char *out, int maxlen) {
  FILE *fp = popen("fastfetch -c none --json 2>/dev/null", "r");
  if (!fp)
    return 0;
  char buf[1024];
  int found_os = 0;
  while (fgets(buf, sizeof(buf), fp)) {
    if (strstr(buf, "\"OS\""))
      found_os = 1;
    if (found_os) {
      char *id_pos = strstr(buf, "\"id\"");
      if (id_pos) {
        char *colon = strchr(id_pos, ':');
        if (colon) {
          char *q1 = strchr(colon, '"');
          if (q1) {
            q1++;
            char *q2 = strchr(q1, '"');
            if (q2 && q2 - q1 > 0 && q2 - q1 < maxlen) {
              memcpy(out, q1, q2 - q1);
              out[q2 - q1] = '\0';
              pclose(fp);
              return 1;
            }
          }
        }
      }
      char *like_pos = strstr(buf, "\"idLike\"");
      if (like_pos) {
        char *colon = strchr(like_pos, ':');
        if (colon) {
          char *q1 = strchr(colon, '"');
          if (q1) {
            q1++;
            char *q2 = strchr(q1, '"');
            if (q2 && q2 - q1 > 0 && q2 - q1 < (int)sizeof(distro_id_like)) {
              memcpy(distro_id_like, q1, q2 - q1);
              distro_id_like[q2 - q1] = '\0';
            }
          }
        }
      }
    }
  }
  pclose(fp);
  return 0;
}

static int detect_distro_os_release(char *out, int maxlen) {
  FILE *fp = fopen("/etc/os-release", "r");
  if (!fp)
    return 0;
  char buf[256];
  int found_id = 0;
  while (fgets(buf, sizeof(buf), fp)) {
    if (!found_id && strncmp(buf, "ID=", 3) == 0) {
      found_id = parse_os_release_val(buf, 3, out, maxlen);
    } else if (strncmp(buf, "ID_LIKE=", 8) == 0) {
      parse_os_release_val(buf, 8, distro_id_like, sizeof(distro_id_like));
    }
  }
  fclose(fp);
  return found_id;
}
#endif

int logo_detect_distro(char *out, size_t maxlen) {
  if (!out || maxlen == 0) return 0;
#ifdef _WIN32
  strncpy(out, "windows", maxlen - 1);
  out[maxlen - 1] = '\0';
  return 1;
#elif defined(__APPLE__)
  if (detect_distro_fastfetch(out, maxlen))
    return 1;
  FILE *fp = popen("sw_vers -productName 2>/dev/null", "r");
  if (fp) {
    char buf[64];
    if (fgets(buf, sizeof(buf), fp)) {
      int len = strlen(buf);
      while (len > 0 && (buf[len-1]=='\n'||buf[len-1]=='\r')) buf[--len]='\0';
      if (len > 0 && len < (int)maxlen) { memcpy(out, buf, len+1); pclose(fp); return 1; }
    }
    pclose(fp);
  }
  strncpy(out, "macos", maxlen-1);
  out[maxlen-1] = '\0';
  return 1;
#else
  if (detect_distro_fastfetch(out, maxlen))
    return 1;
  return detect_distro_os_release(out, maxlen);
#endif
}

// Backward-compatibility functions
void load_default_logo(void) {
  logo_load_default(&g_logo);
  logo_sync_to_globals(&g_logo);
}

int load_logo_file(void) {
  logo_sync_from_globals(&g_logo);
  int res = logo_load_file(&g_logo, NULL);
  logo_sync_to_globals(&g_logo);
  return res;
}

int load_logo_fastfetch(const char *name) {
  logo_sync_from_globals(&g_logo);
  int res = logo_load_fastfetch(&g_logo, name);
  logo_sync_to_globals(&g_logo);
  return res;
}

void process_logo_row(int row) {
  logo_sync_from_globals(&g_logo);
  logo_process_row(&g_logo, row);
  logo_sync_to_globals(&g_logo);
}

void process_logo(void) {
  logo_sync_from_globals(&g_logo);
  logo_process(&g_logo);
  logo_sync_to_globals(&g_logo);
}

void set_distro_colors(const char *distro) {
  logo_set_distro_colors(distro, &color_outer, &color_inner);
}

int detect_distro(char *out, int maxlen) {
  return logo_detect_distro(out, maxlen);
}
