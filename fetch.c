#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>

#ifdef __APPLE__
#include <sys/sysctl.h>
#include <sys/mount.h>
#ifndef LEGACY_IOKIT
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#endif
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "src/core/common.h"
#include "src/renderer/renderer.h"
#include "src/config/config.h"
#include "src/logo/logo.h"
#include "src/platform/platform.h"

#define GAP 2

int render_height = 36;
static int logo_height = 36;        // rows the logo may span (<= render_height)
static int anim_width = ANIM_WIDTH; // canvas columns, shrinks on narrow terminals
static int layout_stacked = 0;      // info below the logo instead of beside it
static int info_clip_cols = -1;     // clip info lines to this many visible columns
static int stacked_info_rows = 0;   // info lines shown in stacked layout
static int term_cols = 0;
static int term_rows = 0;
#ifndef FETCH_VERSION
#define FETCH_VERSION "dev"
#endif

#ifdef __APPLE__
static int sysctl_str(const char *name, char *out, int maxlen) {
  size_t len = maxlen - 1;
  if (sysctlbyname(name, out, &len, NULL, 0) == 0) {
    out[len] = '\0';
    return 1;
  }
  return 0;
}

static long sysctl_long(const char *name) {
  long val = 0;
  size_t len = sizeof(val);
  if (sysctlbyname(name, &val, &len, NULL, 0) == 0)
    return val;
  return 0;
}

static int sysctl_int(const char *name) {
  int val = 0;
  size_t len = sizeof(val);
  if (sysctlbyname(name, &val, &len, NULL, 0) == 0)
    return val;
  return 0;
}
#endif




// Fastfetch output storage
#define MAX_FETCH_LINES 32
#define MAX_LINE_LEN 512
static char fetch_lines[MAX_FETCH_LINES][MAX_LINE_LEN];
static int fetch_line_count = 0;

static int field_line[F_COUNT]; // line index for each field (-1 if not shown)
static int current_field = -1;  // which field is currently being gathered
static int is_refresh_pass = 0; // 1 during the animation refresh tick

static void add_line(const char *line) {
  if (fetch_line_count >= MAX_FETCH_LINES)
    return;
  strncpy(fetch_lines[fetch_line_count], line, MAX_LINE_LEN - 1);
  fetch_lines[fetch_line_count][MAX_LINE_LEN - 1] = '\0';
  fetch_line_count++;
}

// Format a labeled info line using the configured label color.
// If the current field already has a line index, update it in place.

static int box_width = 0; // >0 once box_wrap_lines() has run

static void add_info(const char *label, const char *fmt, ...) {
  char val[MAX_LINE_LEN];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(val, sizeof(val), fmt, ap);
  va_end(ap);

  char line[MAX_LINE_LEN];
  snprintf(line, sizeof(line), "\033[1;%sm%s\033[0m: %s", label_color, label,
           val);

  // Refresh tick: replace the field's line in place. Initial pass: always
  // append a new line (so gathers that emit multiple rows, like multi-GPU,
  // don't overwrite themselves).
  if (is_refresh_pass && current_field >= 0 && field_line[current_field] >= 0) {
    int idx = field_line[current_field];
    if (box_width > 0) {
      int w = visible_width(line);
      int pad = box_width - w;
      if (pad < 0) pad = 0;
      char boxed[MAX_LINE_LEN];
      snprintf(boxed, sizeof(boxed), "\xe2\x94\x82 %s%*s \xe2\x94\x82", line,
               pad, "");
      strncpy(fetch_lines[idx], boxed, MAX_LINE_LEN - 1);
    } else {
      strncpy(fetch_lines[idx], line, MAX_LINE_LEN - 1);
    }
    fetch_lines[idx][MAX_LINE_LEN - 1] = '\0';
    return;
  }
  if (current_field >= 0)
    field_line[current_field] = fetch_line_count;
  add_line(line);
}

// Wrap the info lines (skipping the "user@host" title + separator) in a
// box, fastfetch Caelestia style. Must run once, after all initial
// gather_*() calls, and shifts field_line[] so later live-refresh writes
// (see add_info above) land on the right row.
static void box_wrap_lines(void) {
  int start = 2; // 0 = title, 1 = separator underline
  if (start >= fetch_line_count)
    return;

  int max_w = 0;
  for (int i = start; i < fetch_line_count; i++) {
    int w = visible_width(fetch_lines[i]);
    if (w > max_w)
      max_w = w;
  }
  box_width = max_w;

  char content[MAX_FETCH_LINES][MAX_LINE_LEN];
  int content_count = fetch_line_count - start;
  for (int i = 0; i < content_count; i++)
    strncpy(content[i], fetch_lines[start + i], MAX_LINE_LEN - 1);

  char border[MAX_LINE_LEN];
  char *bp = border;
  memcpy(bp, "\xe2\x95\xad", 3); bp += 3; // ╭
  for (int i = 0; i < max_w + 2; i++) { memcpy(bp, "\xe2\x94\x80", 3); bp += 3; } // ─
  memcpy(bp, "\xe2\x95\xae", 3); bp += 3; // ╮
  *bp = '\0';
  strncpy(fetch_lines[start], border, MAX_LINE_LEN - 1);

  int out = start + 1;
  for (int i = 0; i < content_count && out < MAX_FETCH_LINES; i++, out++) {
    int w = visible_width(content[i]);
    int pad = max_w - w;
    if (pad < 0) pad = 0;
    snprintf(fetch_lines[out], MAX_LINE_LEN, "\xe2\x94\x82 %s%*s \xe2\x94\x82",
             content[i], pad, "");
  }

  bp = border;
  memcpy(bp, "\xe2\x95\xb0", 3); bp += 3; // ╰
  for (int i = 0; i < max_w + 2; i++) { memcpy(bp, "\xe2\x94\x80", 3); bp += 3; } // ─
  memcpy(bp, "\xe2\x95\xaf", 3); bp += 3; // ╯
  *bp = '\0';
  if (out < MAX_FETCH_LINES)
    strncpy(fetch_lines[out++], border, MAX_LINE_LEN - 1);

  fetch_line_count = out;

  for (int i = 0; i < F_COUNT; i++)
    if (field_line[i] >= start)
      field_line[i] += 1;
}

// Runs `<bin> --version` and extracts the first version-looking token
// (starting at the first digit). Same extraction strategy as gather_shell().
static void get_cmd_version(const char *bin, char *out, int outlen) {
  out[0] = '\0';
  if (!bin || !bin[0])
    return;
  for (const char *p = bin; *p; p++) {
    char c = *p;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '/' ||
          c == '_' || c == '-'))
      return;
  }
  char cmd[300];
  snprintf(cmd, sizeof(cmd), "%s --version 2>/dev/null", bin);
  FILE *fp = popen(cmd, "r");
  if (!fp)
    return;
  char buf[256];
  if (fgets(buf, sizeof(buf), fp)) {
    char *ver = buf;
    while (*ver && !(*ver >= '0' && *ver <= '9'))
      ver++;
    if (*ver) {
      int len = 0;
      while (ver[len] && ver[len] != ' ' && ver[len] != '(' &&
             ver[len] != '\n' && len < 30)
        len++;
      memcpy(out, ver, len);
      out[len] = '\0';
    }
  }
  pclose(fp);
}

static void gather_title(void) {
  char user[64] = "";
  char host[64] = "";
#ifdef _WIN32
  platform_gather_title(user, sizeof(user), host, sizeof(host));
#else
  char *login = getlogin();
  if (login)
    strncpy(user, login, sizeof(user) - 1);
  else {
    char *env = getenv("USER");
    if (env)
      strncpy(user, env, sizeof(user) - 1);
  }
  gethostname(host, sizeof(host));
#endif

  char line[MAX_LINE_LEN];
  snprintf(line, sizeof(line), "\033[1;%sm%s\033[0m@\033[1;%sm%s\033[0m",
           label_color, user, label_color, host);
  add_line(line);

  // separator
  int title_len = strlen(user) + 1 + strlen(host);
  char sep[MAX_LINE_LEN];
  int sep_char_len = strlen(config_separator);
  if (sep_char_len == 0)
    sep_char_len = 1;
  int pos = 0;
  for (int i = 0; i < title_len && pos + sep_char_len < MAX_LINE_LEN; i++) {
    memcpy(sep + pos, config_separator, sep_char_len);
    pos += sep_char_len;
  }
  sep[pos] = '\0';
  add_line(sep);
}

static void gather_os(void) {
#ifdef _WIN32
  char os[256] = "";
  platform_gather_os(os, sizeof(os));
  if (os[0])
    add_info("OS", "%s", os);
#elif defined(__APPLE__)
  char version[64] = "";
  if (sysctl_str("kern.osproductversion", version, sizeof(version))) {
    struct utsname u;
    uname(&u);
    add_info("OS", "macOS %s %s", version, u.machine);
  } else {
    struct utsname u;
    uname(&u);
    add_info("OS", "%s %s", u.sysname, u.machine);
  }
#else
  char pretty[128] = "";
  FILE *fp = fopen("/etc/os-release", "r");
  if (fp) {
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
      if (strncmp(buf, "PRETTY_NAME=", 12) == 0) {
        char *val = buf + 12;
        int len = strlen(val);
        while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r'))
          val[--len] = '\0';
        if (*val == '\'' || *val == '"')
          val++;
        len = strlen(val);
        if (len > 0 && (val[len - 1] == '\'' || val[len - 1] == '"'))
          val[--len] = '\0';
        strncpy(pretty, val, sizeof(pretty) - 1);
        break;
      }
    }
    fclose(fp);
  }
  if (!pretty[0])
    strcpy(pretty, "Linux");

  struct utsname u;
  uname(&u);

  add_info("OS", "%s %s", pretty, u.machine);
#endif
}

// Reads the first line of a small pseudo-file (e.g. under /sys or /proc)
// into out, trimming the trailing newline. Returns 1 on success, 0 if the
// file couldn't be opened or was empty.
static int try_read_first_line(const char *path, char *out, int outlen) {
  out[0] = '\0';
  FILE *fp = fopen(path, "r");
  if (!fp)
    return 0;
  if (fgets(out, outlen, fp)) {
    int len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r'))
      out[--len] = '\0';
  }
  fclose(fp);
  return out[0] != '\0';
}

static void gather_host(void) {
#ifdef _WIN32
  char host[256] = "";
  platform_gather_host(host, sizeof(host));
  if (host[0])
    add_info("Host", "%s", host);
#elif defined(__APPLE__)
  char model[128] = "";
  if (sysctl_str("hw.model", model, sizeof(model)))
    add_info("Host", "%s", model);
#else
  char model[128] = "";
  char vendor[64] = "";

  // Try device-tree first (ARM/Apple Silicon), then DMI (x86)
  if (!try_read_first_line("/proc/device-tree/model", model, sizeof(model))) {
    if (try_read_first_line("/sys/class/dmi/id/product_name", model, sizeof(model)))
      try_read_first_line("/sys/class/dmi/id/sys_vendor", vendor, sizeof(vendor));
  }

  // Some boards already embed the vendor name in product_name
  // don't duplicate it if so.
  if (vendor[0] && strncasecmp(model, vendor, strlen(vendor)) == 0)
    vendor[0] = '\0';

  if (model[0]) {
    if (vendor[0])
      add_info("Host", "%s %s", vendor, model);
    else
      add_info("Host", "%s", model);
  }
#endif
}

static void gather_kernel(void) {
#ifdef _WIN32
  char kernel[128] = "";
  platform_gather_kernel(kernel, sizeof(kernel));
  if (kernel[0])
    add_info("Kernel", "%s", kernel);
#else
  struct utsname u;
  uname(&u);
  add_info("Kernel", "%s %s", u.sysname, u.release);
#endif
}

static void gather_uptime(void) {
#ifdef _WIN32
  char uptime[128] = "";
  platform_gather_uptime(uptime, sizeof(uptime));
  if (uptime[0])
    add_info("Uptime", "%s", uptime);
  return;
#elif defined(__APPLE__)
  struct timeval bt;
  size_t len = sizeof(bt);
  if (sysctlbyname("kern.boottime", &bt, &len, NULL, 0) != 0)
    return;
  double secs = difftime(time(NULL), bt.tv_sec);
#else
  FILE *fp = fopen("/proc/uptime", "r");
  if (!fp)
    return;
  double secs = 0;
  if (fscanf(fp, "%lf", &secs) != 1)
    secs = 0;
  fclose(fp);

  int total = (int)secs;
  int days = total / 86400;
  int hours = (total % 86400) / 3600;
  int mins = (total % 3600) / 60;

  char val[128];
  if (days > 0)
    snprintf(val, sizeof(val), "%d day%s, %d hour%s, %d min%s", days,
             days == 1 ? "" : "s", hours, hours == 1 ? "" : "s", mins,
             mins == 1 ? "" : "s");
  else if (hours > 0)
    snprintf(val, sizeof(val), "%d hour%s, %d min%s", hours,
             hours == 1 ? "" : "s", mins, mins == 1 ? "" : "s");
  else
    snprintf(val, sizeof(val), "%d min%s", mins, mins == 1 ? "" : "s");

  add_info("Uptime", "%s", val);
#endif
}

// Count subdirectories in a path (non-recursive)
static int count_subdirs(const char *path) {
  DIR *d = opendir(path);
  if (!d)
    return 0;
  int count = 0;
  struct dirent *ent;
  while ((ent = readdir(d))) {
    if (ent->d_name[0] == '.')
      continue;
    count++;
  }
  closedir(d);
  return count;
}

// Count packages in emerge-style two-level dir (category/package)
static int count_emerge_pkgs(void) {
  DIR *d = opendir("/var/db/pkg");
  if (!d)
    return 0;
  int count = 0;
  struct dirent *cat;
  while ((cat = readdir(d))) {
    if (cat->d_name[0] == '.')
      continue;
    char path[256];
    snprintf(path, sizeof(path), "/var/db/pkg/%s", cat->d_name);
    count += count_subdirs(path);
  }
  closedir(d);
  return count;
}

