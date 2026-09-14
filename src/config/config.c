#include "src/config/config.h"
#include "src/core/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

// Global config instance
fetch_config_t g_config;

// Backward-compatibility global variables
int field_enabled[F_COUNT];
int field_order[MAX_FIELDS];
int field_count = 0;
char custom_label[MAX_CUSTOM][64];
char custom_value[MAX_CUSTOM][256];
int custom_count = 0;
char label_color[16] = "35";
int config_height = 0;
float size_scale = 1.0f;
float config_speed = 0.0f;
int config_spin_x = -1;
int config_spin_y = -1;
int config_box = 0;
char config_shading[128] = "";
char config_shading_mode[16] = "";
char config_separator[8] = "-";
float config_depth = 1.0f;
int depth_user_set = 0;
char config_logo_outer[32] = "";
char config_logo_inner[32] = "";
char extra_disks[MAX_EXTRA_DISKS][128];
int extra_disk_count = 0;
enum v_alignment config_v_alignment = V_ALIGN_TOP;
enum h_alignment config_h_alignment = H_ALIGN_LEFT;
float light_x = 0.4082f, light_y = 0.8165f, light_z = -0.4082f;

static const struct {
  const char *name;
  int id;
} field_map[] = {{"os", F_OS},
                 {"host", F_HOST},
                 {"kernel", F_KERNEL},
                 {"uptime", F_UPTIME},
                 {"packages", F_PACKAGES},
                 {"shell", F_SHELL},
                 {"display", F_DISPLAY},
                 {"wm", F_WM},
                 {"displaymanager", F_DISPLAYMANAGER},
                 {"theme", F_THEME},
                 {"icons", F_ICONS},
                 {"font", F_FONT},
                 {"cursor", F_CURSOR},
                 {"terminal", F_TERMINAL},
                 {"cpu", F_CPU},
                 {"gpu", F_GPU},
                 {"memory", F_MEMORY},
                 {"swap", F_SWAP},
                 {"disk", F_DISK},
                 {"ip", F_IP},
                 {"battery", F_BATTERY},
                 {"locale", F_LOCALE},
                 {"colors", F_COLORS},
                 {NULL, 0}};

int config_field_id_from_name(const char *name) {
  if (!name) return -1;
  for (int i = 0; field_map[i].name; i++) {
    if (strcasecmp(name, field_map[i].name) == 0) {
      return field_map[i].id;
    }
  }
  return -1;
}

const char *config_field_name_from_id(int id) {
  if (id < 0 || id >= F_COUNT) return NULL;
  for (int i = 0; field_map[i].name; i++) {
    if (field_map[i].id == id) {
      return field_map[i].name;
    }
  }
  return NULL;
}

