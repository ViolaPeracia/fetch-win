#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "src/core/common.h"
#include "src/renderer/renderer.h"
#include "src/config/config.h"
#include "src/logo/logo.h"

int render_height = 36;

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    tests_run++; \
    if (cond) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        fprintf(stderr, "  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
    } \
} while(0)

#define TEST_SECTION(title) printf("\n=== %s ===\n", title)

// -------------------------------------------------------------
// Test Suite 1: Config Defaults Initialization
// -------------------------------------------------------------
static void test_config_defaults_init(void) {
    TEST_SECTION("Unit Tests: Config Defaults Initialization");

    fetch_config_t cfg;
    config_init_defaults(&cfg);

    TEST_ASSERT(cfg.field_count == 22, "Default field count is 22 (displaymanager omitted by default)");
    TEST_ASSERT(cfg.field_order[0] == F_OS, "Default first field is OS");
    TEST_ASSERT(cfg.field_order[1] == F_HOST, "Default second field is Host");
    TEST_ASSERT(cfg.field_order[21] == F_COLORS, "Default last field is Colors");

    TEST_ASSERT(cfg.field_enabled[F_OS] == 1, "OS field enabled by default");
    TEST_ASSERT(cfg.field_enabled[F_CPU] == 1, "CPU field enabled by default");
    TEST_ASSERT(cfg.field_enabled[F_GPU] == 1, "GPU field enabled by default");
    TEST_ASSERT(cfg.field_enabled[F_DISPLAYMANAGER] == 0, "Display Manager disabled by default");

    TEST_ASSERT(strcmp(cfg.label_color, "35") == 0, "Default label color is ANSI 35 (magenta)");
    TEST_ASSERT(strcmp(cfg.config_separator, "-") == 0, "Default title separator is '-'");
    TEST_ASSERT(cfg.config_height == 0, "Default height is 0 (auto-fit)");
    TEST_ASSERT(cfg.size_scale == 1.0f, "Default size scale is 1.0f");
    TEST_ASSERT(cfg.config_speed == 0.0f, "Default config speed is 0.0f");
    TEST_ASSERT(cfg.config_spin_x == -1 && cfg.config_spin_y == -1, "Default spin axes are -1 (unlocked)");
    TEST_ASSERT(cfg.config_box == 0, "Default box border is off (0)");
    TEST_ASSERT(cfg.config_depth == 1.0f, "Default 3D depth is 1.0f");
    TEST_ASSERT(cfg.depth_user_set == 0, "Default depth_user_set is 0");
    TEST_ASSERT(cfg.extra_disk_count == 0, "Default extra disks count is 0");
    TEST_ASSERT(cfg.config_v_alignment == V_ALIGN_TOP, "Default vertical alignment is TOP");
    TEST_ASSERT(cfg.config_h_alignment == H_ALIGN_LEFT, "Default horizontal alignment is LEFT");
    TEST_ASSERT(fabsf(cfg.light_x - 0.4082f) < 0.001f, "Default light_x is 0.4082");
    TEST_ASSERT(fabsf(cfg.light_y - 0.8165f) < 0.001f, "Default light_y is 0.8165");
    TEST_ASSERT(fabsf(cfg.light_z - (-0.4082f)) < 0.001f, "Default light_z is -0.4082");
}