// Count lines in a file (for dpkg status, apk installed)
static int count_file_lines(const char *path, const char *prefix) {
  FILE *fp = fopen(path, "r");
  if (!fp)
    return 0;
  int count = 0;
  char buf[512];
  int plen = prefix ? strlen(prefix) : 0;
  while (fgets(buf, sizeof(buf), fp)) {
    if (!prefix || strncmp(buf, prefix, plen) == 0)
      count++;
  }
  fclose(fp);
  return count;
}

static void gather_packages(void) {
#ifdef _WIN32
  char val_win[128] = "";
  platform_gather_packages(val_win, sizeof(val_win));
  if (val_win[0])
    add_info("Packages", "%s", val_win);
  return;
#endif
  char val[128] = "";
  int n;

#ifdef __APPLE__
  // Homebrew — check both ARM and Intel Cellar paths
  const char *cellars[] = {"/opt/homebrew/Cellar", "/usr/local/Cellar", NULL};
  int total = 0;
  for (int i = 0; cellars[i]; i++) {
    DIR *d = opendir(cellars[i]);
    if (!d) continue;
    struct dirent *ent;
    while ((ent = readdir(d))) {
      if (ent->d_name[0] == '.') continue;
      total++;
    }
    closedir(d);
  }
  if (total > 0)
    snprintf(val, sizeof(val), "%d (brew)", total);
#else
  // emerge (Gentoo)
  n = count_emerge_pkgs();
  if (n > 0)
    snprintf(val, sizeof(val), "%d (emerge)", n);
  // pacman (Arch)
  if (!val[0]) {
    n = count_subdirs("/var/lib/pacman/local");
    if (n > 1) // -1 for ALPM_DB_VERSION
      snprintf(val, sizeof(val), "%d (pacman)", n - 1);
  }
  // dpkg (Debian/Ubuntu)
  if (!val[0]) {
    n = count_file_lines("/var/lib/dpkg/status", "Package:");
    if (n > 0)
      snprintf(val, sizeof(val), "%d (dpkg)", n);
  }
  // rpm (Fedora, RHEL, openSUSE, etc.)
  if (!val[0]) {
    FILE *fp = popen("rpm -qa 2>/dev/null", "r");
    if (fp) {
      n = 0;
      char buf[256];
      while (fgets(buf, sizeof(buf), fp))
        n++;
      pclose(fp);
      if (n > 0)
        snprintf(val, sizeof(val), "%d (rpm)", n);
    }
  }
  // xbps (Void)
  if (!val[0]) {
    FILE *fp = popen("xbps-query -l 2>/dev/null | wc -l", "r");
    if (fp) {
      if (fscanf(fp, "%d", &n) == 1 && n > 0)
        snprintf(val, sizeof(val), "%d (xbps)", n);
      pclose(fp);
    }
  }
  // apk (Alpine)
  if (!val[0]) {
    n = count_file_lines("/lib/apk/db/installed", "P:");
    if (n > 0)
      snprintf(val, sizeof(val), "%d (apk)", n);
  }
#endif
  
  // Nix (NixOS, or Nix package manager on other distros).
  {
    const char *home = getenv("HOME");
    char base[128] = "";
    if (home) {
      const char *b = strrchr(home, '/');
      b = b ? b + 1 : home;
      strncpy(base, b, sizeof(base) - 1);
    }

    // Only allow a username charset before it's interpolated
    // into a shell command, reject anything else rather than trying to
    // escape it.
    int base_valid = base[0] != '\0';
    for (int i = 0; base_valid && base[i]; i++) {
      char c = base[i];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
        base_valid = 0;
    }
    if (!base_valid)
      base[0] = '\0';

    char cmd[700];
    if (base[0]) {
      snprintf(cmd, sizeof(cmd),
        "sh -c '"
        "sys=$(nix-store -q --references /run/current-system/sw 2>/dev/null | wc -l); "
        "resolved=$(readlink -f /etc/profiles/per-user/%s 2>/dev/null); "
        "if [ -n \"$resolved\" ]; then "
        "  hop1=$(nix-store -q --references \"$resolved\" 2>/dev/null); "
        "  if [ -n \"$hop1\" ]; then "
        "    usr=$(nix-store -q --references \"$hop1\" 2>/dev/null | wc -l); "
        "  else "
        "    usr=$(nix-store -q --references \"$resolved\" 2>/dev/null | wc -l); "
        "  fi; "
        "else "
        "  usr=0; "
        "fi; "
        "echo \"$sys $usr\"'",
        base);
    } else {
      snprintf(cmd, sizeof(cmd),
        "sh -c '"
        "sys=$(nix-store -q --references /run/current-system/sw 2>/dev/null | wc -l); "
        "echo \"$sys 0\"'");
    }

    int nix_system = 0, nix_user = 0;
    FILE *fp = popen(cmd, "r");
    if (fp) {
      if (fscanf(fp, "%d %d", &nix_system, &nix_user) != 2) {
        nix_system = 0;
        nix_user = 0;
      }
      pclose(fp);
    }

    char nix_val[64] = "";
    if (nix_system > 0 && nix_user > 0)
      snprintf(nix_val, sizeof(nix_val), "%d (system), %d (user)", nix_system, nix_user);
    else if (nix_system > 0)
      snprintf(nix_val, sizeof(nix_val), "%d (system)", nix_system);
    else if (nix_user > 0)
      snprintf(nix_val, sizeof(nix_val), "%d (user)", nix_user);

    if (nix_val[0]) {
      if (val[0])
        snprintf(val + strlen(val), sizeof(val) - strlen(val), ", %s", nix_val);
      else
        snprintf(val, sizeof(val), "%s", nix_val);
    }
  }

  n = 0;
  FILE *flatpak_fp = popen("flatpak list --columns=ref 2>/dev/null", "r");
  if (flatpak_fp) {
    char buf[256];
    while (fgets(buf, sizeof(buf), flatpak_fp))
      n++;
    pclose(flatpak_fp);
  }
  if (n > 0) {
    char flatpak_val[32];
    snprintf(flatpak_val, sizeof(flatpak_val), "%d (flatpak)", n);
    if (val[0])
      snprintf(val + strlen(val), sizeof(val) - strlen(val), ", %s", flatpak_val);
    else
      snprintf(val, sizeof(val), "%s", flatpak_val);
  }

  if (val[0])
    add_info("Packages", "%s", val);
}

static void gather_shell(void) {
#ifdef _WIN32
  char shell_win[128] = "";
  platform_gather_shell(shell_win, sizeof(shell_win));
  if (shell_win[0])
    add_info("Shell", "%s", shell_win);
  return;
#endif
  char *shell = NULL;
  char shell_buf[64] = "";

  // Try to detect the actual running shell from parent process,
  // since $SHELL is the login shell which may differ (e.g. fish launched
  // from bash)
  {
#ifdef __APPLE__
    struct kinfo_proc kp;
    size_t klen = sizeof(kp);
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getppid()};
    if (sysctl(mib, 4, &kp, &klen, NULL, 0) == 0) {
      strncpy(shell_buf, kp.kp_proc.p_comm, sizeof(shell_buf) - 1);
      shell_buf[sizeof(shell_buf) - 1] = '\0';
      if (strcmp(shell_buf, "bash") == 0 || strcmp(shell_buf, "zsh") == 0 ||
          strcmp(shell_buf, "fish") == 0 || strcmp(shell_buf, "dash") == 0 ||
          strcmp(shell_buf, "sh") == 0 || strcmp(shell_buf, "nu") == 0 ||
          strcmp(shell_buf, "elvish") == 0 || strcmp(shell_buf, "tcsh") == 0 ||
          strcmp(shell_buf, "csh") == 0 || strcmp(shell_buf, "xonsh") == 0 ||
          strcmp(shell_buf, "ksh") == 0 || strcmp(shell_buf, "ion") == 0)
        shell = shell_buf;
    }
#else
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/comm", getppid());
    FILE *fp = fopen(path, "r");
    if (fp) {
      if (fgets(shell_buf, sizeof(shell_buf), fp)) {
        int len = strlen(shell_buf);
        while (len > 0 && (shell_buf[len - 1] == '\n' || shell_buf[len - 1] == '\r'))
          shell_buf[--len] = '\0';
        // Only use if it looks like a shell
        if (strcmp(shell_buf, "bash") == 0 || strcmp(shell_buf, "zsh") == 0 ||
            strcmp(shell_buf, "fish") == 0 || strcmp(shell_buf, "dash") == 0 ||
            strcmp(shell_buf, "sh") == 0 || strcmp(shell_buf, "nu") == 0 ||
            strcmp(shell_buf, "elvish") == 0 || strcmp(shell_buf, "tcsh") == 0 ||
            strcmp(shell_buf, "csh") == 0 || strcmp(shell_buf, "xonsh") == 0 ||
            strcmp(shell_buf, "ksh") == 0 || strcmp(shell_buf, "ion") == 0)
          shell = shell_buf;
      }
      fclose(fp);
    }
#endif
  }

  // Fall back to $SHELL (login shell)
  if (!shell)
    shell = getenv("SHELL");
  if (!shell)
    return;

  // Get just the basename
  char *name = strrchr(shell, '/');
  name = name ? name + 1 : shell;

  char version[64] = "";
  get_cmd_version(shell, version, sizeof(version));

  if (version[0])
    add_info("Shell", "%s %s", name, version);
  else
    add_info("Shell", "%s", name);
}

#ifndef __APPLE__
struct drm_modeinfo_min {
  uint32_t clock;
  uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
  uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
  uint32_t vrefresh;
  uint32_t flags;
  uint32_t type;
  char name[32];
};

struct drm_crtc_min {
  uint64_t set_connectors_ptr;
  uint32_t count_connectors;
  uint32_t crtc_id;
  uint32_t fb_id;
  uint32_t x, y;
  uint32_t gamma_size;
  uint32_t mode_valid;
  struct drm_modeinfo_min mode;
};

struct drm_encoder_min {
  uint32_t encoder_id;
  uint32_t encoder_type;
  uint32_t crtc_id;
  uint32_t possible_crtcs;
  uint32_t possible_clones;
};

struct drm_connector_min {
  uint64_t encoders_ptr, modes_ptr, props_ptr, prop_values_ptr;
  uint32_t count_modes, count_props, count_encoders;
  uint32_t encoder_id, connector_id;
  uint32_t connector_type, connector_type_id;
  uint32_t connection;
  uint32_t mm_width, mm_height;
  uint32_t subpixel;
  uint32_t pad;
};

#define DRM_MIN_GETCRTC _IOWR('d', 0xA1, struct drm_crtc_min)
#define DRM_MIN_GETENCODER _IOWR('d', 0xA6, struct drm_encoder_min)
#define DRM_MIN_GETCONNECTOR _IOWR('d', 0xA7, struct drm_connector_min)