void config_sync_to_globals(const fetch_config_t *cfg) {
  if (!cfg) return;
  for (int i = 0; i < F_COUNT; i++) {
    field_enabled[i] = cfg->field_enabled[i];
  }
  for (int i = 0; i < cfg->field_count && i < MAX_FIELDS; i++) {
    field_order[i] = cfg->field_order[i];
  }
  field_count = cfg->field_count;
  custom_count = cfg->custom_count;
  for (int i = 0; i < cfg->custom_count && i < MAX_CUSTOM; i++) {
    strncpy(custom_label[i], cfg->custom_label[i], sizeof(custom_label[0]) - 1);
    custom_label[i][sizeof(custom_label[0]) - 1] = '\0';
    strncpy(custom_value[i], cfg->custom_value[i], sizeof(custom_value[0]) - 1);
    custom_value[i][sizeof(custom_value[0]) - 1] = '\0';
  }
  strncpy(label_color, cfg->label_color, sizeof(label_color) - 1);
  label_color[sizeof(label_color) - 1] = '\0';
  config_height = cfg->config_height;
  size_scale = cfg->size_scale;
  config_speed = cfg->config_speed;
  config_spin_x = cfg->config_spin_x;
  config_spin_y = cfg->config_spin_y;
  config_box = cfg->config_box;
  strncpy(config_shading, cfg->config_shading, sizeof(config_shading) - 1);
  config_shading[sizeof(config_shading) - 1] = '\0';
  strncpy(config_shading_mode, cfg->config_shading_mode, sizeof(config_shading_mode) - 1);
  config_shading_mode[sizeof(config_shading_mode) - 1] = '\0';
  strncpy(config_separator, cfg->config_separator, sizeof(config_separator) - 1);
  config_separator[sizeof(config_separator) - 1] = '\0';
  config_depth = cfg->config_depth;
  depth_user_set = cfg->depth_user_set;
  strncpy(config_logo_outer, cfg->config_logo_outer, sizeof(config_logo_outer) - 1);
  config_logo_outer[sizeof(config_logo_outer) - 1] = '\0';
  strncpy(config_logo_inner, cfg->config_logo_inner, sizeof(config_logo_inner) - 1);
  config_logo_inner[sizeof(config_logo_inner) - 1] = '\0';
  extra_disk_count = cfg->extra_disk_count;
  for (int i = 0; i < cfg->extra_disk_count && i < MAX_EXTRA_DISKS; i++) {
    strncpy(extra_disks[i], cfg->extra_disks[i], sizeof(extra_disks[0]) - 1);
    extra_disks[i][sizeof(extra_disks[0]) - 1] = '\0';
  }
  config_v_alignment = cfg->config_v_alignment;
  config_h_alignment = cfg->config_h_alignment;
  light_x = cfg->light_x;
  light_y = cfg->light_y;
  light_z = cfg->light_z;
}

void config_sync_from_globals(fetch_config_t *cfg) {
  if (!cfg) return;
  for (int i = 0; i < F_COUNT; i++) {
    cfg->field_enabled[i] = field_enabled[i];
  }
  for (int i = 0; i < field_count && i < MAX_FIELDS; i++) {
    cfg->field_order[i] = field_order[i];
  }
  cfg->field_count = field_count;
  cfg->custom_count = custom_count;
  for (int i = 0; i < custom_count && i < MAX_CUSTOM; i++) {
    strncpy(cfg->custom_label[i], custom_label[i], sizeof(cfg->custom_label[0]) - 1);
    cfg->custom_label[i][sizeof(cfg->custom_label[0]) - 1] = '\0';
    strncpy(cfg->custom_value[i], custom_value[i], sizeof(cfg->custom_value[0]) - 1);
    cfg->custom_value[i][sizeof(cfg->custom_value[0]) - 1] = '\0';
  }
  strncpy(cfg->label_color, label_color, sizeof(cfg->label_color) - 1);
  cfg->label_color[sizeof(cfg->label_color) - 1] = '\0';
  cfg->config_height = config_height;
  cfg->size_scale = size_scale;
  cfg->config_speed = config_speed;
  cfg->config_spin_x = config_spin_x;
  cfg->config_spin_y = config_spin_y;
  cfg->config_box = config_box;
  strncpy(cfg->config_shading, config_shading, sizeof(cfg->config_shading) - 1);
  cfg->config_shading[sizeof(cfg->config_shading) - 1] = '\0';
  strncpy(cfg->config_shading_mode, config_shading_mode, sizeof(cfg->config_shading_mode) - 1);
  cfg->config_shading_mode[sizeof(cfg->config_shading_mode) - 1] = '\0';
  strncpy(cfg->config_separator, config_separator, sizeof(cfg->config_separator) - 1);
  cfg->config_separator[sizeof(cfg->config_separator) - 1] = '\0';
  cfg->config_depth = config_depth;
  cfg->depth_user_set = depth_user_set;
  strncpy(cfg->config_logo_outer, config_logo_outer, sizeof(cfg->config_logo_outer) - 1);
  cfg->config_logo_outer[sizeof(cfg->config_logo_outer) - 1] = '\0';
  strncpy(cfg->config_logo_inner, config_logo_inner, sizeof(cfg->config_logo_inner) - 1);
  cfg->config_logo_inner[sizeof(cfg->config_logo_inner) - 1] = '\0';
  cfg->extra_disk_count = extra_disk_count;
  for (int i = 0; i < extra_disk_count && i < MAX_EXTRA_DISKS; i++) {
    strncpy(cfg->extra_disks[i], extra_disks[i], sizeof(cfg->extra_disks[0]) - 1);
    cfg->extra_disks[i][sizeof(cfg->extra_disks[0]) - 1] = '\0';
  }
  cfg->config_v_alignment = config_v_alignment;
  cfg->config_h_alignment = config_h_alignment;
  cfg->light_x = light_x;
  cfg->light_y = light_y;
  cfg->light_z = light_z;
}