// -------------------------------------------------------------
// Test Suite 2: Config Line Parsing, Hints, Keys & Clamping
// -------------------------------------------------------------
static void test_config_line_parsing(void) {
    TEST_SECTION("Unit Tests: Config Line Parsing & Value Clamping");

    fetch_config_t cfg;
    config_init_defaults(&cfg);

    // Comments, empty lines, and whitespace
    TEST_ASSERT(config_parse_line(&cfg, "# This is a comment") == 0, "Ignores full-line comment");
    TEST_ASSERT(config_parse_line(&cfg, "   # Indented comment") == 0, "Ignores indented comment");
    TEST_ASSERT(config_parse_line(&cfg, "") == 0, "Ignores empty line");
    TEST_ASSERT(config_parse_line(&cfg, "   \t  ") == 0, "Ignores whitespace line");

    // Field names with case insensitivity and whitespace
    for (int i = 0; i < F_COUNT; i++) cfg.field_enabled[i] = 0;
    cfg.field_count = 0;

    TEST_ASSERT(config_parse_line(&cfg, "os") == 1, "Parses field 'os'");
    TEST_ASSERT(cfg.field_count == 1 && cfg.field_order[0] == F_OS, "Field 0 is OS");

    TEST_ASSERT(config_parse_line(&cfg, "  kErNeL  ") == 1, "Parses field 'kErNeL' with whitespace & mixed case");
    TEST_ASSERT(cfg.field_count == 2 && cfg.field_order[1] == F_KERNEL, "Field 1 is KERNEL");

    // Setting: label_color named colors and numeric codes
    config_parse_line(&cfg, "label_color=red (ansi 31)");
    TEST_ASSERT(strcmp(cfg.label_color, "31") == 0, "label_color=red mapped to 31 with hint stripped");

    config_parse_line(&cfg, "label_color=green");
    TEST_ASSERT(strcmp(cfg.label_color, "32") == 0, "label_color=green mapped to 32");

    config_parse_line(&cfg, "label_color=yellow");
    TEST_ASSERT(strcmp(cfg.label_color, "33") == 0, "label_color=yellow mapped to 33");

    config_parse_line(&cfg, "label_color=blue");
    TEST_ASSERT(strcmp(cfg.label_color, "34") == 0, "label_color=blue mapped to 34");

    config_parse_line(&cfg, "label_color=magenta");
    TEST_ASSERT(strcmp(cfg.label_color, "35") == 0, "label_color=magenta mapped to 35");

    config_parse_line(&cfg, "label_color=cyan");
    TEST_ASSERT(strcmp(cfg.label_color, "36") == 0, "label_color=cyan mapped to 36");

    config_parse_line(&cfg, "label_color=white");
    TEST_ASSERT(strcmp(cfg.label_color, "37") == 0, "label_color=white mapped to 37");

    config_parse_line(&cfg, "label_color=95 (bright magenta)");
    TEST_ASSERT(strcmp(cfg.label_color, "95") == 0, "label_color=95 parsed raw number with hint stripped");

    // Setting: height with clamping to MAX_HEIGHT
    config_parse_line(&cfg, "height=45 (override)");
    TEST_ASSERT(cfg.config_height == 45, "height=45 parsed with hint stripped");

    config_parse_line(&cfg, "height=999");
    TEST_ASSERT(cfg.config_height == MAX_HEIGHT, "height exceeding MAX_HEIGHT is clamped to MAX_HEIGHT");

    // Setting: size with clamping [0.5, 5.0]
    config_parse_line(&cfg, "size=2.5 (scale factor)");
    TEST_ASSERT(fabsf(cfg.size_scale - 2.5f) < 0.001f, "size=2.5 parsed with hint stripped");

    config_parse_line(&cfg, "size=0.1");
    TEST_ASSERT(fabsf(cfg.size_scale - 0.5f) < 0.001f, "size < 0.5 clamped to 0.5");

    config_parse_line(&cfg, "size=10.0");
    TEST_ASSERT(fabsf(cfg.size_scale - 5.0f) < 0.001f, "size > 5.0 clamped to 5.0");

    // Setting: speed
    config_parse_line(&cfg, "speed=1.75 (multiplier)");
    TEST_ASSERT(fabsf(cfg.config_speed - 1.75f) < 0.001f, "speed=1.75 parsed with hint stripped");

    // Setting: spin
    config_parse_line(&cfg, "spin=x");
    TEST_ASSERT(cfg.config_spin_x == 1 && cfg.config_spin_y == 0, "spin=x locks rotation to X axis");

    config_parse_line(&cfg, "spin=Y");
    TEST_ASSERT(cfg.config_spin_x == 0 && cfg.config_spin_y == 1, "spin=Y locks rotation to Y axis");

    config_parse_line(&cfg, "spin=xy");
    TEST_ASSERT(cfg.config_spin_x == 1 && cfg.config_spin_y == 1, "spin=xy enables both X and Y rotation");

    // Setting: box truthy values
    config_parse_line(&cfg, "box=1");
    TEST_ASSERT(cfg.config_box == 1, "box=1 enables box");
    config_parse_line(&cfg, "box=0");
    TEST_ASSERT(cfg.config_box == 0, "box=0 disables box");
    config_parse_line(&cfg, "box=yes (draw border)");
    TEST_ASSERT(cfg.config_box == 1, "box=yes enables box with hint stripped");
    config_parse_line(&cfg, "box=true");
    TEST_ASSERT(cfg.config_box == 1, "box=true enables box");

    // Setting: shading & separator (must NOT strip inline hints)
    config_parse_line(&cfg, "shading=.:*# (hint characters)");
    TEST_ASSERT(strstr(cfg.config_shading, "(hint characters)") != NULL, "shading keeps freeform parenthesis characters");

    config_parse_line(&cfg, "shading_mode=sextants (2x3)");
    TEST_ASSERT(strcmp(cfg.config_shading_mode, "sextants") == 0, "shading_mode=sextants strips hint");

    config_parse_line(&cfg, "separator=(:)");
    TEST_ASSERT(strcmp(cfg.config_separator, "(:)") == 0, "separator keeps freeform text including parenthesis");

    // Setting: depth with clamping [0.1, 10.0]
    config_parse_line(&cfg, "depth=3.0");
    TEST_ASSERT(fabsf(cfg.config_depth - 3.0f) < 0.001f && cfg.depth_user_set == 1, "depth=3.0 sets depth and depth_user_set");

    config_parse_line(&cfg, "depth=0.01");
    TEST_ASSERT(fabsf(cfg.config_depth - 0.1f) < 0.001f, "depth < 0.1 clamped to 0.1");

    config_parse_line(&cfg, "depth=25.0");
    TEST_ASSERT(fabsf(cfg.config_depth - 10.0f) < 0.001f, "depth > 10.0 clamped to 10.0");

    // Setting: logo_outer and logo_inner
    config_parse_line(&cfg, "logo_outer=cyan");
    TEST_ASSERT(strcmp(cfg.config_logo_outer, "\033[1;36m") == 0, "logo_outer=cyan produces bold cyan SGR");

    config_parse_line(&cfg, "logo_inner=33");
    TEST_ASSERT(strcmp(cfg.config_logo_inner, "\033[1;33m") == 0, "logo_inner=33 produces custom ANSI SGR");

    // Setting: light presets
    config_parse_line(&cfg, "light=front");
    TEST_ASSERT(cfg.light_x == 0.0f && cfg.light_y == 0.0f && cfg.light_z == -1.0f, "light=front sets direct front light");

    config_parse_line(&cfg, "light=top");
    TEST_ASSERT(cfg.light_x == 0.0f && fabsf(cfg.light_y - 0.89f) < 0.01f && fabsf(cfg.light_z - (-0.45f)) < 0.01f, "light=top sets top light");

    // Setting: extra disks and auto-enable F_DISK
    cfg.field_enabled[F_DISK] = 0;
    config_parse_line(&cfg, "disk=/var/lib/data");
    TEST_ASSERT(cfg.extra_disk_count == 1, "disk= added first extra mount point");
    TEST_ASSERT(strcmp(cfg.extra_disks[0], "/var/lib/data") == 0, "extra_disks[0] matches parsed path");
    TEST_ASSERT(cfg.field_enabled[F_DISK] == 1, "disk= automatically enabled F_DISK");

    // Setting: alignments
    config_parse_line(&cfg, "v_alignment=center");
    TEST_ASSERT(cfg.config_v_alignment == V_ALIGN_CENTER, "v_alignment=center parsed");

    config_parse_line(&cfg, "h_alignment=right");
    TEST_ASSERT(cfg.config_h_alignment == H_ALIGN_RIGHT, "h_alignment=right parsed");

    // Field ID and Name lookups
    TEST_ASSERT(config_field_id_from_name("terminal") == F_TERMINAL, "Look up field id for 'terminal'");
    TEST_ASSERT(strcmp(config_field_name_from_id(F_TERMINAL), "terminal") == 0, "Look up field name for F_TERMINAL");
    TEST_ASSERT(config_field_id_from_name("nonexistent") == -1, "Invalid field name returns -1");
    TEST_ASSERT(config_field_name_from_id(999) == NULL, "Invalid field id returns NULL");
}