static int drm_active_mode(const char *card, uint32_t conn_id, int *w, int *h,
                           float *hz, int *mm_w, int *mm_h) {
  char dev[64];
  snprintf(dev, sizeof(dev), "/dev/dri/%s", card);
  int fd = open(dev, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return 0;

  struct drm_connector_min conn;
  memset(&conn, 0, sizeof(conn));
  conn.connector_id = conn_id;
  struct drm_modeinfo_min scratch;
  conn.modes_ptr = (uint64_t)(uintptr_t)&scratch;
  conn.count_modes = 1;
  int got = 0;
  if (ioctl(fd, DRM_MIN_GETCONNECTOR, &conn) == 0) {
    if (conn.mm_width && conn.mm_height) {
      *mm_w = (int)conn.mm_width;
      *mm_h = (int)conn.mm_height;
    }
    struct drm_encoder_min enc;
    memset(&enc, 0, sizeof(enc));
    enc.encoder_id = conn.encoder_id;
    if (conn.encoder_id && ioctl(fd, DRM_MIN_GETENCODER, &enc) == 0 &&
        enc.crtc_id) {
      struct drm_crtc_min crtc;
      memset(&crtc, 0, sizeof(crtc));
      crtc.crtc_id = enc.crtc_id;
      if (ioctl(fd, DRM_MIN_GETCRTC, &crtc) == 0 && crtc.mode_valid &&
          crtc.mode.htotal && crtc.mode.vtotal) {
        *w = crtc.mode.hdisplay;
        *h = crtc.mode.vdisplay;
        float r = crtc.mode.clock * 1000.0f /
                  ((float)crtc.mode.htotal * (float)crtc.mode.vtotal);
        if (crtc.mode.flags & 0x10)
          r *= 2.0f;
        if (crtc.mode.flags & 0x20)
          r /= 2.0f;
        if (crtc.mode.vscan > 1)
          r /= (float)crtc.mode.vscan;
        *hz = r;
        got = 1;
      }
    }
  }
  close(fd);
  return got;
}

static int read_edid(const char *conn_dir, unsigned char *edid, size_t max) {
  char path[320];
  snprintf(path, sizeof(path), "/sys/class/drm/%s/edid", conn_dir);
  FILE *fp = fopen(path, "rb");
  if (!fp)
    return 0;
  size_t n = fread(edid, 1, max, fp);
  fclose(fp);
  if (n < 128)
    return 0;
  static const unsigned char hdr[8] = {0x00, 0xFF, 0xFF, 0xFF,
                                       0xFF, 0xFF, 0xFF, 0x00};
  return memcmp(edid, hdr, 8) == 0;
}

static void edid_name(const unsigned char *e, char *out, size_t outsz) {
  out[0] = '\0';
  for (int i = 54; i <= 108; i += 18) {
    const unsigned char *d = e + i;
    if (d[0] || d[1] || d[2] || d[3] != 0xFC)
      continue;
    size_t n = 0;
    for (int j = 5; j < 18 && n + 1 < outsz; j++) {
      if (d[j] == 0x0A)
        break;
      out[n++] = (char)d[j];
    }
    while (n > 0 && out[n - 1] == ' ')
      n--;
    out[n] = '\0';
    if (out[0])
      return;
  }
  unsigned v = ((unsigned)e[8] << 8) | e[9];
  char m[4] = {(char)('A' + ((v >> 10) & 0x1F) - 1),
               (char)('A' + ((v >> 5) & 0x1F) - 1),
               (char)('A' + (v & 0x1F) - 1), '\0'};
  for (int i = 0; i < 3; i++)
    if (m[i] < 'A' || m[i] > 'Z')
      return;
  snprintf(out, outsz, "%s%02X%02X", m, e[11], e[10]);
}

static void edid_size_mm(const unsigned char *e, int *w, int *h) {
  const unsigned char *d = e + 54;
  if (d[0] || d[1]) {
    int mw = d[12] | ((d[14] & 0xF0) << 4);
    int mh = d[13] | ((d[14] & 0x0F) << 8);
    if (mw > 0 && mh > 0) {
      *w = mw;
      *h = mh;
      return;
    }
  }
  if (e[21] && e[22]) {
    *w = e[21] * 10;
    *h = e[22] * 10;
  }
}

static float edid_refresh(const unsigned char *e) {
  const unsigned char *d = e + 54;
  unsigned clock = d[0] | ((unsigned)d[1] << 8);
  if (!clock)
    return 0;
  unsigned htotal = (d[2] | ((d[4] & 0xF0) << 4)) + (d[3] | ((d[4] & 0x0F) << 8));
  unsigned vtotal = (d[5] | ((d[7] & 0xF0) << 4)) + (d[6] | ((d[7] & 0x0F) << 8));
  if (!htotal || !vtotal)
    return 0;
  return clock * 10000.0f / (float)(htotal * vtotal);
}

static void format_hz(float hz, char *out, size_t outsz) {
  if (hz <= 0.0f) {
    out[0] = '\0';
    return;
  }
  if (fabsf(hz - roundf(hz)) < 0.05f)
    snprintf(out, outsz, "%.0f Hz", hz);
  else
    snprintf(out, outsz, "%.2f Hz", hz);
}

static int connector_is_internal(const char *conn) {
  return strncmp(conn, "eDP", 3) == 0 || strncmp(conn, "LVDS", 4) == 0 ||
         strncmp(conn, "DSI", 3) == 0;
}
#endif

static void gather_display(void) {
#ifdef _WIN32
  platform_gather_display(add_info);
  return;
#elif defined(__APPLE__)
  FILE *fp = popen("system_profiler SPDisplaysDataType 2>/dev/null", "r");
  if (!fp) return;
  char buf[512];
  char name[64] = "", res[64] = "";
  while (fgets(buf, sizeof(buf), fp)) {
    if (!name[0]) {
      char *p = strstr(buf, "Chipset Model:");
      if (!p) p = strstr(buf, "Chip:");
      if (p) {
        p = strchr(p, ':');
        if (p) {
          p++;
          while (*p == ' ') p++;
          int len = strlen(p);
          while (len > 0 && (p[len-1]=='\n'||p[len-1]=='\r')) p[--len]='\0';
          if (len > 0 && len < (int)sizeof(name)) memcpy(name, p, len+1);
        }
      }
    }
    if (!res[0]) {
      char *p = strstr(buf, "Resolution:");
      if (p) {
        p = strchr(p, ':');
        if (p) {
          p++;
          while (*p == ' ') p++;
          int len = strlen(p);
          while (len > 0 && (p[len-1]=='\n'||p[len-1]=='\r')) p[--len]='\0';
          if (len > 0 && len < (int)sizeof(res)) memcpy(res, p, len+1);
        }
      }
    }
  }
  pclose(fp);
  if (name[0] && res[0])
    add_info("Display", "%s %s", name, res);
  else if (res[0])
    add_info("Display", "%s", res);
#else
  DIR *d = opendir("/sys/class/drm");
  if (!d)
    return;
  struct dirent *ent;
  int emitted = 0;
  while ((ent = readdir(d))) {
    // Pattern: cardN-CONNECTOR (skip bare cardN and renderD*)
    if (strncmp(ent->d_name, "card", 4) != 0)
      continue;
    const char *dash = strchr(ent->d_name + 4, '-');
    if (!dash)
      continue;

    char path[320];
    snprintf(path, sizeof(path), "/sys/class/drm/%s/status", ent->d_name);
    FILE *fp = fopen(path, "r");
    if (!fp)
      continue;
    char status[32] = "";
    if (fgets(status, sizeof(status), fp)) {
      int l = strlen(status);
      while (l > 0 && (status[l - 1] == '\n' || status[l - 1] == '\r'))
        status[--l] = '\0';
    }
    fclose(fp);
    if (strcmp(status, "connected") != 0)
      continue;

    snprintf(path, sizeof(path), "/sys/class/drm/%s/modes", ent->d_name);
    fp = fopen(path, "r");
    if (!fp)
      continue;
    char mode[32] = "";
    if (fgets(mode, sizeof(mode), fp)) {
      int l = strlen(mode);
      while (l > 0 && (mode[l - 1] == '\n' || mode[l - 1] == '\r'))
        mode[--l] = '\0';
    }
    fclose(fp);
    if (!mode[0])
      continue;

    const char *conn = dash + 1;
    char card[32];
    size_t card_len = (size_t)(dash - ent->d_name);
    if (card_len >= sizeof(card))
      continue;
    memcpy(card, ent->d_name, card_len);
    card[card_len] = '\0';

    unsigned char edid[256];
    int have_edid = read_edid(ent->d_name, edid, sizeof(edid));

    int w = 0, h = 0, mm_w = 0, mm_h = 0;
    float hz = 0.0f;
    unsigned conn_id = 0;
    snprintf(path, sizeof(path), "/sys/class/drm/%s/connector_id",
             ent->d_name);
    fp = fopen(path, "r");
    if (fp) {
      if (fscanf(fp, "%u", &conn_id) != 1)
        conn_id = 0;
      fclose(fp);
    }
    if (conn_id)
      drm_active_mode(card, conn_id, &w, &h, &hz, &mm_w, &mm_h);

    if (hz <= 0.0f && have_edid)
      hz = edid_refresh(edid);
    if ((!mm_w || !mm_h) && have_edid)
      edid_size_mm(edid, &mm_w, &mm_h);

    char res[32];
    if (w > 0 && h > 0)
      snprintf(res, sizeof(res), "%dx%d", w, h);
    else
      snprintf(res, sizeof(res), "%s", mode);

    char label[80];
    char name[32] = "";
    if (have_edid)
      edid_name(edid, name, sizeof(name));
    snprintf(label, sizeof(label), "Display (%s)", name[0] ? name : conn);

    char inches[16] = "";
    if (mm_w > 0 && mm_h > 0) {
      int diag = (int)lroundf(
          sqrtf((float)(mm_w * mm_w + mm_h * mm_h)) / 25.4f);
      if (diag > 0)
        snprintf(inches, sizeof(inches), " in %d\"", diag);
    }
    char rate[32];
    format_hz(hz, rate, sizeof(rate));
    const char *sep = rate[0] ? (inches[0] ? ", " : " @ ") : "";

    add_info(label, "%s%s%s%s [%s]", res, inches, sep, rate,
             connector_is_internal(conn) ? "Built-in" : "External");
    emitted++;
  }
  closedir(d);

  // Fallback: read first mode from any card
  if (!emitted) {
    d = opendir("/sys/class/drm");
    if (!d) return;
    while ((ent = readdir(d))) {
      if (strncmp(ent->d_name, "card", 4) != 0)
        continue;
      char path[320];
      snprintf(path, sizeof(path), "/sys/class/drm/%s/modes", ent->d_name);
      FILE *fp = fopen(path, "r");
      if (!fp)
        continue;
      char mode[32] = "";
      if (fgets(mode, sizeof(mode), fp)) {
        int l = strlen(mode);
        while (l > 0 && (mode[l - 1] == '\n' || mode[l - 1] == '\r'))
          mode[--l] = '\0';
      }
      fclose(fp);
      if (mode[0]) {
        add_info("Display", "%s", mode);
        break;
      }
    }
    closedir(d);
  }
#endif
}

static void gather_wm(void) {
#ifdef _WIN32
  char wm_win[64] = "";
  platform_gather_wm(wm_win, sizeof(wm_win));
  if (wm_win[0])
    add_info("WM", "%s", wm_win);
  return;
#elif defined(__APPLE__)
  add_info("WM", "Aqua");
#else
  // Check WAYLAND_DISPLAY or XDG_SESSION_TYPE to determine session type
  char *wayland = getenv("WAYLAND_DISPLAY");
  char *session = getenv("XDG_SESSION_TYPE");
  char *desktop = getenv("XDG_CURRENT_DESKTOP");
  int is_wayland =
      (wayland && wayland[0]) || (session && strcmp(session, "wayland") == 0);

  // Try to figure out the WM name. Process detection is most accurate
  // (e.g. dwl sets XDG_CURRENT_DESKTOP=sway for compat), so try that first.
  char wm[64] = "";
  int wm_is_binary = 0; // true only if wm[] holds an actual invocable binary name

  // 1. Check env vars for specific WMs
  char *hyprland = getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (hyprland) {
    strcpy(wm, "Hyprland");
    wm_is_binary = 1;
  }

  // 2. Try process list by scanning /proc/*/comm
  if (!wm[0]) {
    static const char *known_wms[] = {"dwl",   "sway",    "river", "labwc",
                                      "weston", "i3",      "bspwm", "openbox",
                                      "awesome", "dwm",    NULL};
    DIR *proc = opendir("/proc");
    if (proc) {
      struct dirent *ent;
      while ((ent = readdir(proc)) && !wm[0]) {
        if (ent->d_name[0] < '1' || ent->d_name[0] > '9')
          continue;
        char path[64];
        snprintf(path, sizeof(path), "/proc/%s/comm", ent->d_name);
        FILE *fp = fopen(path, "r");
        if (!fp)
          continue;
        char comm[64] = "";
        if (fgets(comm, sizeof(comm), fp)) {
          int len = strlen(comm);
          while (len > 0 && (comm[len - 1] == '\n' || comm[len - 1] == '\r'))
            comm[--len] = '\0';
          for (int i = 0; known_wms[i]; i++) {
            if (strcmp(comm, known_wms[i]) == 0) {
              strncpy(wm, comm, sizeof(wm) - 1);
              wm_is_binary = 1;
              break;
            }
          }
        }
        fclose(fp);
      }
      closedir(proc);
    }
  }

  // 3. Fall back to DE-to-WM mapping from XDG_CURRENT_DESKTOP
  if (!wm[0] && desktop && desktop[0]) {
    char first[32];
    int n = 0;
    while (desktop[n] && desktop[n] != ':' && n < (int)sizeof(first) - 1) {
      first[n] = desktop[n];
      n++;
    }
    first[n] = '\0';
    if (strcasecmp(first, "KDE") == 0)
      strncpy(wm, "KWin", sizeof(wm) - 1);
    else if (strcasecmp(first, "GNOME") == 0)
      strncpy(wm, "Mutter", sizeof(wm) - 1);
    else if (strcasecmp(first, "XFCE") == 0)
      strncpy(wm, "xfwm4", sizeof(wm) - 1);
    else if (strcasecmp(first, "Cinnamon") == 0)
      strncpy(wm, "Muffin", sizeof(wm) - 1);
    else if (strcasecmp(first, "MATE") == 0)
      strncpy(wm, "Marco", sizeof(wm) - 1);
    else if (strcasecmp(first, "LXQt") == 0)
      strncpy(wm, "Openbox", sizeof(wm) - 1);
    else if (strcasecmp(first, "Budgie") == 0)
      strncpy(wm, "Mutter", sizeof(wm) - 1);
    else if (strcasecmp(first, "Deepin") == 0)
      strncpy(wm, "KWin", sizeof(wm) - 1);
    else {
      // Not a known DE name — likely the WM's own name (e.g. niri),
      // which is also a real invocable binary.
      strncpy(wm, desktop, sizeof(wm) - 1);
      wm_is_binary = 1;
    }
  }

  if (wm[0]) {
    char version[64] = "";
    if (wm_is_binary)
      get_cmd_version(wm, version, sizeof(version));
    if (version[0])
      add_info("WM", "%s %s%s", wm, version, is_wayland ? " (Wayland)" : "");
    else
      add_info("WM", "%s%s", wm, is_wayland ? " (Wayland)" : "");
  }
#endif
}

static void gather_displaymanager(void) {
#ifdef _WIN32
  char dm_win[64] = "";
  platform_gather_displaymanager(dm_win, sizeof(dm_win));
  if (dm_win[0])
    add_info("Display Manager", "%s", dm_win);
  return;
#elif !defined(__APPLE__)
  static const char *known_dms[] = {"sddm",    "gdm",   "gdm3", "lightdm",
                                    "lxdm",    "greetd", "xdm",  "slim",
                                    "lemurs",  "ly",    NULL};
  static const struct {
    const char *id;
    const char *label;
  } labels[] = {
      {"sddm", "SDDM"},       {"gdm", "GDM"},         {"gdm3", "GDM"},
      {"lightdm", "LightDM"}, {"lxdm", "LXDM"},       {"greetd", "greetd"},
      {"xdm", "XDM"},         {"slim", "SLiM"},       {"lemurs", "lemurs"},
      {"ly", "ly"},           {NULL, NULL},
  };
  const char *matched_id = NULL;

  // Scan running processes, portable across init systems (works on
  // Gentoo/OpenRC, runit, s6, etc.), and reflects what's running
  // right now rather than what's just enabled.
  DIR *proc = opendir("/proc");
  if (proc) {
    struct dirent *ent;
    while ((ent = readdir(proc)) && !matched_id) {
      if (ent->d_name[0] < '1' || ent->d_name[0] > '9')
        continue;
      char path[64];
      snprintf(path, sizeof(path), "/proc/%s/comm", ent->d_name);
      FILE *fp = fopen(path, "r");
      if (!fp)
        continue;
      char comm[64] = "";
      if (fgets(comm, sizeof(comm), fp)) {
        int len = strlen(comm);
        while (len > 0 && (comm[len - 1] == '\n' || comm[len - 1] == '\r'))
          comm[--len] = '\0';
        for (int i = 0; known_dms[i]; i++) {
          if (strcmp(comm, known_dms[i]) == 0) {
            matched_id = known_dms[i];
            break;
          }
        }
      }
      fclose(fp);
    }
    closedir(proc);
  }

  if (matched_id) {
    for (int i = 0; labels[i].id; i++) {
      if (strcmp(labels[i].id, matched_id) == 0) {
        add_info("Display Manager", "%s", labels[i].label);
        break;
      }
    }
  }
#endif
}

static void gather_cpu(void) {
#ifdef _WIN32
  char cpu_win[256] = "";
  platform_gather_cpu(cpu_win, sizeof(cpu_win));
  if (cpu_win[0])
    add_info("CPU", "%s", cpu_win);
  return;
#elif defined(__APPLE__)
  char name[128] = "";
  if (sysctl_str("machdep.cpu.brand_string", name, sizeof(name))) {
    int cores = sysctl_int("hw.ncpu");
    long freq_hz = sysctl_long("hw.cpufrequency_max");
    if (freq_hz > 0) {
      float ghz = freq_hz / 1e9f;
      add_info("CPU", "%s (%d) @ %.2f GHz", name, cores, ghz);
    } else if (cores > 0) {
      add_info("CPU", "%s (%d)", name, cores);
    } else {
      add_info("CPU", "%s", name);
    }
  }
#else
  char name[128] = "";
  int cores = 0;
  float max_ghz = 0;

  // Try x86 model name first
  FILE *fp = fopen("/proc/cpuinfo", "r");
  if (fp) {
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
      if (!name[0] && strncmp(buf, "model name", 10) == 0) {
        char *val = strchr(buf, ':');
        if (val) {
          val++;
          while (*val == ' ')
            val++;
          int len = strlen(val);
          while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r'))
            len--;
          if (len > 0 && len < (int)sizeof(name)) {
            memcpy(name, val, len);
            name[len] = '\0';
            char *at = strstr(name, " @ ");
            if (at)
              *at = '\0';
          }
        }
      }
      if (strncmp(buf, "processor", 9) == 0)
        cores++;
    }
    fclose(fp);
  }

  // ARM/Apple Silicon: extract chip from device-tree model
  if (!name[0]) {
    char model[128] = "";
    fp = fopen("/proc/device-tree/model", "r");
    if (fp) {
      if (fgets(model, sizeof(model), fp)) {
        int len = strlen(model);
        while (len > 0 && (model[len - 1] == '\n' || model[len - 1] == '\0'))
          len--;
        model[len] = '\0';
      }
      fclose(fp);
    }
    // Extract chip name like "M1" from "Apple MacBook Air (M1, 2020)"
    char *paren = strchr(model, '(');
    if (paren) {
      paren++;
      char *comma = strchr(paren, ',');
      char *end = comma ? comma : strchr(paren, ')');
      if (end) {
        snprintf(name, sizeof(name), "Apple %.*s", (int)(end - paren), paren);
      }
    }
  }

  // Get max frequency from cpufreq
  {
    DIR *cpufreq = opendir("/sys/devices/system/cpu/cpufreq");
    if (cpufreq) {
      struct dirent *ent;
      long max_khz = 0;
      while ((ent = readdir(cpufreq))) {
        if (strncmp(ent->d_name, "policy", 6) != 0)
          continue;
        char path[128];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpufreq/%s/cpuinfo_max_freq",
                 ent->d_name);
        FILE *f = fopen(path, "r");
        if (!f)
          continue;
        long khz = 0;
        if (fscanf(f, "%ld", &khz) == 1 && khz > max_khz)
          max_khz = khz;
        fclose(f);
      }
      closedir(cpufreq);
      if (max_khz > 0)
        max_ghz = max_khz / 1000000.0f;
    }
  }

  if (name[0]) {
    if (cores > 0 && max_ghz > 0)
      add_info("CPU", "%s (%d) @ %.2f GHz", name, cores, max_ghz);
    else if (cores > 0)
      add_info("CPU", "%s (%d)", name, cores);
    else
      add_info("CPU", "%s", name);
  }
