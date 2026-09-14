#ifndef FETCH_CONFIG_H
#define FETCH_CONFIG_H

#include <stddef.h>
#include "src/core/common.h"
#include "src/renderer/renderer.h"

// Available field enumeration IDs
enum {
  F_OS,
  F_HOST,
  F_KERNEL,
  F_UPTIME,
  F_PACKAGES,
  F_SHELL,
  F_DISPLAY,
  F_WM,
  F_DISPLAYMANAGER,
  F_THEME,
  F_ICONS,
  F_FONT,
  F_CURSOR,
  F_TERMINAL,
  F_CPU,
  F_GPU,
  F_MEMORY,
  F_SWAP,
  F_DISK,
  F_IP,
  F_BATTERY,
  F_LOCALE,
  F_COLORS,
  F_COUNT
};

#define F_SEPARATOR F_COUNT
#define F_CUSTOM_BASE (F_COUNT + 1)
#define MAX_CUSTOM 16
#define MAX_FIELDS (F_COUNT + MAX_CUSTOM + 16)

#define MAX_EXTRA_DISKS 8

typedef struct {
  int field_enabled[F_COUNT];
  int field_order[MAX_FIELDS];
  int field_count;
  char custom_label[MAX_CUSTOM][64];
  char custom_value[MAX_CUSTOM][256];
  int custom_count;
  char label_color[16];
  int config_height;
  float size_scale;
  float config_speed;
  int config_spin_x;
  int config_spin_y;
  int config_box;
  char config_shading[128];
  char config_shading_mode[16];
  char config_separator[8];
  float config_depth;
  int depth_user_set;
  char config_logo_outer[32];
  char config_logo_inner[32];
  char extra_disks[MAX_EXTRA_DISKS][128];
  int extra_disk_count;
  enum v_alignment config_v_alignment;
  enum h_alignment config_h_alignment;
  float light_x;
  float light_y;
  float light_z;
} fetch_config_t;

// Modular configuration API
void config_init_defaults(fetch_config_t *cfg);
int config_parse_line(fetch_config_t *cfg, const char *line);
int config_load_file(fetch_config_t *cfg, const char *path);
void config_get_default_path(char *out, size_t outsz);

int config_field_id_from_name(const char *name);
const char *config_field_name_from_id(int id);

// Backward-compatibility global instance and synchronization
extern fetch_config_t g_config;
void config_sync_to_globals(const fetch_config_t *cfg);
void config_sync_from_globals(fetch_config_t *cfg);

// Compatibility globals for existing fetch.c and Phase 0 baseline tests
extern int field_enabled[F_COUNT];
extern int field_order[MAX_FIELDS];
extern int field_count;
extern char custom_label[MAX_CUSTOM][64];
extern char custom_value[MAX_CUSTOM][256];
extern int custom_count;
extern char label_color[16];
extern int config_height;
extern float size_scale;
extern float config_speed;
extern int config_spin_x;
extern int config_spin_y;
extern int config_box;
extern char config_shading[128];
extern char config_shading_mode[16];
extern char config_separator[8];
extern float config_depth;
extern int depth_user_set;
extern char config_logo_outer[32];
extern char config_logo_inner[32];
extern char extra_disks[MAX_EXTRA_DISKS][128];
extern int extra_disk_count;
extern enum v_alignment config_v_alignment;
extern enum h_alignment config_h_alignment;
extern float light_x;
extern float light_y;
extern float light_z;

// Backward-compatibility functions
void config_defaults(void);
void load_config(void);

#endif // FETCH_CONFIG_H