// -------------------------------------------------------------
// Test Suite 3: Config File Deserialization & Global Sync
// -------------------------------------------------------------
static void test_config_file_and_globals_sync(void) {
    TEST_SECTION("Unit Tests: Config File Deserialization & Global Sync");

    const char *cfg_text =
        "# Temporary Modular Config\n"
        "host\n"
        "cpu\n"
        "memory\n"
        "label_color=blue\n"
        "box=true\n"
        "speed=2.2\n"
        "size=1.5\n";

    FILE *fp = fopen("test_phase2_config.tmp", "w");
    TEST_ASSERT(fp != NULL, "Creates temporary config file");
    fputs(cfg_text, fp);
    fclose(fp);

    fetch_config_t cfg;
    config_init_defaults(&cfg);

    int loaded = config_load_file(&cfg, "test_phase2_config.tmp");
    TEST_ASSERT(loaded == 1, "config_load_file successfully loads file");
    TEST_ASSERT(cfg.field_count == 3, "File order resets defaults to 3 fields");
    TEST_ASSERT(cfg.field_order[0] == F_HOST, "Field 0 is HOST");
    TEST_ASSERT(cfg.field_order[1] == F_CPU, "Field 1 is CPU");
    TEST_ASSERT(cfg.field_order[2] == F_MEMORY, "Field 2 is MEMORY");
    TEST_ASSERT(strcmp(cfg.label_color, "34") == 0, "label_color=blue parsed to 34");
    TEST_ASSERT(cfg.config_box == 1, "box=true parsed");
    TEST_ASSERT(fabsf(cfg.config_speed - 2.2f) < 0.01f, "speed=2.2 parsed");

    // Test non-existent file
    int missing = config_load_file(&cfg, "non_existent_file_xyz_123.cfg");
    TEST_ASSERT(missing == 0, "Missing file returns 0 without crashing");

    // Test sync to and from globals
    config_sync_to_globals(&cfg);
    TEST_ASSERT(field_count == 3, "Globals field_count synced");
    TEST_ASSERT(field_order[0] == F_HOST, "Globals field_order[0] synced");
    TEST_ASSERT(strcmp(label_color, "34") == 0, "Globals label_color synced");
    TEST_ASSERT(config_box == 1, "Globals config_box synced");

    // Mutate global and sync back
    field_count = 1;
    field_order[0] = F_DISK;
    strcpy(label_color, "31");
    config_sync_from_globals(&cfg);
    TEST_ASSERT(cfg.field_count == 1 && cfg.field_order[0] == F_DISK, "Sync from globals updates struct");
    TEST_ASSERT(strcmp(cfg.label_color, "31") == 0, "Sync from globals updates label_color");

    remove("test_phase2_config.tmp");
}