#endif
}

// Extract the human product name from `lspci -d VVVV:DDDD` output.
// Example input: "01:00.0 VGA compatible controller: NVIDIA Corporation
// AD106M [GeForce RTX 4070 Max-Q / Mobile] (rev a1)"
// We prefer the bracket content; otherwise the chunk after "Corporation ".
#ifndef __APPLE__
static int gpu_lookup_lspci(const char *pci_id, char *out, int outlen) {
  if (!pci_id || !pci_id[0])
    return 0;
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "lspci -d %s 2>/dev/null", pci_id);
  FILE *fp = popen(cmd, "r");
  if (!fp)
    return 0;
  char line[256];
  int ok = 0;
  if (fgets(line, sizeof(line), fp)) {
    int l = strlen(line);
    while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r'))
      line[--l] = '\0';
    char *rev = strstr(line, " (rev ");
    if (rev)
      *rev = '\0';

    char *lb = strrchr(line, '[');
    char *rb = strrchr(line, ']');
    const char *name = NULL;

    // AMD's lspci strings always wrap the vendor tag in brackets
    // (e.g. "[AMD/ATI]"); discrete cards additionally wrap the product
    // name in a second pair ("[Radeon RX 9070 XT]"), but some iGPUs only
    // have the single vendor bracket. Blindly taking the last bracket
    // pair picks up "AMD/ATI" in that case, so count brackets first.
    int bracket_count = 0;
    for (char *pp = line; *pp; pp++)
      if (*pp == '[')
        bracket_count++;

    if (bracket_count >= 2 && lb && rb && rb > lb) {
      *rb = '\0';
      name = lb + 1;
    } else if (lb && rb && rb > lb) {
      char *after = rb + 1;
      while (*after == ' ')
        after++;
      if (*after)
        name = after;
    }

    if (!name) {
      char *corp = strstr(line, " Corporation ");
      int skip = corp ? 13 : 0;
      if (!corp) {
        corp = strstr(line, " Corp ");
        if (corp)
          skip = 6;
      }
      // Skip past "bus:dev.fn class:" prefix even if no Corporation.
      if (!corp) {
        char *c1 = strchr(line, ':');
        if (c1) {
          char *c2 = strchr(c1 + 1, ':');
          if (c2) {
            corp = c2;
            skip = 1;
          }
        }
      }
      name = corp ? (corp + skip) : line;
      while (*name == ' ')
        name++;
    }

    if (name && *name) {
      strncpy(out, name, outlen - 1);
      out[outlen - 1] = '\0';
      ok = 1;
    }
  }
  pclose(fp);
  return ok;
}

static int str_ci_contains(const char *haystack, const char *needle) {
  size_t hlen = strlen(haystack), nlen = strlen(needle);
  if (nlen == 0 || nlen > hlen)
    return 0;
  for (size_t i = 0; i + nlen <= hlen; i++) {
    size_t j = 0;
    while (j < nlen &&
           tolower((unsigned char)haystack[i + j]) ==
               tolower((unsigned char)needle[j]))
      j++;
    if (j == nlen)
      return 1;
  }
  return 0;
}

static int amd_name_is_igpu(const char *name) {
  static const char *codenames[] = {
      "Raphael",     "Phoenix",     "Phoenix2",  "Hawk Point",
      "Renoir",      "Cezanne",     "Rembrandt", "Picasso",
      "Raven",       "Raven2",      "Van Gogh",  "Mendocino",
      "Barcelo",     "Strix Point", "Strix Halo", "Krackan Point",
      NULL};
  for (int i = 0; codenames[i]; i++)
    if (str_ci_contains(name, codenames[i]))
      return 1;
  return 0;
}
#endif

static void gather_gpu(void) {
#ifdef _WIN32
  platform_gather_gpu(add_info);
  return;
#elif defined(__APPLE__)
  FILE *fp = popen("system_profiler SPDisplaysDataType 2>/dev/null", "r");
  if (!fp) return;
  char buf[512];
  char gpu[128] = "";
  while (fgets(buf, sizeof(buf), fp)) {
    char *p = strstr(buf, "Chipset Model:");
    if (!p) p = strstr(buf, "Chip:");
    if (p) {
      p = strchr(p, ':');
      if (p) {
        p++;
        while (*p == ' ') p++;
        int len = strlen(p);
        while (len > 0 && (p[len-1]=='\n'||p[len-1]=='\r')) p[--len]='\0';
        if (len > 0 && len < (int)sizeof(gpu)) { memcpy(gpu, p, len+1); break; }
      }
    }
  }
  pclose(fp);
  if (gpu[0])
    add_info("GPU", "%s", gpu);
#else
  DIR *d = opendir("/sys/class/drm");
  if (!d)
    return;
  struct dirent *ent;
  while ((ent = readdir(d))) {
    // Only cardN (not cardN-CONNECTOR or renderD*)
    if (strncmp(ent->d_name, "card", 4) != 0)
      continue;
    int all_digits = 1;
    for (int i = 4; ent->d_name[i]; i++) {
      if (ent->d_name[i] < '0' || ent->d_name[i] > '9') {
        all_digits = 0;
        break;
      }
    }
    if (!all_digits)
      continue;

    char path[256];
    snprintf(path, sizeof(path), "/sys/class/drm/%s/device/uevent",
             ent->d_name);
    FILE *fp = fopen(path, "r");
    if (!fp)
      continue;
    char driver[32] = "", pci_id[16] = "", compat[64] = "";
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
      if (strncmp(buf, "DRIVER=", 7) == 0) {
        char *v = buf + 7;
        int l = strlen(v);
        while (l > 0 && (v[l - 1] == '\n' || v[l - 1] == '\r'))
          v[--l] = '\0';
        strncpy(driver, v, sizeof(driver) - 1);
      } else if (strncmp(buf, "PCI_ID=", 7) == 0) {
        char *v = buf + 7;
        int l = strlen(v);
        while (l > 0 && (v[l - 1] == '\n' || v[l - 1] == '\r'))
          v[--l] = '\0';
        strncpy(pci_id, v, sizeof(pci_id) - 1);
      } else if (strncmp(buf, "OF_COMPATIBLE_0=", 16) == 0) {
        char *v = buf + 16;
        int l = strlen(v);
        while (l > 0 && (v[l - 1] == '\n' || v[l - 1] == '\r'))
          v[--l] = '\0';
        strncpy(compat, v, sizeof(compat) - 1);
      }
    }
    fclose(fp);

    char name[160] = "";
    const char *type = "";

    // Skip display subsystems — they're not GPUs
    if (strstr(compat, "display-subsystem"))
      continue;

    if (strncmp(compat, "apple,agx", 9) == 0) {
      char cpu[64] = "";
      FILE *mfp = fopen("/proc/device-tree/model", "r");
      if (mfp) {
        char model[128];
        if (fgets(model, sizeof(model), mfp)) {
          char *paren = strchr(model, '(');
          if (paren) {
            paren++;
            char *comma = strchr(paren, ',');
            char *end = comma ? comma : strchr(paren, ')');
            if (end)
              snprintf(cpu, sizeof(cpu), "%.*s", (int)(end - paren), paren);
          }
        }
        fclose(mfp);
      }
      if (cpu[0])
        snprintf(name, sizeof(name), "Apple %s", cpu);
      else
        strcpy(name, "Apple GPU");
      type = "Integrated";
    } else if (pci_id[0] && gpu_lookup_lspci(pci_id, name, sizeof(name))) {
      // lspci gave us a human name.
    } else if (strcmp(driver, "i915") == 0 || strcmp(driver, "xe") == 0) {
      strcpy(name, "Intel Graphics");
    } else if (strcmp(driver, "amdgpu") == 0 || strcmp(driver, "radeon") == 0) {
      strcpy(name, "AMD Graphics");
    } else if (strcmp(driver, "nvidia") == 0 ||
               strcmp(driver, "nouveau") == 0) {
      strcpy(name, "NVIDIA GPU");
    } else if (driver[0]) {
      strncpy(name, driver, sizeof(name) - 1);
    }

    if (!type[0]) {
      if (!strcmp(driver, "i915") || !strcmp(driver, "xe"))
        type = "Integrated";
      else if (!strcmp(driver, "nvidia") || !strcmp(driver, "nouveau"))
        type = "Discrete";
      else if (!strcmp(driver, "amdgpu"))
        type = amd_name_is_igpu(name) ? "Integrated" : "Discrete";
    }

    if (!name[0])
      continue;
    if (type[0])
      add_info("GPU", "%s [%s]", name, type);
    else
      add_info("GPU", "%s", name);
  }
  closedir(d);
#endif
}

static void gather_memory(void) {
#ifdef _WIN32
  char mem_win[256] = "";
  platform_gather_memory(mem_win, sizeof(mem_win));
  if (mem_win[0])
    add_info("Memory", "%s", mem_win);
  return;
#elif defined(__APPLE__)
  long long total = sysctl_long("hw.memsize");
  if (total <= 0) return;

  long page_size = 4096;
  long long free_pages = 0, inactive_pages = 0, speculative_pages = 0;
  FILE *fp = popen("vm_stat 2>/dev/null", "r");
  if (fp) {
    char buf[128];
    while (fgets(buf, sizeof(buf), fp)) {
      if (sscanf(buf, "Pages free: %lld.", &free_pages) == 1) {}
      else if (sscanf(buf, "Pages inactive: %lld.", &inactive_pages) == 1) {}
      else if (sscanf(buf, "Pages speculative: %lld.", &speculative_pages) == 1) {}
      else if (strstr(buf, "page size of")) {
        sscanf(buf, "Mach Virtual Memory Statistics: (page size of %ld)", &page_size);
      }
    }
    pclose(fp);
  }
  long long avail = (free_pages + inactive_pages + speculative_pages) * page_size;

  float used_gib = (total - avail) / 1073741824.0f;
  float total_gib = total / 1073741824.0f;
  int pct = (int)((total - avail) * 100 / total);
  const char *color = pct >= 80 ? "31" : pct >= 50 ? "93" : "32";
  add_info("Memory", "%.2f GiB / %.2f GiB (\033[%sm%d%%\033[0m)", used_gib,
           total_gib, color, pct);
#else
  long long total = 0, avail = 0;
  FILE *fp = fopen("/proc/meminfo", "r");
  if (!fp)
    return;
  char buf[128];
  while (fgets(buf, sizeof(buf), fp)) {
    if (strncmp(buf, "MemTotal:", 9) == 0)
      sscanf(buf + 9, " %lld", &total);
    else if (strncmp(buf, "MemAvailable:", 13) == 0)
      sscanf(buf + 13, " %lld", &avail);
  }
  fclose(fp);
  if (total <= 0)
    return;

  float used_gib = (total - avail) / 1048576.0f;
  float total_gib = total / 1048576.0f;
  int pct = (int)((total - avail) * 100 / total);

  // Color: green <50%, yellow 50-79%, red 80%+
  const char *color = pct >= 80 ? "31" : pct >= 50 ? "93" : "32";

  add_info("Memory", "%.2f GiB / %.2f GiB (\033[%sm%d%%\033[0m)", used_gib,
           total_gib, color, pct);
#endif
}

