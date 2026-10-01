#ifndef FETCH_PLATFORM_H
#define FETCH_PLATFORM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Terminal Capabilities & Lifecycle --- */

typedef struct {
  int is_tty;          /* 1 if running in an interactive terminal */
  int supports_vt;     /* 1 if VT100 / ANSI escape processing is supported */
  int supports_mouse;  /* 1 if SGR mouse reporting is supported */
} platform_term_caps_t;

/**
 * Initialize platform terminal into raw/non-canonical mode, configure
 * signal-safe interrupt/resize handlers, hide cursor, and enable mouse tracking.
 * Safe to call at startup. Returns 0 on success, negative on error.
 */
int platform_terminal_init(platform_term_caps_t *caps);

/**
 * Restore terminal modes, mouse reporting, and cursor visibility.
 * Guaranteed to be idempotent and safe to call multiple times.
 */
void platform_terminal_cleanup(void);

/**
 * Query current terminal size in rows and columns.
 * Sets *rows and *cols to 0 if dimensions cannot be determined.
 */
void platform_get_term_size(int *rows, int *cols);

/**
 * Check if a terminal resize occurred since the last check.
 * Returns 1 if resized (and clears the resize flag), 0 otherwise.
 */
int platform_check_resize(void);

/**
 * Sleep for the specified duration in microseconds between animation frames.
 */
void platform_sleep_frame(unsigned int usec);

/* --- Atomic Frame Output --- */

/**
 * Write a formatted buffer directly to the terminal output.
 * Returns number of bytes written, or negative on write error.
 */
int platform_write_output(const char *buf, size_t len);

/* --- Input & Event Handling --- */

typedef enum {
  INPUT_NONE = 0,
  INPUT_EXIT_KEY,    /* Normal keypress triggering exit (not consumed from tty) */
  INPUT_MOUSE_DRAG,  /* Mouse drag event with relative deltas */
  INPUT_MOUSE_UP     /* Mouse button release */
} platform_input_event_t;

typedef struct {
  int btn;           /* Button index (0 = left, 32 = drag) */
  int x, y;          /* 1-based column and row coordinates */
  int dx, dy;        /* Relative column and row deltas since last drag event */
} platform_mouse_event_t;

/**
 * Poll for pending user input.
 * If a regular keypress is pending, it returns INPUT_EXIT_KEY without consuming
 * the byte from the terminal buffer, allowing the calling shell to receive it.
 * SGR mouse escapes are parsed and return INPUT_MOUSE_DRAG or INPUT_MOUSE_UP.
 * Returns INPUT_NONE if no events are pending.
 */
platform_input_event_t platform_poll_input(platform_mouse_event_t *mouse_event);

/**
 * Check if the program has received an interrupt signal (SIGINT or SIGTERM).
 * Returns 1 if interrupted, 0 otherwise.
 */
int platform_is_interrupted(void);

/* --- Platform Paths & OS Identification --- */

/**
 * Resolve the platform-specific default path to the config file.
 * (e.g. $XDG_CONFIG_HOME/fetch/config or ~/.config/fetch/config)
 */
void platform_get_config_path(char *out, size_t outsz);

/**
 * Resolve the platform-specific default path to the custom logo file.
 * (e.g. $XDG_CONFIG_HOME/fetch/logo.txt or ~/.config/fetch/logo.txt)
 */
void platform_get_logo_path(char *out, size_t outsz);

/**
 * Detect the OS or distribution identifier string (e.g. "arch", "ubuntu", "darwin").
 * Returns 1 if detected, 0 otherwise.
 */
int platform_detect_os_id(char *out, size_t outsz);

/* --- System Information Gathers --- */

typedef void (*platform_emit_info_cb)(const char *label, const char *fmt, ...);

void platform_gather_title(char *out_user, size_t usersz, char *out_host, size_t hostsz);
void platform_gather_os(char *out, size_t outsz);
void platform_gather_host(char *out, size_t outsz);
void platform_gather_kernel(char *out, size_t outsz);
void platform_gather_uptime(char *out, size_t outsz);
void platform_gather_packages(char *out, size_t outsz);
void platform_gather_shell(char *out, size_t outsz);
void platform_gather_display(platform_emit_info_cb emit_cb);
void platform_gather_wm(char *out, size_t outsz);
void platform_gather_displaymanager(char *out, size_t outsz);
void platform_gather_theme(char *out, size_t outsz);
void platform_gather_icons(char *out, size_t outsz);
void platform_gather_font(char *out, size_t outsz);
void platform_gather_cursor(char *out, size_t outsz);
void platform_gather_terminal(char *out, size_t outsz);
void platform_gather_cpu(char *out, size_t outsz);
void platform_gather_gpu(platform_emit_info_cb emit_cb);
void platform_gather_memory(char *out, size_t outsz);
void platform_gather_swap(char *out, size_t outsz);
void platform_gather_disk(const char *path, char *out, size_t outsz);
void platform_gather_ip(platform_emit_info_cb emit_cb);
void platform_gather_battery(char *out_label, size_t labelsz, char *out_val, size_t valsz);
void platform_gather_locale(char *out, size_t outsz);

/**
 * Gather the active power/energy profile name.
 * On Windows this maps the active power scheme GUID to a friendly name
 * (e.g. "Balanced", "High performance", "Power saver"). Writes an empty
 * string when no profile can be determined.
 */
void platform_gather_powerprofile(char *out, size_t outsz);

/**
 * Invalidate cached static system information, allowing refresh on next query.
 */
void platform_invalidate_info_cache(void);

#ifdef FETCH_TESTING
/* --- Test Control Helpers (guarded for test harness only) --- */
platform_input_event_t platform_parse_input_chunk(const char *buf, size_t len,
                                                  platform_mouse_event_t *mouse_event,
                                                  size_t *consumed);
void platform_set_interrupted_for_test(int val);
void platform_set_resized_for_test(int val);
void platform_reset_input_state_for_test(void);
void platform_set_pending_bytes_for_test(int count);
int  platform_get_pending_bytes_for_test(void);
int  platform_is_ctrl_handler_registered_for_test(void);
int  platform_get_query_count_for_test(const char *name);
int  platform_is_field_cached_for_test(const char *name);
void platform_reset_query_counts_for_test(void);
int  platform_run_command(const char *cmd, char *out, size_t outsz, unsigned int timeout_ms);
int  platform_count_dir_packages(const char *path);
int  platform_parse_winget_output(const char *output);
void platform_format_packages_string(char *out, size_t outsz, int winget, int scoop, int choco);
#endif

#ifdef __cplusplus
}
#endif

#endif /* FETCH_PLATFORM_H */