// -------------------------------------------------------------
// Test Suite 4: Logo Default Loading & Processing
// -------------------------------------------------------------
static void test_logo_default_loading(void) {
    TEST_SECTION("Unit Tests: Default Gentoo Logo Loading");

    fetch_logo_t logo;
    logo_load_default(&logo);

    TEST_ASSERT(logo.rows == 18, "Default Gentoo logo has 18 rows");
    TEST_ASSERT(strcmp(logo.distro, "gentoo") == 0, "Default logo distro tag is 'gentoo'");
    TEST_ASSERT(strlen(logo.data[0]) > 0, "Logo row 0 has content");

    logo_process(&logo);
    TEST_ASSERT(logo.cols > 25, "Logo cols computed accurately (> 25)");
    TEST_ASSERT(logo.cell_counts[0] > 0, "Cell counts for row 0 populated");
    TEST_ASSERT(logo.has_ansi == 0, "Default ASCII logo contains no embedded ANSI escapes");

    // Test global bridge
    load_default_logo();
    TEST_ASSERT(logo_rows == 18, "Compatibility logo_rows matches default");
    process_logo();
    TEST_ASSERT(logo_cols > 25, "Compatibility logo_cols matches default");
}

// -------------------------------------------------------------
// Test Suite 5: Custom Logo File & Metadata Parsing
// -------------------------------------------------------------
static void test_custom_logo_file(void) {
    TEST_SECTION("Unit Tests: Custom Logo Loading & Metadata Parsing");

    const char *custom_art =
        "# distro: fedora\n"
        "          .---.\n"
        "         / ... \\\n"
        "        | .---. |\n"
        "        | |   | |\n"
        "   .----' '---' '----.\n"
        "  / .---------------. \\\n"
        "\n"
        "\n";

    FILE *fp = fopen("test_phase2_logo.tmp", "w");
    TEST_ASSERT(fp != NULL, "Creates temporary logo file");
    fputs(custom_art, fp);
    fclose(fp);

    fetch_logo_t logo;
    logo_init(&logo);

    int res = logo_load_file(&logo, "test_phase2_logo.tmp");
    TEST_ASSERT(res == 1, "logo_load_file successfully loads file");
    TEST_ASSERT(strcmp(logo.distro, "fedora") == 0, "Parses '# distro: fedora' header");
    TEST_ASSERT(logo.rows == 6, "Loads exactly 6 rows of art (empty lines trimmed)");

    logo_process(&logo);
    TEST_ASSERT(logo.cols > 15, "Logo columns parsed");
    TEST_ASSERT(logo.cells[0][10][0] == '.', "Cell codepoint extracted correctly");

    remove("test_phase2_logo.tmp");
}