static void gather_swap(void) {
#ifdef _WIN32
  char swap_win[256] = "";
  platform_gather_swap(swap_win, sizeof(swap_win));
  if (swap_win[0])
    add_info("Swap", "%s", swap_win);
  return;
#elif defined(__APPLE__)
  char swapstr[256] = "";
  if (!sysctl_str("vm.swapusage", swapstr, sizeof(swapstr)))
    return;
  float total_mb = 0, used_mb = 0;
  sscanf(swapstr, "swap on %*s (total %fM %*s %fM", &total_mb, &used_mb);
  if (total_mb <= 0) return;
  int pct = (int)(used_mb * 100 / total_mb);
  const char *color = pct >= 80 ? "31" : pct >= 50 ? "93" : "32";
  add_info("Swap", "%.2f MiB / %.2f MiB (\033[%sm%d%%\033[0m)",
           used_mb, total_mb, color, pct);
#else
  long long total = 0, free_s = 0;
  FILE *fp = fopen("/proc/meminfo", "r");
  if (!fp)
    return;
  char buf[128];
  while (fgets(buf, sizeof(buf), fp)) {
    if (strncmp(buf, "SwapTotal:", 10) == 0)
      sscanf(buf + 10, " %lld", &total);
    else if (strncmp(buf, "SwapFree:", 9) == 0)
      sscanf(buf + 9, " %lld", &free_s);
  }
  fclose(fp);
  if (total <= 0)
    return;

  long long used = total - free_s;
  int pct = (int)(used * 100 / total);
  const char *color = pct >= 80 ? "31" : pct >= 50 ? "93" : "32";

  if (total >= 1048576)
    add_info("Swap", "%.2f GiB / %.2f GiB (\033[%sm%d%%\033[0m)",
             used / 1048576.0f, total / 1048576.0f, color, pct);
  else
    add_info("Swap", "%.2f MiB / %.2f MiB (\033[%sm%d%%\033[0m)",
             used / 1024.0f, total / 1024.0f, color, pct);
#endif
}

static void gather_disk_one(const char *path) {
#ifdef _WIN32
  char disk_win[256] = "";
  platform_gather_disk(path, disk_win, sizeof(disk_win));
  if (disk_win[0]) {
    char label[64];
    const char *display_path = (path && strcmp(path, "/") != 0) ? path : "C:";
    snprintf(label, sizeof(label), "Disk (%s)", display_path);
    add_info(label, "%s", disk_win);
  }
  return;
#else
  struct statvfs st;
  if (statvfs(path, &st) != 0)
    return;
  if (st.f_blocks == 0)
    return;

  float total_gib = (float)st.f_blocks * (float)st.f_frsize / (1024 * 1024 * 1024);
  float free_gib = (float)st.f_bfree * (float)st.f_frsize / (1024 * 1024 * 1024);
  float used_gib = total_gib - free_gib;
  int pct = (int)(used_gib * 100 / total_gib);
  const char *color = pct >= 80 ? "31" : pct >= 50 ? "93" : "32";

  char label[64];
  snprintf(label, sizeof(label), "Disk (%s)", path);

  char fstype[32] = "";
#ifdef __APPLE__
  struct statfs *mnts;
  int count = getmntinfo(&mnts, MNT_NOWAIT);
  for (int i = 0; i < count; i++) {
    if (strcmp(mnts[i].f_mntonname, path) == 0) {
      strncpy(fstype, mnts[i].f_fstypename, sizeof(fstype) - 1);
      break;
    }
  }
#else
  FILE *fp = fopen("/proc/mounts", "r");
  if (fp) {
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) {
      char dev[128], mnt[128], fs[32];
      if (sscanf(buf, "%127s %127s %31s", dev, mnt, fs) == 3) {
        if (strcmp(mnt, path) == 0) {
          strncpy(fstype, fs, sizeof(fstype) - 1);
          break;
        }
      }
    }
    fclose(fp);
  }
#endif

  if (fstype[0])
    add_info(label, "%.2f GiB / %.2f GiB (\033[%sm%d%%\033[0m) - %s",
             used_gib, total_gib, color, pct, fstype);
  else
    add_info(label, "%.2f GiB / %.2f GiB (\033[%sm%d%%\033[0m)", used_gib,
             total_gib, color, pct);
#endif
}

static void gather_disk(void) {
  gather_disk_one("/");
  for (int i = 0; i < extra_disk_count; i++)
    gather_disk_one(extra_disks[i]);
}

static void gather_battery(void) {
#ifdef _WIN32
  char label_win[80] = "";
  char val_win[256] = "";
  platform_gather_battery(label_win, sizeof(label_win), val_win, sizeof(val_win));
  if (label_win[0] && val_win[0])
    add_info(label_win, "%s", val_win);
  return;
#elif defined(__APPLE__) && !defined(LEGACY_IOKIT)
  CFTypeRef info = IOPSCopyPowerSourcesInfo();
  if (!info) return;
  CFArrayRef sources = IOPSCopyPowerSourcesList(info);
  if (!sources || CFArrayGetCount(sources) == 0) {
    if (sources) CFRelease(sources);
    CFRelease(info);
    return;
  }
  CFDictionaryRef src = IOPSGetPowerSourceDescription(info, CFArrayGetValueAtIndex(sources, 0));
  if (!src) { CFRelease(sources); CFRelease(info); return; }

  int capacity = -1;
  CFNumberRef capRef = CFDictionaryGetValue(src, CFSTR(kIOPSCurrentCapacityKey));
  if (capRef) CFNumberGetValue(capRef, kCFNumberIntType, &capacity);

  int isCharging = 0;
  CFBooleanRef chgRef = CFDictionaryGetValue(src, CFSTR(kIOPSIsChargingKey));
  if (chgRef) isCharging = CFBooleanGetValue(chgRef);

  int timeToEmpty = -1;
  CFNumberRef tteRef = CFDictionaryGetValue(src, CFSTR(kIOPSTimeToEmptyKey));
  if (tteRef) CFNumberGetValue(tteRef, kCFNumberIntType, &timeToEmpty);

  CFRelease(sources);
  CFRelease(info);

  if (capacity < 0) return;

  const char *status = isCharging ? "Charging" : (timeToEmpty >= 0 ? "Discharging" : "");
  char time_str[64] = "";
  if (timeToEmpty > 0 && !isCharging) {
    int h = timeToEmpty / 60, m = timeToEmpty % 60;
    if (h > 0)
      snprintf(time_str, sizeof(time_str), "%d hour%s, %d min%s remaining",
               h, h==1?"":"s", m, m==1?"":"s");
    else
      snprintf(time_str, sizeof(time_str), "%d min%s remaining", m, m==1?"":"s");
  }

  const char *color = capacity >= 50 ? "32" : capacity >= 20 ? "93" : "31";
  if (time_str[0] && status[0])
    add_info("Battery", "\033[%sm%d%%\033[0m (%s) [%s]", color, capacity, time_str, status);
  else if (status[0])
    add_info("Battery", "\033[%sm%d%%\033[0m [%s]", color, capacity, status);
  else
    add_info("Battery", "\033[%sm%d%%\033[0m", color, capacity);
#else
  // Find first battery in /sys/class/power_supply
  char bat_name[64] = "";
  DIR *psd = opendir("/sys/class/power_supply");
  if (!psd)
    return;
  struct dirent *psent;
  while ((psent = readdir(psd))) {
    if (psent->d_name[0] == '.')
      continue;
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity",
             psent->d_name);
    FILE *test = fopen(path, "r");
    if (test) {
      fclose(test);
      strncpy(bat_name, psent->d_name, sizeof(bat_name) - 1);
      break;
    }
  }
  closedir(psd);
  if (!bat_name[0])
    return;

  char path[256];
  FILE *fp;
  int capacity = -1;
  char status[32] = "";

  // Prefer energy_now/energy_full for accurate percentage
  long energy_now = 0, energy_full = 0;
  snprintf(path, sizeof(path), "/sys/class/power_supply/%s/energy_now",
           bat_name);
  fp = fopen(path, "r");
  if (fp) {
    if (fscanf(fp, "%ld", &energy_now) != 1)
      energy_now = 0;
    fclose(fp);
  }
  snprintf(path, sizeof(path), "/sys/class/power_supply/%s/energy_full",
           bat_name);
  fp = fopen(path, "r");
  if (fp) {
    if (fscanf(fp, "%ld", &energy_full) != 1)
      energy_full = 0;
    fclose(fp);
  }
  if (energy_full > 0)
    capacity = (int)(energy_now * 100 / energy_full);

  // Fall back to charge_now/charge_full
  if (capacity < 0) {
    long charge_now = 0, charge_full = 0;
    snprintf(path, sizeof(path), "/sys/class/power_supply/%s/charge_now",
             bat_name);
    fp = fopen(path, "r");
    if (fp) {
      if (fscanf(fp, "%ld", &charge_now) != 1)
        charge_now = 0;
      fclose(fp);
    }
    snprintf(path, sizeof(path), "/sys/class/power_supply/%s/charge_full",
             bat_name);
    fp = fopen(path, "r");
    if (fp) {
      if (fscanf(fp, "%ld", &charge_full) != 1)
        charge_full = 0;
      fclose(fp);
    }
    if (charge_full > 0)
      capacity = (int)(charge_now * 100 / charge_full);
  }

  // Last resort: capacity file
  if (capacity < 0) {
    snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity",
             bat_name);
    fp = fopen(path, "r");
    if (fp) {
      if (fscanf(fp, "%d", &capacity) != 1)
        capacity = -1;
      fclose(fp);
    }
  }

  snprintf(path, sizeof(path), "/sys/class/power_supply/%s/status", bat_name);
  fp = fopen(path, "r");
  if (fp) {
    if (fgets(status, sizeof(status), fp)) {
      int len = strlen(status);
      while (len > 0 && (status[len - 1] == '\n' || status[len - 1] == '\r'))
        status[--len] = '\0';
    }
    fclose(fp);
  }

  // Get time remaining estimate from power_now
  char time_str[64] = "";
  if (energy_now > 0) {
    long power_now = 0;
    snprintf(path, sizeof(path), "/sys/class/power_supply/%s/power_now",
             bat_name);
    fp = fopen(path, "r");
    if (fp) {
      if (fscanf(fp, "%ld", &power_now) != 1)
        power_now = 0;
      fclose(fp);
    }
    // power_now is negative when discharging on some systems
    if (power_now < 0)
      power_now = -power_now;
    if (power_now > 0) {
      int mins_left = (int)((float)energy_now / power_now * 60);
      int hours = mins_left / 60;
      int mins = mins_left % 60;
      if (hours > 0)
        snprintf(time_str, sizeof(time_str), "%d hour%s, %d min%s remaining",
                 hours, hours == 1 ? "" : "s", mins, mins == 1 ? "" : "s");
      else
        snprintf(time_str, sizeof(time_str), "%d min%s remaining", mins,
                 mins == 1 ? "" : "s");
    }
  }

  char model[64] = "";
  snprintf(path, sizeof(path), "/sys/class/power_supply/%s/model_name",
           bat_name);
  fp = fopen(path, "r");
  if (!fp) {
    snprintf(path, sizeof(path), "/sys/class/power_supply/%s/manufacturer",
             bat_name);
    fp = fopen(path, "r");
  }
  if (fp) {
    if (fgets(model, sizeof(model), fp)) {
      int len = strlen(model);
      while (len > 0 && (model[len - 1] == '\n' || model[len - 1] == '\r' ||
                         model[len - 1] == ' '))
        model[--len] = '\0';
    }
    fclose(fp);
  }

  char label[80];
  if (model[0])
    snprintf(label, sizeof(label), "Battery (%s)", model);
  else
    snprintf(label, sizeof(label), "Battery");

  if (capacity >= 0) {
    const char *color = capacity >= 50 ? "32" : capacity >= 20 ? "93" : "31";
    if (time_str[0] && status[0])
      add_info(label, "\033[%sm%d%%\033[0m (%s) [%s]", color, capacity,
               time_str, status);
    else if (status[0])
      add_info(label, "\033[%sm%d%%\033[0m [%s]", color, capacity, status);
    else
      add_info(label, "\033[%sm%d%%\033[0m", color, capacity);
  }
#endif
}