void config_init_defaults(fetch_config_t *cfg) {
  if (!cfg) return;
  memset(cfg, 0, sizeof(*cfg));

  // Default order
  int defaults[] = {
      F_OS,     F_HOST,  F_KERNEL, F_UPTIME,  F_PACKAGES, F_SHELL,    F_DISPLAY,
      F_WM,     F_THEME, F_ICONS,  F_FONT,    F_CURSOR,   F_TERMINAL, F_CPU,
      F_GPU,    F_MEMORY, F_SWAP,  F_DISK,    F_IP,       F_BATTERY,  F_LOCALE,
      F_COLORS};
  cfg->field_count = sizeof(defaults) / sizeof(defaults[0]);
  for (int i = 0; i < cfg->field_count; i++) {
    cfg->field_order[i] = defaults[i];
    cfg->field_enabled[defaults[i]] = 1;
  }

  strcpy(cfg->label_color, "35"); // default magenta
  cfg->config_height = 0;
  cfg->size_scale = 1.0f;
  cfg->config_speed = 0.0f;
  cfg->config_spin_x = -1;
  cfg->config_spin_y = -1;
  cfg->config_box = 0;
  cfg->config_shading[0] = '\0';
  cfg->config_shading_mode[0] = '\0';
  strcpy(cfg->config_separator, "-");
  cfg->config_depth = 1.0f;
  cfg->depth_user_set = 0;
  cfg->config_logo_outer[0] = '\0';
  cfg->config_logo_inner[0] = '\0';
  cfg->extra_disk_count = 0;
  cfg->config_v_alignment = V_ALIGN_TOP;
  cfg->config_h_alignment = H_ALIGN_LEFT;
  cfg->light_x = 0.4082f;
  cfg->light_y = 0.8165f;
  cfg->light_z = -0.4082f;
}