// -------------------------------------------------------------
// Test Suite 6: ANSI Color Extraction & Codepoints
// -------------------------------------------------------------
static void test_logo_ansi_extraction(void) {
    TEST_SECTION("Unit Tests: Logo ANSI SGR Color Extraction");

    fetch_logo_t logo;
    logo_init(&logo);

    // Row with: 3 chars red (31), 4 chars blue (34), reset, 6 chars normal
    const char *colored_row = "\033[1;31mRED\033[1;34mBLUE\033[0mNORMAL";
    strcpy(logo.data[0], colored_row);
    logo.rows = 1;

    logo_process(&logo);

    TEST_ASSERT(logo.has_ansi == 1, "Detects embedded ANSI sequences");
    TEST_ASSERT(logo.cols == 13, "Visible columns count excludes ANSI escapes (3+4+6=13)");
    TEST_ASSERT(logo.cell_counts[0] == 13, "Row cell count is 13");

    // Verify cell characters
    TEST_ASSERT(strcmp(logo.cells[0][0], "R") == 0, "Cell 0 is 'R'");
    TEST_ASSERT(strcmp(logo.cells[0][1], "E") == 0, "Cell 1 is 'E'");
    TEST_ASSERT(strcmp(logo.cells[0][2], "D") == 0, "Cell 2 is 'D'");
    TEST_ASSERT(strcmp(logo.cells[0][3], "B") == 0, "Cell 3 is 'B'");
    TEST_ASSERT(strcmp(logo.cells[0][7], "N") == 0, "Cell 7 is 'N'");

    // Verify cell colors
    TEST_ASSERT(logo.cell_color[0][0] == 31, "Cell 0 color is 31 (Red)");
    TEST_ASSERT(logo.cell_color[0][1] == 31, "Cell 1 color is 31 (Red)");
    TEST_ASSERT(logo.cell_color[0][2] == 31, "Cell 2 color is 31 (Red)");
    TEST_ASSERT(logo.cell_color[0][3] == 34, "Cell 3 color is 34 (Blue)");
    TEST_ASSERT(logo.cell_color[0][4] == 34, "Cell 4 color is 34 (Blue)");
    TEST_ASSERT(logo.cell_color[0][7] == 0,  "Cell 7 color is 0 (Reset/Normal)");

    // Test Multi-byte Unicode in colored logo
    logo_init(&logo);
    strcpy(logo.data[0], "\033[32m\xe2\x96\x88\xe2\x96\x92\033[0m");
    logo.rows = 1;
    logo_process(&logo);
    TEST_ASSERT(logo.cols == 2, "2 UTF-8 characters parsed to 2 visible columns");
    TEST_ASSERT(strcmp(logo.cells[0][0], "\xe2\x96\x88") == 0, "Full block U+2588 codepoint preserved");
    TEST_ASSERT(logo.cell_color[0][0] == 32, "Full block cell color is 32 (Green)");
}