static void gather_terminal(void) {
#ifdef _WIN32
  char term_win[128] = "";
  platform_gather_terminal(term_win, sizeof(term_win));
  if (term_win[0])
    add_info("Terminal", "%s", term_win);
  return;
#endif
  char term[64] = "";
  // Try TERM_PROGRAM first, then walk up the process tree
  char *tp = getenv("TERM_PROGRAM");
  if (tp && tp[0]) {
    strncpy(term, tp, sizeof(term) - 1);
  } else if (getenv("KITTY_WINDOW_ID")) {
    strcpy(term, "kitty");
  } else if (getenv("ALACRITTY_LOG")) {
    strcpy(term, "alacritty");
  } else if (getenv("WEZTERM_PANE")) {
    strcpy(term, "wezterm");
  } else if (getenv("GHOSTTY_RESOURCES_DIR")) {
    strcpy(term, "ghostty");
  } else if (getenv("TERMINAL_EMULATOR")) {
    strncpy(term, getenv("TERMINAL_EMULATOR"), sizeof(term) - 1);
  } else {
#ifdef __APPLE__
    // Walk up the process tree using sysctl
    pid_t pid = getpid();
    for (int depth = 0; depth < 4; depth++) {
      struct kinfo_proc kp;
      size_t klen = sizeof(kp);
      int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
      if (sysctl(mib, 4, &kp, &klen, NULL, 0) != 0) break;
      pid_t ppid = kp.kp_eproc.e_ppid;
      if (ppid <= 0) break;
      pid = ppid;

      mib[3] = pid;
      klen = sizeof(kp);
      if (sysctl(mib, 4, &kp, &klen, NULL, 0) != 0) break;
      strncpy(term, kp.kp_proc.p_comm, sizeof(term) - 1);
      term[sizeof(term) - 1] = '\0';

      if (term[0] && strcmp(term, "bash") != 0 && strcmp(term, "zsh") != 0 &&
          strcmp(term, "sh") != 0 && strcmp(term, "dash") != 0 &&
          strcmp(term, "fish") != 0 && strcmp(term, "nu") != 0 &&
          strcmp(term, "elvish") != 0 && strcmp(term, "xonsh") != 0 &&
          strcmp(term, "tcsh") != 0 && strcmp(term, "csh") != 0)
        break;
      term[0] = '\0';
    }
#else
    // Walk up the process tree by reading /proc directly
    pid_t pid = getpid();
    for (int depth = 0; depth < 4; depth++) {
      // Get PPID from /proc/<pid>/status
      char path[64];
      snprintf(path, sizeof(path), "/proc/%d/status", pid);
      FILE *fp = fopen(path, "r");
      if (!fp) break;
      pid_t ppid = 0;
      char buf[128];
      while (fgets(buf, sizeof(buf), fp)) {
        if (sscanf(buf, "PPid:\t%d", &ppid) == 1)
          break;
      }
      fclose(fp);
      if (ppid <= 0) break;
      pid = ppid;

      // Get command name from /proc/<pid>/comm
      snprintf(path, sizeof(path), "/proc/%d/comm", pid);
      fp = fopen(path, "r");
      if (!fp) break;
      if (!fgets(term, sizeof(term), fp)) { fclose(fp); break; }
      fclose(fp);
      int len = strlen(term);
      while (len > 0 && (term[len - 1] == '\n' || term[len - 1] == '\r'))
        term[--len] = '\0';

      // If we found a non-shell, stop
      if (term[0] && strcmp(term, "bash") != 0 && strcmp(term, "zsh") != 0 &&
          strcmp(term, "sh") != 0 && strcmp(term, "dash") != 0 &&
          strcmp(term, "fish") != 0 && strcmp(term, "nu") != 0 &&
          strcmp(term, "elvish") != 0 && strcmp(term, "xonsh") != 0 &&
          strcmp(term, "tcsh") != 0 && strcmp(term, "csh") != 0)
        break;
      term[0] = '\0';
    }
#endif
  }
  if (term[0]) {
    char version[64] = "";
    get_cmd_version(term, version, sizeof(version));
    if (version[0])
      add_info("Terminal", "%s %s", term, version);
    else
      add_info("Terminal", "%s", term);
  }
}

#ifndef __APPLE__
static int iface_is_wireless(const char *name) {
  char path[128];
  snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", name);
  DIR *d = opendir(path);
  if (d) {
    closedir(d);
    return 1;
  }
  return 0;
}
#endif

// Best-effort human label for an interface. Falls back to the raw name
// wrapped in "Local IP (...)" for anything unrecognized.
static void iface_label(const char *name, int is_default, char *out, int outlen) {
  const char *kind = NULL;

  if (strncmp(name, "tailscale", 9) == 0)
    kind = "Tailscale";
  else if (strncmp(name, "zt", 2) == 0)
    kind = "ZeroTier";
  else if (strncmp(name, "wg", 2) == 0)
    kind = "WireGuard";
  else if (strncmp(name, "ppp", 3) == 0)
    kind = "PPP";
  else if (strncmp(name, "docker", 6) == 0 || strncmp(name, "br-", 3) == 0 ||
           strncmp(name, "veth", 4) == 0 || strncmp(name, "virbr", 5) == 0)
    kind = "Virtual";
  else if (strncmp(name, "utun", 4) == 0 || strncmp(name, "tun", 3) == 0 ||
           strncmp(name, "tap", 3) == 0)
    kind = "VPN";
#ifdef __APPLE__
  else if (strncmp(name, "en", 2) == 0)
    kind = is_default ? "Wi-Fi/Ethernet" : NULL;
#else
  else if (iface_is_wireless(name))
    kind = "Wi-Fi";
  else if (strncmp(name, "eth", 3) == 0 || strncmp(name, "en", 2) == 0)
    kind = "Ethernet";
#endif

  if (kind)
    snprintf(out, outlen, "%s (%s)", kind, name);
  else
    snprintf(out, outlen, "Local IP (%s)", name);
}

static int get_default_route_iface(char *out, int outlen) {
#ifdef __APPLE__
  FILE *fp = popen("route -n get default 2>/dev/null", "r");
  if (!fp)
    return 0;
  char buf[256];
  int found = 0;
  while (fgets(buf, sizeof(buf), fp)) {
    char *p = strstr(buf, "interface:");
    if (p) {
      p += 10;
      while (*p == ' ')
        p++;
      int len = strlen(p);
      while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r' || p[len - 1] == ' '))
        p[--len] = '\0';
      if (len > 0 && len < outlen) {
        memcpy(out, p, len + 1);
        found = 1;
      }
      break;
    }
  }
  pclose(fp);
  return found;
#else
  FILE *fp = fopen("/proc/net/route", "r");
  if (!fp)
    return 0;
  char line[256];
  int found = 0;
  unsigned long best_metric = ULONG_MAX;
  if (!fgets(line, sizeof(line), fp)) { // skip header
    fclose(fp);
    return 0;
  }
  while (fgets(line, sizeof(line), fp)) {
    char iface[64];
    unsigned long dest, metric;
    if (sscanf(line, "%63s %lx %*x %*x %*d %*d %lu", iface, &dest, &metric) == 3) {
      if (dest == 0 && metric < best_metric) {
        best_metric = metric;
        strncpy(out, iface, outlen - 1);
        out[outlen - 1] = '\0';
        found = 1;
      }
    }
  }
  fclose(fp);
  return found;
#endif
}

static void gather_ip(void) {
#ifdef _WIN32
  platform_gather_ip(add_info);
  return;
#else
  char default_iface[64] = "";
  get_default_route_iface(default_iface, sizeof(default_iface));

  struct ifaddrs *ifa_list, *ifa;
  if (getifaddrs(&ifa_list) != 0)
    return;

  struct {
    char name[64];
    char addr[80];
    int is_default;
  } entries[16];
  int n = 0;

  for (ifa = ifa_list; ifa && n < 16; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
      continue;
#ifdef __APPLE__
    if (strcmp(ifa->ifa_name, "lo0") == 0)
#else
    if (strcmp(ifa->ifa_name, "lo") == 0)
#endif
      continue;

    struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
    char addr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sa->sin_addr, addr, sizeof(addr));

    struct sockaddr_in *mask = (struct sockaddr_in *)ifa->ifa_netmask;
    unsigned int bits = 0;
    if (mask) {
      unsigned int m = ntohl(mask->sin_addr.s_addr);
      while (m & 0x80000000) {
        bits++;
        m <<= 1;
      }
    }

    strncpy(entries[n].name, ifa->ifa_name, sizeof(entries[n].name) - 1);
    entries[n].name[sizeof(entries[n].name) - 1] = '\0';
    if (bits > 0)
      snprintf(entries[n].addr, sizeof(entries[n].addr), "%s/%u", addr, bits);
    else
      snprintf(entries[n].addr, sizeof(entries[n].addr), "%s", addr);
    entries[n].is_default = default_iface[0] && strcmp(ifa->ifa_name, default_iface) == 0;
    n++;
  }
  freeifaddrs(ifa_list);

  for (int pass = 1; pass >= 0; pass--) {
    for (int i = 0; i < n; i++) {
      if (entries[i].is_default != pass)
        continue;
      char lbl[64];
      iface_label(entries[i].name, entries[i].is_default, lbl, sizeof(lbl));
      add_info(lbl, "%s", entries[i].addr);
    }
  }
#endif
}

static void gather_locale(void) {
#ifdef _WIN32
  char loc_win[64] = "";
  platform_gather_locale(loc_win, sizeof(loc_win));
  if (loc_win[0])
    add_info("Locale", "%s", loc_win);
  return;
#else
  char *lang = getenv("LANG");
  if (lang && lang[0])
    add_info("Locale", "%s", lang);
#endif
}

static int read_ini_key(const char *path, const char *section, const char *key,
                        char *out, int maxlen) {
  FILE *fp = fopen(path, "r");
  if (!fp)
    return 0;
  char buf[512];
  size_t keylen = strlen(key);
  size_t seclen = section ? strlen(section) : 0;
  int in_section = section ? 0 : 1;
  int found = 0;
  while (fgets(buf, sizeof(buf), fp)) {
    char *line = buf;
    while (*line == ' ' || *line == '\t')
      line++;
    int len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                       line[len - 1] == ' ' || line[len - 1] == '\t'))
      line[--len] = '\0';
    if (line[0] == '[') {
      if (section)
        in_section = strncmp(line + 1, section, seclen) == 0 &&
                     line[1 + seclen] == ']';
      continue;
    }
    if (!in_section || strncmp(line, key, keylen) != 0)
      continue;
    char *val = line + keylen;
    while (*val == ' ' || *val == '\t')
      val++;
    if (*val != '=')
      continue;
    val++;
    while (*val == ' ' || *val == '\t')
      val++;
    int vlen = strlen(val);
    if (vlen >= 2 && ((val[0] == '"' && val[vlen - 1] == '"') ||
                      (val[0] == '\'' && val[vlen - 1] == '\''))) {
      val[vlen - 1] = '\0';
      val++;
      vlen -= 2;
    }
    if (vlen > 0 && vlen < maxlen) {
      memcpy(out, val, vlen + 1);
      found = 1;
    }
    break;
  }
  fclose(fp);
  return found;
}

static int read_gtk3_setting(const char *key, char *out, int maxlen) {
  const char *home = getenv("HOME");
  if (!home)
    return 0;
  char path[512];
  snprintf(path, sizeof(path), "%s/.config/gtk-3.0/settings.ini", home);
  return read_ini_key(path, NULL, key, out, maxlen);
}

static int read_gtk2_setting(const char *key, char *out, int maxlen) {
  const char *home = getenv("HOME");
  if (!home)
    return 0;
  char path[512];
  snprintf(path, sizeof(path), "%s/.gtkrc-2.0", home);
  if (read_ini_key(path, NULL, key, out, maxlen))
    return 1;
  snprintf(path, sizeof(path), "%s/.config/gtk-2.0/gtkrc", home);
  return read_ini_key(path, NULL, key, out, maxlen);
}

static void read_gtk_setting(const char *key, char *out, int maxlen) {
  read_gtk3_setting(key, out, maxlen);
}

static int qtct_conf_path(char *out, size_t outsz) {
  const char *home = getenv("HOME");
  if (!home)
    return 0;
  static const char *variants[] = {"qt6ct/qt6ct.conf", "qt5ct/qt5ct.conf",
                                   NULL};
  const char *platform = getenv("QT_QPA_PLATFORMTHEME");
  if (platform && strncmp(platform, "qt", 2) == 0) {
    snprintf(out, outsz, "%s/.config/%s/%s.conf", home, platform, platform);
    if (access(out, R_OK) == 0)
      return 1;
  }
  for (int i = 0; variants[i]; i++) {
    snprintf(out, outsz, "%s/.config/%s", home, variants[i]);
    if (access(out, R_OK) == 0)
      return 1;
  }
  return 0;
}

static int read_kdeglobals(const char *section, const char *key, char *out,
                           int maxlen) {
  const char *home = getenv("HOME");
  if (!home)
    return 0;
  char path[512];
  snprintf(path, sizeof(path), "%s/.config/kdeglobals", home);
  return read_ini_key(path, section, key, out, maxlen);
}

static void qt_theme(char *out, int maxlen) {
  char path[512];
  if (qtct_conf_path(path, sizeof(path)) &&
      read_ini_key(path, "Appearance", "style", out, maxlen) && out[0])
    return;
  if (read_kdeglobals("KDE", "widgetStyle", out, maxlen) && strstr(out, "ct-style"))
    out[0] = '\0';
}

static void qt_icons(char *out, int maxlen) {
  char path[512];
  if (qtct_conf_path(path, sizeof(path)) &&
      read_ini_key(path, "Appearance", "icon_theme", out, maxlen) && out[0])
    return;
  read_kdeglobals("Icons", "Theme", out, maxlen);
}

static void font_pt_format(char *font, size_t fontsz) {
  char *last = strrchr(font, ' ');
  if (!last || !last[1])
    return;
  for (const char *p = last + 1; *p; p++)
    if (*p < '0' || *p > '9')
      return;
  char size[16];
  snprintf(size, sizeof(size), "%s", last + 1);
  size_t room = fontsz - (size_t)(last - font);
  snprintf(last, room, " (%spt)", size);
}

static void qt_font(char *out, int maxlen) {
  char path[512];
  char raw[192] = "";
  if (!qtct_conf_path(path, sizeof(path)))
    return;
  if (!read_ini_key(path, "Fonts", "general", raw, sizeof(raw)) || !raw[0])
    return;
  char *comma = strchr(raw, ',');
  if (!comma)
    return;
  *comma = '\0';
  char *size = comma + 1;
  char *end = strchr(size, ',');
  if (end)
    *end = '\0';
  snprintf(out, maxlen, "%s (%spt)", raw, size);
}

static void append_tagged(char *dst, size_t dstsz, const char *val,
                          const char *tag) {
  if (!val[0])
    return;
  size_t n = strlen(dst);
  snprintf(dst + n, dstsz - n, "%s%s [%s]", n ? ", " : "", val, tag);
}

static void append_gtk_pair(char *dst, size_t dstsz, const char *gtk2,
                            const char *gtk3) {
  if (gtk2[0] && gtk3[0] && strcmp(gtk2, gtk3) == 0) {
    append_tagged(dst, dstsz, gtk3, "GTK2/3");
    return;
  }
  append_tagged(dst, dstsz, gtk2, "GTK2");
  append_tagged(dst, dstsz, gtk3, "GTK3");
}