int config_parse_line(fetch_config_t *cfg, const char *raw_line) {
  if (!cfg || !raw_line) return 0;

  char buf[512];
  strncpy(buf, raw_line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  int len = strlen(buf);
  while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
                     buf[len - 1] == ' '  || buf[len - 1] == '\t'))
    buf[--len] = '\0';

  // Skip comments and empty lines
  char *line = buf;
  while (*line == ' ' || *line == '\t')
    line++;
  if (*line == '#' || *line == '\0')
    return 0;

  // Check for key=value settings
  if (strncmp(line, "label_color=", 12) == 0) {
    char *val = line + 12;
    strip_inline_hint(val);
    // Accept color names or numbers
    if (strcmp(val, "red") == 0)
      strcpy(cfg->label_color, "31");
    else if (strcmp(val, "green") == 0)
      strcpy(cfg->label_color, "32");
    else if (strcmp(val, "yellow") == 0)
      strcpy(cfg->label_color, "33");
    else if (strcmp(val, "blue") == 0)
      strcpy(cfg->label_color, "34");
    else if (strcmp(val, "magenta") == 0)
      strcpy(cfg->label_color, "35");
    else if (strcmp(val, "cyan") == 0)
      strcpy(cfg->label_color, "36");
    else if (strcmp(val, "white") == 0)
      strcpy(cfg->label_color, "37");
    else
      strncpy(cfg->label_color, val, sizeof(cfg->label_color) - 1);
    return 1;
  }

  if (strncmp(line, "height=", 7) == 0) {
    char *val = line + 7;
    strip_inline_hint(val);
    cfg->config_height = atoi(val);
    if (cfg->config_height > MAX_HEIGHT)
      cfg->config_height = MAX_HEIGHT;
    return 1;
  }

  if (strncmp(line, "size=", 5) == 0) {
    char *val = line + 5;
    strip_inline_hint(val);
    cfg->size_scale = atof(val);
    if (cfg->size_scale < 0.5f)
      cfg->size_scale = 0.5f;
    if (cfg->size_scale > 5.0f)
      cfg->size_scale = 5.0f;
    return 1;
  }

  if (strncmp(line, "speed=", 6) == 0) {
    char *val = line + 6;
    strip_inline_hint(val);
    cfg->config_speed = atof(val);
    return 1;
  }

  if (strncmp(line, "spin=", 5) == 0) {
    char *val = line + 5;
    strip_inline_hint(val);
    cfg->config_spin_x = (strchr(val, 'x') || strchr(val, 'X')) ? 1 : 0;
    cfg->config_spin_y = (strchr(val, 'y') || strchr(val, 'Y')) ? 1 : 0;
    return 1;
  }

  if (strncmp(line, "box=", 4) == 0) {
    char *val = line + 4;
    strip_inline_hint(val);
    cfg->config_box = (strcmp(val, "1") == 0 || strcasecmp(val, "y") == 0 ||
                       strcasecmp(val, "yes") == 0 || strcasecmp(val, "true") == 0)
                          ? 1
                          : 0;
    return 1;
  }

  if (strncmp(line, "shading=", 8) == 0) {
    char *val = line + 8; // note: no strip_inline_hint() here to allow freeform shading strings
    strncpy(cfg->config_shading, val, sizeof(cfg->config_shading) - 1);
    cfg->config_shading[sizeof(cfg->config_shading) - 1] = '\0';
    return 1;
  }

  if (strncmp(line, "shading_mode=", 13) == 0) {
    char *val = line + 13;
    strip_inline_hint(val);
    strncpy(cfg->config_shading_mode, val, sizeof(cfg->config_shading_mode) - 1);
    cfg->config_shading_mode[sizeof(cfg->config_shading_mode) - 1] = '\0';
    return 1;
  }

  if (strncmp(line, "separator=", 10) == 0) {
    char *val = line + 10; // note: no strip_inline_hint() here to allow freeform separator strings
    strncpy(cfg->config_separator, val, sizeof(cfg->config_separator) - 1);
    cfg->config_separator[sizeof(cfg->config_separator) - 1] = '\0';
    return 1;
  }

  if (strncmp(line, "depth=", 6) == 0) {
    char *val = line + 6;
    strip_inline_hint(val);
    cfg->config_depth = atof(val);
    if (cfg->config_depth < 0.1f) cfg->config_depth = 0.1f;
    if (cfg->config_depth > 10.0f) cfg->config_depth = 10.0f;
    cfg->depth_user_set = 1;
    return 1;
  }

  if (strncmp(line, "logo_outer=", 11) == 0) {
    char *val = line + 11;
    strip_inline_hint(val);
    if (strcmp(val, "red") == 0) snprintf(cfg->config_logo_outer, sizeof(cfg->config_logo_outer), "\033[1;31m");
    else if (strcmp(val, "green") == 0) snprintf(cfg->config_logo_outer, sizeof(cfg->config_logo_outer), "\033[1;32m");
    else if (strcmp(val, "yellow") == 0) snprintf(cfg->config_logo_outer, sizeof(cfg->config_logo_outer), "\033[1;33m");
    else if (strcmp(val, "blue") == 0) snprintf(cfg->config_logo_outer, sizeof(cfg->config_logo_outer), "\033[1;34m");
    else if (strcmp(val, "magenta") == 0) snprintf(cfg->config_logo_outer, sizeof(cfg->config_logo_outer), "\033[1;35m");
    else if (strcmp(val, "cyan") == 0) snprintf(cfg->config_logo_outer, sizeof(cfg->config_logo_outer), "\033[1;36m");
    else if (strcmp(val, "white") == 0) snprintf(cfg->config_logo_outer, sizeof(cfg->config_logo_outer), "\033[1;37m");
    else snprintf(cfg->config_logo_outer, sizeof(cfg->config_logo_outer), "\033[1;%sm", val);
    return 1;
  }

  if (strncmp(line, "logo_inner=", 11) == 0) {
    char *val = line + 11;
    strip_inline_hint(val);
    if (strcmp(val, "red") == 0) snprintf(cfg->config_logo_inner, sizeof(cfg->config_logo_inner), "\033[1;31m");
    else if (strcmp(val, "green") == 0) snprintf(cfg->config_logo_inner, sizeof(cfg->config_logo_inner), "\033[1;32m");
    else if (strcmp(val, "yellow") == 0) snprintf(cfg->config_logo_inner, sizeof(cfg->config_logo_inner), "\033[1;33m");
    else if (strcmp(val, "blue") == 0) snprintf(cfg->config_logo_inner, sizeof(cfg->config_logo_inner), "\033[1;34m");
    else if (strcmp(val, "magenta") == 0) snprintf(cfg->config_logo_inner, sizeof(cfg->config_logo_inner), "\033[1;35m");
    else if (strcmp(val, "cyan") == 0) snprintf(cfg->config_logo_inner, sizeof(cfg->config_logo_inner), "\033[1;36m");
    else if (strcmp(val, "white") == 0) snprintf(cfg->config_logo_inner, sizeof(cfg->config_logo_inner), "\033[1;37m");
    else snprintf(cfg->config_logo_inner, sizeof(cfg->config_logo_inner), "\033[1;%sm", val);
    return 1;
  }

  if (strncmp(line, "light=", 6) == 0) {
    char *val = line + 6;
    strip_inline_hint(val);
    if (strcmp(val, "top-left") == 0) {
      cfg->light_x = 0.41f;
      cfg->light_y = 0.82f;
      cfg->light_z = -0.41f;
    } else if (strcmp(val, "top-right") == 0) {
      cfg->light_x = -0.41f;
      cfg->light_y = 0.82f;
      cfg->light_z = -0.41f;
    } else if (strcmp(val, "top") == 0) {
      cfg->light_x = 0.0f;
      cfg->light_y = 0.89f;
      cfg->light_z = -0.45f;
    } else if (strcmp(val, "left") == 0) {
      cfg->light_x = 0.82f;
      cfg->light_y = 0.41f;
      cfg->light_z = -0.41f;
    } else if (strcmp(val, "right") == 0) {
      cfg->light_x = -0.82f;
      cfg->light_y = 0.41f;
      cfg->light_z = -0.41f;
    } else if (strcmp(val, "front") == 0) {
      cfg->light_x = 0.0f;
      cfg->light_y = 0.0f;
      cfg->light_z = -1.0f;
    } else if (strcmp(val, "bottom-left") == 0) {
      cfg->light_x = 0.41f;
      cfg->light_y = -0.82f;
      cfg->light_z = -0.41f;
    } else if (strcmp(val, "bottom-right") == 0) {
      cfg->light_x = -0.41f;
      cfg->light_y = -0.82f;
      cfg->light_z = -0.41f;
    }
    return 1;
  }

  // disk=/path — add extra mount point
  if (strncasecmp(line, "disk=", 5) == 0) {
    char *path = line + 5; // note: no strip_inline_hint() here to allow spaces in path
    if (*path && cfg->extra_disk_count < MAX_EXTRA_DISKS) {
      strncpy(cfg->extra_disks[cfg->extra_disk_count], path,
              sizeof(cfg->extra_disks[0]) - 1);
      cfg->extra_disks[cfg->extra_disk_count][sizeof(cfg->extra_disks[0]) - 1] = '\0';
      cfg->extra_disk_count++;
    }
    // also enable disk field if not already
    if (!cfg->field_enabled[F_DISK] && cfg->field_count < MAX_FIELDS) {
      cfg->field_enabled[F_DISK] = 1;
      cfg->field_order[cfg->field_count++] = F_DISK;
    }
    return 1;
  }

  if (strncmp(line, "v_alignment=", 12) == 0) {
    char *val = line + 12;
    strip_inline_hint(val);
    if (strcmp(val, "top") == 0) {
      cfg->config_v_alignment = V_ALIGN_TOP;
    } else if (strcmp(val, "center") == 0) {
      cfg->config_v_alignment = V_ALIGN_CENTER;
    } else if (strcmp(val, "bottom") == 0) {
      cfg->config_v_alignment = V_ALIGN_BOTTOM;
    }
    return 1;
  }

  if (strncmp(line, "h_alignment=", 12) == 0) {
    char *val = line + 12;
    strip_inline_hint(val);
    if (strcmp(val, "left") == 0) {
      cfg->config_h_alignment = H_ALIGN_LEFT;
    } else if (strcmp(val, "center") == 0) {
      cfg->config_h_alignment = H_ALIGN_CENTER;
    } else if (strcmp(val, "right") == 0) {
      cfg->config_h_alignment = H_ALIGN_RIGHT;
    }
    return 1;
  }

  // Separator: a line of dashes
  if (strcmp(line, "---") == 0) {
    if (cfg->field_count < MAX_FIELDS)
      cfg->field_order[cfg->field_count++] = F_SEPARATOR;
    return 1;
  }

  // Custom static field: custom_Label=value
  if (strncmp(line, "custom_", 7) == 0) {
    char *eq = strchr(line + 7, '=');
    if (eq && eq > line + 7 && cfg->custom_count < MAX_CUSTOM &&
        cfg->field_count < MAX_FIELDS) {
      int llen = (int)(eq - (line + 7));
      if (llen > (int)sizeof(cfg->custom_label[0]) - 1)
        llen = (int)sizeof(cfg->custom_label[0]) - 1;
      memcpy(cfg->custom_label[cfg->custom_count], line + 7, llen);
      cfg->custom_label[cfg->custom_count][llen] = '\0';
      strncpy(cfg->custom_value[cfg->custom_count], eq + 1,
              sizeof(cfg->custom_value[0]) - 1);
      cfg->custom_value[cfg->custom_count][sizeof(cfg->custom_value[0]) - 1] = '\0';
      cfg->field_order[cfg->field_count++] = F_CUSTOM_BASE + cfg->custom_count;
      cfg->custom_count++;
      return 1;
    }
    return 0;
  }

  // Match field name
  for (int i = 0; field_map[i].name; i++) {
    if (strcasecmp(line, field_map[i].name) == 0) {
      int id = field_map[i].id;
      if (!cfg->field_enabled[id] && cfg->field_count < MAX_FIELDS) {
        cfg->field_enabled[id] = 1;
        cfg->field_order[cfg->field_count++] = id;
      }
      return 1;
    }
  }

  return 0;
}