// -------------------------------------------------------------
// Test Suite 7: Distro Color Schemes Mapping
// -------------------------------------------------------------
static void test_distro_color_mapping(void) {
    TEST_SECTION("Unit Tests: Distro Color Scheme Mapping");

    const char *outer = NULL, *inner = NULL;

    // Gentoo: magenta (35) + white (37)
    logo_set_distro_colors("gentoo", &outer, &inner);
    TEST_ASSERT(strcmp(outer, "\033[1;35m") == 0, "Gentoo outer is magenta");
    TEST_ASSERT(strcmp(inner, "\033[1;37m") == 0, "Gentoo inner is white");

    // Arch: cyan (36) + cyan (36)
    logo_set_distro_colors("arch", &outer, &inner);
    TEST_ASSERT(strcmp(outer, "\033[1;36m") == 0 && strcmp(inner, "\033[1;36m") == 0, "Arch colors are cyan/cyan");

    // Ubuntu: red (31) + white (37)
    logo_set_distro_colors("ubuntu", &outer, &inner);
    TEST_ASSERT(strcmp(outer, "\033[1;31m") == 0 && strcmp(inner, "\033[1;37m") == 0, "Ubuntu colors are red/white");

    // Debian: red (31) + white (37)
    logo_set_distro_colors("debian", &outer, &inner);
    TEST_ASSERT(strcmp(outer, "\033[1;31m") == 0 && strcmp(inner, "\033[1;37m") == 0, "Debian colors are red/white");

    // Fedora: blue (34) + white (37)
    logo_set_distro_colors("fedora", &outer, &inner);
    TEST_ASSERT(strcmp(outer, "\033[1;34m") == 0 && strcmp(inner, "\033[1;37m") == 0, "Fedora colors are blue/white");

    // NixOS: blue (34) + cyan (36)
    logo_set_distro_colors("nixos", &outer, &inner);
    TEST_ASSERT(strcmp(outer, "\033[1;34m") == 0 && strcmp(inner, "\033[1;36m") == 0, "NixOS colors are blue/cyan");

    // Void: green (32) + green (32)
    logo_set_distro_colors("void", &outer, &inner);
    TEST_ASSERT(strcmp(outer, "\033[1;32m") == 0 && strcmp(inner, "\033[1;32m") == 0, "Void colors are green/green");

    // Alpine: blue (34) + white (37)
    logo_set_distro_colors("alpine", &outer, &inner);
    TEST_ASSERT(strcmp(outer, "\033[1;34m") == 0 && strcmp(inner, "\033[1;37m") == 0, "Alpine colors are blue/white");

    // openSUSE: green (32) + white (37)
    logo_set_distro_colors("opensuse", &outer, &inner);
    TEST_ASSERT(strcmp(outer, "\033[1;32m") == 0 && strcmp(inner, "\033[1;37m") == 0, "openSUSE colors are green/white");

    // macOS: cyan (36) + white (37)
    logo_set_distro_colors("macos", &outer, &inner);
    TEST_ASSERT(strcmp(outer, "\033[1;36m") == 0 && strcmp(inner, "\033[1;37m") == 0, "macOS colors are cyan/white");

    // Unknown distro fallback: magenta (35) + white (37)
    logo_set_distro_colors("unknown_distro", &outer, &inner);
    TEST_ASSERT(strcmp(outer, "\033[1;35m") == 0 && strcmp(inner, "\033[1;37m") == 0, "Unknown distro falls back to magenta/white");
}