static void gather_theme(void) {
#ifdef _WIN32
  char theme_win[64] = "";
  platform_gather_theme(theme_win, sizeof(theme_win));
  if (theme_win[0])
    add_info("Theme", "%s", theme_win);
  return;
#elif defined(__APPLE__)
  char style[64] = "";
  FILE *fp = popen("defaults read -g AppleInterfaceStyle 2>/dev/null", "r");
  if (fp) {
    if (fgets(style, sizeof(style), fp)) {
      int len = strlen(style);
      while (len > 0 && (style[len-1]=='\n'||style[len-1]=='\r')) style[--len]='\0';
    }
    pclose(fp);
  }
  if (style[0])
    add_info("Theme", "%s", style);
  else
    add_info("Theme", "Light");
#else
  char qt[64] = "", gtk2[64] = "", gtk3[64] = "", out[256] = "";
  qt_theme(qt, sizeof(qt));
  read_gtk2_setting("gtk-theme-name", gtk2, sizeof(gtk2));
  read_gtk3_setting("gtk-theme-name", gtk3, sizeof(gtk3));
  append_tagged(out, sizeof(out), qt, "Qt");
  append_gtk_pair(out, sizeof(out), gtk2, gtk3);
  if (out[0])
    add_info("Theme", "%s", out);
#endif
}

static void gather_icons(void) {
#ifdef _WIN32
  char icons_win[64] = "";
  platform_gather_icons(icons_win, sizeof(icons_win));
  if (icons_win[0])
    add_info("Icons", "%s", icons_win);
  return;
#elif defined(__APPLE__)
  add_info("Icons", "System");
#else
  char qt[64] = "", gtk2[64] = "", gtk3[64] = "", out[256] = "";
  qt_icons(qt, sizeof(qt));
  read_gtk2_setting("gtk-icon-theme-name", gtk2, sizeof(gtk2));
  read_gtk3_setting("gtk-icon-theme-name", gtk3, sizeof(gtk3));
  append_tagged(out, sizeof(out), qt, "Qt");
  append_gtk_pair(out, sizeof(out), gtk2, gtk3);
  if (out[0])
    add_info("Icons", "%s", out);
#endif
}

static void gather_font(void) {
#ifdef _WIN32
  char font_win[128] = "";
  platform_gather_font(font_win, sizeof(font_win));
  if (font_win[0])
    add_info("Font", "%s", font_win);
  return;
#elif defined(__APPLE__)
  char font[128] = "";
  FILE *fp = popen("defaults read NSGlobalDomain AppleFont 2>/dev/null", "r");
  if (fp) {
    if (fgets(font, sizeof(font), fp)) {
      int len = strlen(font);
      while (len > 0 && (font[len-1]=='\n'||font[len-1]=='\r')) font[--len]='\0';
    }
    pclose(fp);
  }
  if (font[0])
    add_info("Font", "%s", font);
#else
  char qt[128] = "", gtk2[128] = "", gtk3[128] = "", out[448] = "";
  qt_font(qt, sizeof(qt));
  read_gtk2_setting("gtk-font-name", gtk2, sizeof(gtk2));
  read_gtk3_setting("gtk-font-name", gtk3, sizeof(gtk3));
  font_pt_format(gtk2, sizeof(gtk2));
  font_pt_format(gtk3, sizeof(gtk3));
  append_tagged(out, sizeof(out), qt, "Qt");
  append_gtk_pair(out, sizeof(out), gtk2, gtk3);
  if (out[0])
    add_info("Font", "%s", out);
#endif
}

static void gather_cursor(void) {
#ifdef _WIN32
  char cursor_win[64] = "";
  platform_gather_cursor(cursor_win, sizeof(cursor_win));
  if (cursor_win[0])
    add_info("Cursor", "%s", cursor_win);
  return;
#elif !defined(__APPLE__)
  char cursor[64] = "";
  read_gtk_setting("gtk-cursor-theme-name", cursor, sizeof(cursor));
  if (cursor[0]) {
    char size[16] = "";
    read_gtk_setting("gtk-cursor-theme-size", size, sizeof(size));
    if (size[0])
      add_info("Cursor", "%s (%spx)", cursor, size);
    else
      add_info("Cursor", "%s", cursor);
  }
#endif
}


// Sizing + layout, shared by startup and SIGWINCH so a resize reproduces
// the startup look instead of inheriting whatever the terminal height is.
// Wide terminals get the full 60-col canvas beside the info; medium ones
// shrink the canvas to keep the info intact; phone-width ones stack the
// info below the logo. Info lines clip instead of wrapping.
static void apply_layout(int show_info) {
  int rows = term_rows; 
  int cols = term_cols;
  if (rows > 1)
    rows--; // leave 1 row margin to prevent scroll-jitter

  // Ideal height: config/flag override > auto-fit to info lines > default
  int ideal = (int)(36 * size_scale);
  if (config_height > 0) {
    ideal = config_height;
  } else if (show_info && fetch_line_count > 0) {
    int info_height = fetch_line_count + 2;
    if (ideal < info_height)
      ideal = info_height;
  }
  if (ideal < 20)
    ideal = 20;
  if (ideal > MAX_HEIGHT)
    ideal = MAX_HEIGHT;

  render_height = ideal;
  anim_width = ANIM_WIDTH;
  layout_stacked = 0;
  info_clip_cols = -1;
  stacked_info_rows = 0;

  if (cols <= 0) { // no width info (not a tty): cap height only
    if (rows > 0 && render_height > rows)
      render_height = rows;
    logo_height = render_height;
    return;
  }

  int info_cols = 0;
  if (show_info)
    for (int i = 0; i < fetch_line_count; i++) {
      int w = visible_width(fetch_lines[i]);
      if (w > info_cols)
        info_cols = w;
    }

  if (show_info && cols < ANIM_WIDTH + GAP + info_cols) {
    // Full canvas + info doesn't fit: give the info what it needs and
    // shrink the canvas, unless that leaves the canvas unreadable —
    // then stack the info below a full-width logo instead.
    anim_width = cols - GAP - info_cols;
    if (anim_width < 20) {
      layout_stacked = 1;
      anim_width = cols < ANIM_WIDTH ? cols : ANIM_WIDTH;
    }
  } else if (anim_width > cols) {
    anim_width = cols;
  }

  // The projection needs ~5/3 the columns of its rows; shrink the logo on
  // narrow canvases instead of clipping its sides. Only the logo shrinks:
  // render_height keeps covering the info lines so none are dropped.
  int wcap = anim_width * 3 / 5;

  if (layout_stacked) {
    if (render_height > wcap)
      render_height = wcap;
    stacked_info_rows = fetch_line_count;
    if (rows > 0) {
      int budget = rows - fetch_line_count - 1; // 1 spacer row
      if (budget < render_height)
        render_height = budget;
      if (render_height < 6)
        render_height = 0; // too small to read: show info only
      int info_budget = rows - (render_height ? render_height + 1 : 0);
      if (stacked_info_rows > info_budget)
        stacked_info_rows = info_budget < 0 ? 0 : info_budget;
    }
    logo_height = render_height;
    info_clip_cols = cols;
  } else {
    if (show_info)
      info_clip_cols = cols - anim_width - GAP;
    if (rows > 0 && render_height > rows)
      render_height = rows;
    logo_height = render_height > wcap ? wcap : render_height;
    if (!show_info)
      render_height = logo_height; // no info: no point in blank rows
  }
}



static void get_alignment_padding(int* vertical, int* horizontal) {
  size_t max = 0;
  for (int i = 0; i < fetch_line_count; i++) {
    size_t len = visible_width(fetch_lines[i]);
    if (len > max) {
      max = len;
    }
  }

  switch (config_v_alignment) {
    default:
    case V_ALIGN_TOP:
      *vertical = 0;
      break;
    case V_ALIGN_CENTER:
      *vertical = term_rows / 2 - render_height / 2;
      break;
    case V_ALIGN_BOTTOM:
      *vertical = term_rows - (render_height);
      break;
  }

  switch (config_h_alignment) {
    default:
    case H_ALIGN_LEFT:
      *horizontal = 0;
      break;
    case H_ALIGN_CENTER:
      *horizontal = term_cols / 2 - (anim_width + GAP + max) / 2;
      break;
    case H_ALIGN_RIGHT:
      *horizontal= term_cols - (anim_width + GAP + max);
      break;
  }
}