int config_load_file(fetch_config_t *cfg, const char *path) {
  if (!cfg || !path || !path[0])
    return 0;

  FILE *fp = fopen(path, "r");
  if (!fp)
    return 0;

  // Config file exists — reset defaults, use file order
  for (int i = 0; i < F_COUNT; i++)
    cfg->field_enabled[i] = 0;
  cfg->field_count = 0;
  cfg->custom_count = 0;

  char buf[512];
  while (fgets(buf, sizeof(buf), fp)) {
    config_parse_line(cfg, buf);
  }
  fclose(fp);
  return 1;
}

__attribute__((weak)) void platform_get_config_path(char *out, size_t outsz);

void config_get_default_path(char *out, size_t outsz) {
  if (!out || outsz == 0) return;
  if (platform_get_config_path) {
    platform_get_config_path(out, outsz);
    if (out[0]) return;
  }
  const char *home = getenv("HOME");
  if (home && home[0]) {
    snprintf(out, outsz, "%s/.config/fetch/config", home);
  } else {
    out[0] = '\0';
  }
}

void config_defaults(void) {
  config_init_defaults(&g_config);
  config_sync_to_globals(&g_config);
}

void load_config(void) {
  char path[512];
  config_get_default_path(path, sizeof(path));
  if (path[0] == '\0')
    return;

  config_sync_from_globals(&g_config);
  if (config_load_file(&g_config, path)) {
    config_sync_to_globals(&g_config);
  }
}