// -------------------------------------------------------------
// Test Suite 8: Separators and Custom Static Fields
// -------------------------------------------------------------
static void test_separators_and_custom_fields(void) {
    TEST_SECTION("Unit Tests: Separators & Custom Static Fields");

    fetch_config_t cfg;
    config_init_defaults(&cfg);

    // 1. Initial state
    TEST_ASSERT(cfg.custom_count == 0, "Initial custom_count is 0");

    // Reset fields
    for (int i = 0; i < F_COUNT; i++) cfg.field_enabled[i] = 0;
    cfg.field_count = 0;

    // 2. Separator parsing
    int rc = config_parse_line(&cfg, "---");
    TEST_ASSERT(rc == 1, "Separator line '---' returns 1");
    TEST_ASSERT(cfg.field_count == 1, "Field count increments for separator");
    TEST_ASSERT(cfg.field_order[0] == F_SEPARATOR, "field_order[0] is F_SEPARATOR");

    // 3. Custom field parsing
    rc = config_parse_line(&cfg, "custom_Pronouns=he/him");
    TEST_ASSERT(rc == 1, "Valid custom field 'custom_Pronouns=he/him' returns 1");
    TEST_ASSERT(cfg.field_count == 2, "Field count is 2");
    TEST_ASSERT(cfg.custom_count == 1, "Custom count is 1");
    TEST_ASSERT(cfg.field_order[1] == F_CUSTOM_BASE, "field_order[1] is F_CUSTOM_BASE");
    TEST_ASSERT(strcmp(cfg.custom_label[0], "Pronouns") == 0, "custom_label[0] is 'Pronouns'");
    TEST_ASSERT(strcmp(cfg.custom_value[0], "he/him") == 0, "custom_value[0] is 'he/him'");

    // 4. Mixed ordering
    config_parse_line(&cfg, "os");
    config_parse_line(&cfg, "---");
    config_parse_line(&cfg, "custom_Website=example.com");
    config_parse_line(&cfg, "cpu");

    TEST_ASSERT(cfg.field_count == 6, "Mixed config has 6 entries");
    TEST_ASSERT(cfg.field_order[0] == F_SEPARATOR, "Entry 0 is separator");
    TEST_ASSERT(cfg.field_order[1] == F_CUSTOM_BASE + 0, "Entry 1 is custom 0");
    TEST_ASSERT(cfg.field_order[2] == F_OS, "Entry 2 is OS");
    TEST_ASSERT(cfg.field_order[3] == F_SEPARATOR, "Entry 3 is separator");
    TEST_ASSERT(cfg.field_order[4] == F_CUSTOM_BASE + 1, "Entry 4 is custom 1");
    TEST_ASSERT(cfg.field_order[5] == F_CPU, "Entry 5 is CPU");
    TEST_ASSERT(strcmp(cfg.custom_label[1], "Website") == 0, "custom_label[1] is 'Website'");
    TEST_ASSERT(strcmp(cfg.custom_value[1], "example.com") == 0, "custom_value[1] is 'example.com'");

    // 5. Malformed custom entries
    TEST_ASSERT(config_parse_line(&cfg, "custom_") == 0, "custom_ without equals is rejected");
    TEST_ASSERT(config_parse_line(&cfg, "custom_NoEquals") == 0, "custom_NoEquals is rejected");
    TEST_ASSERT(config_parse_line(&cfg, "custom_=val") == 0, "custom_=val (empty label) is rejected");

    // 6. Empty value
    rc = config_parse_line(&cfg, "custom_Empty=");
    TEST_ASSERT(rc == 1, "custom_Empty= is accepted");
    TEST_ASSERT(strcmp(cfg.custom_label[cfg.custom_count - 1], "Empty") == 0, "Empty label stored");
    TEST_ASSERT(strcmp(cfg.custom_value[cfg.custom_count - 1], "") == 0, "Empty value stored");

    // 7. Label truncation at 63 characters
    char long_label_line[300];
    char long_name[120];
    memset(long_name, 'A', 100);
    long_name[100] = '\0';
    snprintf(long_label_line, sizeof(long_label_line), "custom_%s=testval", long_name);
    rc = config_parse_line(&cfg, long_label_line);
    TEST_ASSERT(rc == 1, "Long label is accepted with truncation");
    int ci = cfg.custom_count - 1;
    TEST_ASSERT(strlen(cfg.custom_label[ci]) == 63, "Label truncated to 63 bytes");
    TEST_ASSERT(cfg.custom_label[ci][63] == '\0', "Label is NUL-terminated");
    TEST_ASSERT(strcmp(cfg.custom_value[ci], "testval") == 0, "Value intact after label truncation");

    // 8. Value truncation at 255 characters
    char long_val_line[500];
    char long_val[350];
    memset(long_val, 'B', 300);
    long_val[300] = '\0';
    snprintf(long_val_line, sizeof(long_val_line), "custom_TruncVal=%s", long_val);
    rc = config_parse_line(&cfg, long_val_line);
    TEST_ASSERT(rc == 1, "Long value is accepted with truncation");
    ci = cfg.custom_count - 1;
    TEST_ASSERT(strcmp(cfg.custom_label[ci], "TruncVal") == 0, "Label intact");
    TEST_ASSERT(strlen(cfg.custom_value[ci]) == 255, "Value truncated to 255 bytes");
    TEST_ASSERT(cfg.custom_value[ci][255] == '\0', "Value is NUL-terminated");

    // 9. Fill up to MAX_CUSTOM (16 fields)
    while (cfg.custom_count < MAX_CUSTOM) {
        char buf[64];
        snprintf(buf, sizeof(buf), "custom_Field%d=V%d", cfg.custom_count, cfg.custom_count);
        TEST_ASSERT(config_parse_line(&cfg, buf) == 1, "Adds custom field up to MAX_CUSTOM");
    }
    TEST_ASSERT(cfg.custom_count == MAX_CUSTOM, "Custom count reached MAX_CUSTOM (16)");

    // 10. 17th custom field overflow rejected
    int fields_before = cfg.field_count;
    rc = config_parse_line(&cfg, "custom_17th=fail");
    TEST_ASSERT(rc == 0, "17th custom field is rejected");
    TEST_ASSERT(cfg.custom_count == MAX_CUSTOM, "Custom count remains 16");
    TEST_ASSERT(cfg.field_count == fields_before, "Field count unchanged after rejection");

    // 11. Globals synchronization: struct -> globals
    config_sync_to_globals(&cfg);
    TEST_ASSERT(field_count == cfg.field_count, "field_count synced to global");
    TEST_ASSERT(custom_count == cfg.custom_count, "custom_count synced to global");
    TEST_ASSERT(field_order[0] == cfg.field_order[0], "field_order[0] synced to global");
    TEST_ASSERT(field_order[1] == cfg.field_order[1], "field_order[1] synced to global");
    TEST_ASSERT(strcmp(custom_label[0], "Pronouns") == 0, "custom_label[0] synced to global");
    TEST_ASSERT(strcmp(custom_value[0], "he/him") == 0, "custom_value[0] synced to global");

    // 12. Globals synchronization: globals -> struct
    fetch_config_t cfg2;
    memset(&cfg2, 0, sizeof(cfg2));
    config_sync_from_globals(&cfg2);
    TEST_ASSERT(cfg2.field_count == field_count, "cfg2.field_count synced from global");
    TEST_ASSERT(cfg2.custom_count == custom_count, "cfg2.custom_count synced from global");
    TEST_ASSERT(cfg2.field_order[0] == field_order[0], "cfg2.field_order[0] synced from global");
    TEST_ASSERT(cfg2.field_order[1] == field_order[1], "cfg2.field_order[1] synced from global");
    TEST_ASSERT(strcmp(cfg2.custom_label[0], "Pronouns") == 0, "cfg2.custom_label[0] synced from global");
    TEST_ASSERT(strcmp(cfg2.custom_value[0], "he/him") == 0, "cfg2.custom_value[0] synced from global");
}

// -------------------------------------------------------------
// Main Test Runner
// -------------------------------------------------------------
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("====================================================\n");
    printf("  FETCH PHASE 2 EXTRACTION UNIT TESTS               \n");
    printf("  Testing isolated src/config and src/logo modules  \n");
    printf("====================================================\n");

    test_config_defaults_init();
    test_config_line_parsing();
    test_config_file_and_globals_sync();
    test_separators_and_custom_fields();
    test_logo_default_loading();
    test_custom_logo_file();
    test_logo_ansi_extraction();
    test_distro_color_mapping();

    printf("\n====================================================\n");
    printf("  PHASE 2 TEST SUMMARY: %d / %d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(" (%d FAILED)", tests_failed);
    } else {
        printf(" (100%% SUCCESS)");
    }
    printf("\n====================================================\n");

    return tests_failed == 0 ? 0 : 1;
}