int main(int argc, char **argv) {
  char distro[64] = "";
  const char *logo_name = NULL;
  int rotate_x = 1, rotate_y = 1;
  float speed = 1.0f;
  int show_info = 1;
  int use_color = 1;
  int max_frames = 2000;
  const char *shading = NULL;
  const char *shading_mode = NULL;
  int box_flag = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf(
          "Usage: fetch [options]\n\n"
          "Options:\n"
          "  -l, --logo <name>         Use a logo from fastfetch by name\n"
          "                            Any logo fastfetch supports works, "
          "e.g.:\n"
          "                              gentoo, arch, nixos, debian, ubuntu,\n"
          "                              fedora, void, alpine, opensuse, "
          "manjaro,\n"
          "                              proxmox, pop, linuxmint, "
          "endeavouros...\n"
          "                            Run 'fastfetch --list-logos' to see "
          "all\n"
          "  --rotate-x                Lock rotation to X axis only\n"
          "  --rotate-y                Lock rotation to Y axis only\n"
          "  -s, --speed <float>       Speed multiplier (default 1.0)\n"
          "  --size <float>            Scale the logo (e.g. 2.0 for double "
          "size)\n"
          "  --depth <float>           Scale the 3D depth (default 1.0)\n"
          "  --height <n>              Override render height in rows\n"
          "  --no-info                 Just the logo, no system info\n"
          "  --no-color                Disable logo coloring\n"
          "  --frames <n>              Stop after n frames (default 2000)\n"
          "  --infinite                Run forever (keypress or Ctrl-C to "
          "exit)\n"
          "  --shading-mode <mode>     ascii (.,-~:;=!*#$@, the default), or "
          "opt into\n"
          "                            sub-cell blocks: sextants (2x3) or "
          "blocks (2x2)\n"
          "  --shading-chars <str>     Custom shading ramp, supports UTF-8\n"
          "  --box                     Draw a border box around the info block\n"
          "  -V, --version             Show version\n"
          "  -h, --help                Show this help\n\n"
          "Config: ~/.config/fetch/config\n"
          "  List field names to show (in order), one per line.\n"
          "  Comment out or remove fields to hide them.\n"
          "  Available fields:\n"
          "    os, host, kernel, uptime, packages, shell, display, wm,\n"
          "    displaymanager, theme, icons, font, cursor, terminal, cpu,\n"
          "    gpu, memory, swap, disk, ip, battery, locale, colors\n\n"
          "  Separators and custom fields:\n"
          "    ---                      Blank line separator\n"
          "    custom_Label=value       Static field (e.g. custom_Pronouns=he/him)\n\n"
          "  Extra disks:\n"
          "    disk=/home               Show additional mount point\n"
          "    disk=/data               (repeat for multiple mounts)\n\n"
          "  Settings:\n"
          "    label_color=<color>      Label color (red, green, yellow, "
          "blue,\n"
          "                             magenta, cyan, white, or ANSI number)\n"
          "    separator=<char>         Title separator character\n"
          "    shading_mode=<mode>      ascii (default), blocks or sextants\n"
          "    shading=<str>            Shading ramp characters\n"
          "    light=<dir>              Light direction (top-left, top-right, "
          "top,\n"
          "                             left, right, front, bottom-left, "
          "bottom-right)\n"
          "    spin=<axes>              Rotation axes (x, y, or xy)\n"
          "    speed=<float>            Rotation speed\n"
          "    size=<float>             Logo scale\n"
          "    height=<n>               Render height in rows\n\n"
          "    box=<0/1>                Draw a border box around the info block\n\n"
          "Logo: ~/.config/fetch/logo.txt\n"
          "  Custom ASCII/Unicode logo. Add '# distro: <name>' as the\n"
          "  first line to set the color scheme.\n");
      return 0;
    } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
      printf("fetch %s \"%s\" (%s, %s)\n", FETCH_VERSION, FETCH_CODENAME,
             FETCH_ARCH, FETCH_OS);
      return 0;
    } else if (strcmp(argv[i], "--logo") == 0 || strcmp(argv[i], "-l") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "fetch: option '%s' requires an argument\n", argv[i]);
        return 1;
      }
      logo_name = argv[++i];
    } else if (strcmp(argv[i], "--rotate-x") == 0) {
      rotate_x = 1;
      rotate_y = 0;
    } else if (strcmp(argv[i], "--rotate-y") == 0) {
      rotate_x = 0;
      rotate_y = 1;
    } else if (strcmp(argv[i], "--speed") == 0 || strcmp(argv[i], "-s") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "fetch: option '%s' requires an argument\n", argv[i]);
        return 1;
      }
      speed = atof(argv[++i]);
    } else if (strcmp(argv[i], "--no-info") == 0) {
      show_info = 0;
    } else if (strcmp(argv[i], "--no-color") == 0) {
      use_color = 0;
    } else if (strcmp(argv[i], "--frames") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "fetch: option '%s' requires an argument\n", argv[i]);
        return 1;
      }
      max_frames = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--infinite") == 0) {
      max_frames = 0;
    } else if (strcmp(argv[i], "--shading-chars") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "fetch: option '%s' requires an argument\n", argv[i]);
        return 1;
      }
      shading = argv[++i];
    } else if (strcmp(argv[i], "--shading-mode") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "fetch: option '%s' requires an argument\n", argv[i]);
        return 1;
      }
      shading_mode = argv[++i];
    } else if (strcmp(argv[i], "--height") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "fetch: option '%s' requires an argument\n", argv[i]);
        return 1;
      }
      config_height = atoi(argv[++i]);
      if (config_height > MAX_HEIGHT)
        config_height = MAX_HEIGHT;
    } else if (strcmp(argv[i], "--size") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "fetch: option '%s' requires an argument\n", argv[i]);
        return 1;
      }
      size_scale = atof(argv[++i]);
      if (size_scale < 0.5f)
        size_scale = 0.5f;
      if (size_scale > 5.0f)
        size_scale = 5.0f;
    } else if (strcmp(argv[i], "--depth") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "fetch: option '%s' requires an argument\n", argv[i]);
        return 1;
      }
      config_depth = atof(argv[++i]);
      if (config_depth < 0.1f)
        config_depth = 0.1f;
      if (config_depth > 10.0f)
        config_depth = 10.0f;
      depth_user_set = 1;
    } else if (strcmp(argv[i], "--box") == 0) {
      box_flag = 1;
    } else {
      fprintf(stderr, "unknown option: %s\nTry 'fetch --help'\n", argv[i]);
      return 1;
    }
  }

  config_defaults();
  load_config();
  platform_get_term_size(&term_rows, &term_cols);

  // Shading: CLI flags, then config, then the ascii default
  if (!shading && config_shading[0])
    shading = config_shading;
  if (!shading_mode && config_shading_mode[0])
    shading_mode = config_shading_mode;
  if (!select_shading(shading_mode, shading)) {
    fprintf(stderr, "unknown shading mode: %s\nTry 'fetch --help'\n",
            shading_mode);
    return 1;
  }
  if (config_speed > 0 && speed == 1.0f)
    speed = config_speed;
  if (config_spin_x >= 0 && rotate_x == 1 && rotate_y == 1) {
    rotate_x = config_spin_x;
    rotate_y = config_spin_y;
  }
  if (box_flag)
    config_box = 1;

  if (logo_name) {
    if (logo_load_builtin(&g_logo, logo_name)) {
      logo_sync_to_globals(&g_logo);
    } else {
      int got = load_logo_fastfetch(logo_name);
      if (!got) {
        load_default_logo();
      }
    }
    strncpy(distro, logo_name, sizeof(distro) - 1);
  } else {
    // Try logo.txt first
    int has_custom_logo = load_logo_file();
    if (file_distro[0])
      strncpy(distro, file_distro, sizeof(distro) - 1);
    else
      detect_distro(distro, sizeof(distro));

    // Only try fastfetch if no custom logo.txt was loaded
    int got_logo = has_custom_logo;
    if (!got_logo && distro[0]) {
      if (logo_load_builtin(&g_logo, distro)) {
        logo_sync_to_globals(&g_logo);
        got_logo = 1;
      } else {
        got_logo = load_logo_fastfetch(distro);
        if (!got_logo && distro_id_like[0]) {
          char like_copy[64];
          strncpy(like_copy, distro_id_like, sizeof(like_copy) - 1);
          like_copy[sizeof(like_copy) - 1] = '\0';
          char *tok = strtok(like_copy, " ");
          while (tok && !got_logo) {
            got_logo = load_logo_fastfetch(tok);
            if (got_logo)
              strncpy(distro, tok, sizeof(distro) - 1);
            tok = strtok(NULL, " ");
          }
        }
      }
    }
    if (!got_logo && logo_rows == 0) {
      if (distro[0] && logo_load_builtin(&g_logo, distro)) {
        logo_sync_to_globals(&g_logo);
        got_logo = 1;
      } else {
        load_default_logo();
#ifndef _WIN32
        if (distro[0] && strcasecmp(distro, "gentoo") != 0)
          fprintf(stderr,
                  "fetch: couldn't load %s logo (is fastfetch installed?). "
                  "using built-in gentoo logo.\n",
                  distro);
#endif
      }
    }
  }

  // Process logo into codepoint cells
  process_logo();

  if (distro[0])
    set_distro_colors(distro);
  if (config_logo_outer[0])
    color_outer = config_logo_outer;
  if (config_logo_inner[0])
    color_inner = config_logo_inner;

  typedef void (*gather_fn)(void);
  gather_fn fns[F_COUNT] = {
      [F_OS] = gather_os,
      [F_HOST] = gather_host,
      [F_KERNEL] = gather_kernel,
      [F_UPTIME] = gather_uptime,
      [F_PACKAGES] = gather_packages,
      [F_SHELL] = gather_shell,
      [F_DISPLAY] = gather_display,
      [F_WM] = gather_wm,
      [F_DISPLAYMANAGER] = gather_displaymanager,
      [F_THEME] = gather_theme,
      [F_ICONS] = gather_icons,
      [F_FONT] = gather_font,
      [F_CURSOR] = gather_cursor,
      [F_TERMINAL] = gather_terminal,
      [F_CPU] = gather_cpu,
      [F_GPU] = gather_gpu,
      [F_MEMORY] = gather_memory,
      [F_SWAP] = gather_swap,
      [F_DISK] = gather_disk,
      [F_IP] = gather_ip,
      [F_BATTERY] = gather_battery,
      [F_LOCALE] = gather_locale,
      [F_COLORS] = NULL,
  };

  for (int i = 0; i < F_COUNT; i++)
    field_line[i] = -1;

  if (show_info) {
    gather_title();
    for (int i = 0; i < field_count; i++) {
      int id = field_order[i];
      if (id == F_SEPARATOR) {
        add_line("");
      } else if (id >= F_CUSTOM_BASE && id < F_CUSTOM_BASE + MAX_CUSTOM) {
        int ci = id - F_CUSTOM_BASE;
        add_info(custom_label[ci], "%s", custom_value[ci]);
      } else if (id == F_COLORS) {
        add_line("");
        add_line("\033[40m   \033[41m   \033[42m   \033[43m   "
                 "\033[44m   \033[45m   \033[46m   \033[47m   \033[0m");
        add_line("\033[100m   \033[101m   \033[102m   \033[103m   "
                 "\033[104m   \033[105m   \033[106m   \033[107m   \033[0m");
      } else if (id < F_COUNT && fns[id]) {
        current_field = id;
        fns[id]();
      }
    }
    current_field = -1;
  }
  if (config_box)
    box_wrap_lines();
  apply_layout(show_info);

  build_points();

  float A = 0.0f;
  float B = 0.0f;
  float K1 = 37.0f * logo_height / 36.0f * size_scale;
  const float K2 = 5.5f;

  // Compute the face-on (A=0, B=0) projection extent from the point
  // cloud. This is deterministic and sets a fixed frame height.
  float face_up = 0, face_dn = 0;
  for (int i = 0; i < POINT_COUNT; i++) {
    float zc = PZ[i] + K2;
    if (zc < 0.1f) continue;
    float ys = K1 * PY[i] / zc;
    if (ys > face_up) face_up = ys;
    if (-ys > face_dn) face_dn = -ys;
  }
  // Place y_center so the logo top lands at row 1 (row 0 is padding)
  float fixed_y_center = face_up + 1.0f;

  float hlx, hly, hlz;
  render_compute_half_vector(light_x, light_y, light_z, &hlx, &hly, &hlz);

  platform_term_caps_t caps;
  platform_terminal_init(&caps);

  int fetch_start = show_info ? 1 : 0;
  // Tighten render_height to fit the face-on logo + info,
  // but not when the user explicitly set --height
  if (config_height == 0) {
    int logo_bottom = (int)(fixed_y_center + face_dn) + 2;
    int info_bottom = show_info ? fetch_start + fetch_line_count + 1 : 0;
    int needed = logo_bottom > info_bottom ? logo_bottom : info_bottom;
    if (needed < render_height)
      render_height = needed;
  }

  int mouse_dragging = 0;
  float drag_vx = 0.0f, drag_vy = 0.0f;

  for (int frame = 0; max_frames == 0 || frame < max_frames; frame++) {
    if (platform_is_interrupted())
      break;

    int should_break = 0;
    platform_mouse_event_t mev;
    platform_input_event_t iev;
    while ((iev = platform_poll_input(&mev)) != INPUT_NONE) {
      if (iev == INPUT_EXIT_KEY) {
        should_break = 1;
        break;
      }
      if (iev == INPUT_MOUSE_DRAG) {
        mouse_dragging = 1;
        drag_vy = -mev.dx * 0.03f;
        drag_vx = -mev.dy * 0.03f;
        B += drag_vy;
        A += drag_vx;
      } else if (iev == INPUT_MOUSE_UP) {
        mouse_dragging = 0;
      }
    }
    if (should_break || platform_is_interrupted())
      break;

    // Handle terminal resize: recompute the same layout as startup
    if (platform_check_resize()) {
      platform_get_term_size(&term_rows, &term_cols);
      int old_h = render_height, old_w = anim_width;
      int old_stacked = layout_stacked, old_clip = info_clip_cols;
      apply_layout(show_info);
      if (render_height != old_h || anim_width != old_w ||
          layout_stacked != old_stacked || info_clip_cols != old_clip) {
        K1 = 37.0f * logo_height / 36.0f * size_scale;
        platform_write_output("\033[2J", 4);
      }
    }
    // Refresh fast dynamic fields every ~1 second (20 frames).
    // Only uptime/memory/swap — they're pure /proc reads, no popen,
    // so they don't hitch the animation. Battery/disk/ip use popen and
    // stay static (user can restart to refresh those).
    if (show_info && frame > 0 && frame % 20 == 0) {
      is_refresh_pass = 1;
      if (field_line[F_UPTIME] >= 0) {
        current_field = F_UPTIME;
        gather_uptime();
      }
      if (field_line[F_MEMORY] >= 0) {
        current_field = F_MEMORY;
        gather_memory();
      }
      if (field_line[F_SWAP] >= 0) {
        current_field = F_SWAP;
        gather_swap();
      }
      current_field = -1;
      is_refresh_pass = 0;
    }

    clear_buf();
    if (!mouse_dragging) {
      if (drag_vx != 0.0f || drag_vy != 0.0f) {
        A += drag_vx;
        B += drag_vy;
      } else {
        A += rotate_x ? 0.04f * speed : 0.0f;
        B += rotate_y ? 0.06f * speed : 0.0f;
      }
    }
    const float lx = light_x, ly = light_y, lz = light_z;
    const float y_center = (!layout_stacked && show_info)
                              ? fixed_y_center
                              : render_height * 0.5f;
    const int smax = shading_count - 1;
    const int aw = anim_width;

    render_project_points(A, B, K1, K2, y_center, aw, render_height,
                          lx, ly, lz, hlx, hly, hlz);

    int top_padding;
    int left_padding;
    get_alignment_padding(&top_padding, &left_padding);

    // Batch entire frame into a single write (reuse buffer across frames)
    static char *out_buf = NULL;
    static size_t out_cap = 0;
    size_t need = (render_height + stacked_info_rows + 2) * (2048u + left_padding) + 64 + top_padding;
    if (need > out_cap) {
      free(out_buf);
      out_buf = malloc(need);
      out_cap = out_buf ? need : 0;
    }
    if (!out_buf) {
      platform_terminal_cleanup();
      return 1;
    }
    char *p = out_buf;
    char *end = out_buf + need - 8;
    *p++ = '\033'; *p++ = '[';
    *p++ = 'H';

    // Pre-formatted reset + newline sequences
    const char reset_seq[] = "\033[0m";
    const char clr_seq[] = "\033[K";

    // Pre-formatted common ANSI color escape prefix: \033[1;XXm
    // c is in 30-37 or 90-97, pre-encode the 6-byte prefix (cached across frames)
    static char ansi_tbl[128][8];
    static int ansi_len[128];
    int inner_len = strlen(color_inner);
    int outer_len = strlen(color_outer);

    if (top_padding > 0) {
      memset(p, '\n', top_padding);
      p += top_padding;
    }

    for (int i = 0; i < render_height && p + 8 < end; i++) {
      if (left_padding > 0) {
        memset(p, ' ', left_padding);
        p += left_padding;
      }

      if (!use_color) {
        for (int j = 0; j < aw && p + 8 < end; j++) {
          int c = 0;
          const char *sc = cell_glyph(i, j, smax, &c);
          if (!sc) { *p++ = ' '; continue; }
          int k = 0;
          while (k < 4 && sc[k]) { p[k] = sc[k]; k++; }
          p += k ? k : 1;
        }
      } else {
        int prev_color = -1;
        for (int j = 0; j < aw && p + 16 < end; j++) {
          int c = 0;
          const char *sc = cell_glyph(i, j, smax, &c);
          if (!sc) {
            if (prev_color != -1) {
              memcpy(p, reset_seq, 4); p += 4;
              prev_color = -1;
            }
            *p++ = ' ';
          } else {
            if (c != prev_color) {
              if (logo_has_ansi && c > 0 && c < 128) {
                // Build ANSI escape lazily on first use
                if (!ansi_len[c]) {
                  ansi_len[c] = snprintf(ansi_tbl[c], sizeof(ansi_tbl[c]),
                                         "\033[1;%dm", c);
                }
                memcpy(p, ansi_tbl[c], ansi_len[c]);
                p += ansi_len[c];
              } else {
                const char *cs = (c == 1) ? color_inner : color_outer;
                int clen = (c == 1) ? inner_len : outer_len;
                memcpy(p, cs, clen); p += clen;
              }
              prev_color = c;
            }
            int k = 0;
            while (k < 4 && sc[k]) { p[k] = sc[k]; k++; }
            p += k ? k : 1;
          }
        }
        if (prev_color != -1 && p + 4 < end) {
          memcpy(p, reset_seq, 4); p += 4;
        }
      }

      int fi = i - fetch_start;
      if (!layout_stacked && fi >= 0 && fi < fetch_line_count &&
          p + GAP + 4 < end) {
        memset(p, ' ', GAP); p += GAP;
        p = emit_clipped(p, end, fetch_lines[fi], info_clip_cols);
      }

      // Erase to end of line + newline
      if (p + 8 >= end) break;
      memcpy(p, clr_seq, 4); p += 4;
      *p++ = '\n';
    }
    if (layout_stacked && stacked_info_rows > 0) {
      if (render_height > 0 && p + 8 < end) {
        memcpy(p, clr_seq, 4); p += 4;
        *p++ = '\n';
      }
      for (int i = 0; i < stacked_info_rows && p + 16 < end; i++) {
        p = emit_clipped(p, end, fetch_lines[i], info_clip_cols);
        memcpy(p, clr_seq, 4); p += 4;
        *p++ = '\n';
      }
    }
    if (platform_write_output(out_buf, p - out_buf) < 0)
      break;
    platform_sleep_frame(50000);
  }

  platform_terminal_cleanup();
  return 0;
}
