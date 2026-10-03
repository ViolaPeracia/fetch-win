#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

// Rename main so fetch.c does not define main()
#define main fetch_main
#include "../fetch.c"
#undef main

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
// Test Suite 1: Core UTF-8 & ANSI Utilities
// -------------------------------------------------------------
static void test_utf8_ansi(void) {
    TEST_SECTION("Core UTF-8 & ANSI Parsing Utilities");

    // utf8_char_len
    TEST_ASSERT(utf8_char_len('A') == 1, "ASCII byte len is 1");
    TEST_ASSERT(utf8_char_len((unsigned char)"\xC3\xA9"[0]) == 2, "2-byte UTF-8 lead byte returns 2");
    TEST_ASSERT(utf8_char_len((unsigned char)"\xE2\x96\x88"[0]) == 3, "3-byte UTF-8 lead byte returns 3");
    TEST_ASSERT(utf8_char_len((unsigned char)"\xF0\x9F\x8D\xA9"[0]) == 4, "4-byte UTF-8 lead byte returns 4");

    // skip_ansi
    TEST_ASSERT(skip_ansi("\033[1;35mText") == 7, "Skips standard SGR color escape");
    TEST_ASSERT(skip_ansi("\033[0m") == 4, "Skips reset escape");
    TEST_ASSERT(skip_ansi("\033[38;2;255;100;50m") == 18, "Skips 24-bit SGR color escape");
    TEST_ASSERT(skip_ansi("Normal text") == 0, "Non-escape returns 0");

    // strip_inline_hint
    char hint1[64] = "magenta (red, green, blue, ...)";
    strip_inline_hint(hint1);
    TEST_ASSERT(strcmp(hint1, "magenta") == 0, "Strips trailing documentation hint");

    char hint2[64] = "white   ";
    strip_inline_hint(hint2);
    TEST_ASSERT(strcmp(hint2, "white") == 0, "Trims trailing whitespace without hint");

    char hint3[64] = "no_hints";
    strip_inline_hint(hint3);
    TEST_ASSERT(strcmp(hint3, "no_hints") == 0, "Preserves strings without hints");

    // visible_width
    TEST_ASSERT(visible_width("Hello") == 5, "Plain ASCII visible width");
    TEST_ASSERT(visible_width("\033[1;32mGreen\033[0m") == 5, "Visible width ignores ANSI escapes");
    TEST_ASSERT(visible_width("█▀▄") == 3, "Multi-byte UTF-8 counts as 1 column per codepoint");
    TEST_ASSERT(visible_width("\033[31m█\033[0m \033[34m█\033[0m") == 3, "Mixed UTF-8 and ANSI visible width");

    // emit_clipped
    char out_buf[128];
    char *end = out_buf + sizeof(out_buf);
    
    // Test unclipped
    char *p = emit_clipped(out_buf, end, "Short text", 20);
    *p = '\0';
    TEST_ASSERT(strcmp(out_buf, "Short text") == 0, "Unclipped text emitted intact");

    // Test clipped with ellipsis
    p = emit_clipped(out_buf, end, "This is a very long string that must be clipped", 10);
    *p = '\0';
    TEST_ASSERT(strncmp(out_buf, "This is", 7) == 0, "Clipped text retains prefix");
    TEST_ASSERT(strstr(out_buf, "...") != NULL, "Clipped text includes ellipsis");
    TEST_ASSERT(visible_width(out_buf) <= 10, "Clipped visible width satisfies max_cols");

    // Test clipping with ANSI color reset preservation
    p = emit_clipped(out_buf, end, "\033[1;31mVery long red colored text exceeding width\033[0m", 12);
    *p = '\0';
    TEST_ASSERT(strstr(out_buf, "\033[0m") != NULL, "Clipped colored string restores ANSI reset");
}

// -------------------------------------------------------------
// Test Suite 2: Renderer & Shading Baseline
// -------------------------------------------------------------
static void test_renderer_baseline(void) {
    TEST_SECTION("Renderer & 3D Math Baseline");

    // char_weight_utf8
    float w_M = char_weight_utf8("M");
    float w_o = char_weight_utf8("o");
    float w_dot = char_weight_utf8(".");
    float w_space = char_weight_utf8(" ");
    TEST_ASSERT(w_M > w_o, "ASCII 'M' is heavier than 'o'");
    TEST_ASSERT(w_o > w_dot, "ASCII 'o' is heavier than '.'");
    TEST_ASSERT(w_dot > w_space, "ASCII '.' is heavier than space");
    TEST_ASSERT(w_space == 0.0f, "Space character has 0 weight");

    // UTF-8 block weights
    float w_full = char_weight_utf8("\xe2\x96\x88");   // U+2588 Full block
    float w_dark = char_weight_utf8("\xe2\x96\x93");   // U+2593 Dark shade
    float w_med  = char_weight_utf8("\xe2\x96\x92");   // U+2592 Medium shade
    float w_light= char_weight_utf8("\xe2\x96\x91");   // U+2591 Light shade
    TEST_ASSERT(w_full == 1.0f, "Full block has weight 1.0");
    TEST_ASSERT(w_dark == 0.75f, "Dark shade has weight 0.75");
    TEST_ASSERT(w_med == 0.50f, "Medium shade has weight 0.50");
    TEST_ASSERT(w_light == 0.25f, "Light shade has weight 0.25");

    // select_shading: ascii mode
    TEST_ASSERT(select_shading("ascii", NULL) == 1, "select_shading accepts 'ascii'");
    TEST_ASSERT(sub_rows == 1 && sub_cols == 1, "ASCII mode sets 1x1 sub-cell grid");
    TEST_ASSERT(shading_count > 5, "ASCII mode loads standard ramp");

    // select_shading: blocks mode
    TEST_ASSERT(select_shading("blocks", NULL) == 1, "select_shading accepts 'blocks'");
    TEST_ASSERT(sub_rows == 2 && sub_cols == 2, "Blocks mode sets 2x2 sub-cell grid");

    // select_shading: sextants mode
    TEST_ASSERT(select_shading("sextants", NULL) == 1, "select_shading accepts 'sextants'");
    TEST_ASSERT(sub_rows == 3 && sub_cols == 2, "Sextants mode sets 2x3 sub-cell grid");
    TEST_ASSERT(strlen(sextant_glyphs[1]) > 0, "Sextants glyph table is generated");

    // select_shading: custom ramp
    TEST_ASSERT(select_shading("ascii", ".*#") == 1, "select_shading accepts custom chars");
    TEST_ASSERT(shading_count == 3, "Custom ramp parsed to 3 codepoints");
    TEST_ASSERT(strcmp(shading_chars[0], ".") == 0, "Custom ramp char 0 is '.'");
    TEST_ASSERT(strcmp(shading_chars[1], "*") == 0, "Custom ramp char 1 is '*'");
    TEST_ASSERT(strcmp(shading_chars[2], "#") == 0, "Custom ramp char 2 is '#'");

    // select_shading: invalid mode
    TEST_ASSERT(select_shading("invalid_mode", NULL) == 0, "select_shading rejects unknown mode");

    // Logo loading & point cloud generation
    load_default_logo();
    TEST_ASSERT(logo_rows == 18, "Default Gentoo logo has 18 rows");
    process_logo();
    TEST_ASSERT(logo_cols > 20, "Default Gentoo logo has columns > 20");

    // Reset shading to ascii before building points
    select_shading("ascii", NULL);
    size_scale = 1.0f;
    config_depth = 1.0f;
    depth_user_set = 1;

    build_points();
    TEST_ASSERT(POINT_COUNT > 100, "Point cloud generated points > 100");
    TEST_ASSERT(POINT_COUNT < MAX_POINTS, "Point count within MAX_POINTS limit");

    // Verify all coordinates and normal vectors are finite numbers
    int finite_points = 1;
    for (int i = 0; i < POINT_COUNT; i++) {
        if (isnan(PX[i]) || isinf(PX[i]) ||
            isnan(PY[i]) || isinf(PY[i]) ||
            isnan(PZ[i]) || isinf(PZ[i]) ||
            isnan(NX[i]) || isinf(NX[i]) ||
            isnan(NY[i]) || isinf(NY[i]) ||
            isnan(NZ[i]) || isinf(NZ[i])) {
            finite_points = 0;
            break;
        }
    }
    TEST_ASSERT(finite_points == 1, "All 3D points and surface normals are finite");

    // Buffer clearing and cell glyph mapping
    render_height = 20;
    clear_buf();
    int color_out = -1;
    const char *empty_g = cell_glyph(5, 5, shading_count - 1, &color_out);
    TEST_ASSERT(empty_g == NULL, "Empty buffer returns NULL glyph");

    // Set sample value in buffer
    zbuf[5 * sub_rows][5 * sub_cols] = 0.5f;
    lumbuf[5 * sub_rows][5 * sub_cols] = 0.8f;
    colorbuf[5 * sub_rows][5 * sub_cols] = 35;
    const char *g = cell_glyph(5, 5, shading_count - 1, &color_out);
    TEST_ASSERT(g != NULL, "Populated buffer returns valid glyph");
    TEST_ASSERT(color_out == 35, "Populated buffer returns correct cell color");
}

// -------------------------------------------------------------
// Test Suite 3: Layout, Geometry & Sizing
// -------------------------------------------------------------
static void test_layout_sizing(void) {
    TEST_SECTION("Terminal Layout, Geometry & Resize");

    // Test wide terminal (side-by-side)
    term_cols = 120;
    term_rows = 40;
    fetch_line_count = 10;
    for (int i = 0; i < fetch_line_count; i++) {
        strcpy(fetch_lines[i], "OS: Linux 6.8.0 x86_64");
    }
    apply_layout(1);
    TEST_ASSERT(layout_stacked == 0, "Wide terminal (120 cols) uses side-by-side layout");
    TEST_ASSERT(anim_width == ANIM_WIDTH, "Wide terminal uses full 60 col canvas width");
    TEST_ASSERT(render_height >= 12, "Render height covers info rows");

    // Test narrow terminal (stacked layout): cols = 40 with 30-col info -> anim_width = 40-2-30 = 8 < 20 -> stacked!
    term_cols = 40;
    term_rows = 50;
    fetch_line_count = 5;
    for (int i = 0; i < fetch_line_count; i++) {
        strcpy(fetch_lines[i], "Memory: 14.20 GiB / 31.86 GiB (44%)");
    }
    apply_layout(1);
    TEST_ASSERT(layout_stacked == 1, "Narrow terminal (40 cols with 35-col info) triggers stacked layout");
    TEST_ASSERT(anim_width <= 40, "Stacked layout caps canvas width to terminal width");

    // Test no info (--no-info)
    term_cols = 100;
    term_rows = 40;
    apply_layout(0);
    TEST_ASSERT(layout_stacked == 0, "No-info layout is not stacked");
    TEST_ASSERT(render_height == logo_height, "No-info render height matches logo height");

    // Test zero terminal dimensions (non-tty fallback)
    term_cols = 0;
    term_rows = 0;
    apply_layout(1);
    TEST_ASSERT(render_height >= 20, "Zero terminal size falls back to safe default height");

    // Test alignment calculations
    config_v_alignment = V_ALIGN_CENTER;
    config_h_alignment = V_ALIGN_CENTER;
    term_rows = 40;
    term_cols = 120;
    render_height = 30;
    anim_width = 60;
    int v_pad = 0, h_pad = 0;
    get_alignment_padding(&v_pad, &h_pad);
    TEST_ASSERT(v_pad == (40/2 - 30/2), "Vertical centering computes correct padding");
    TEST_ASSERT(h_pad > 0, "Horizontal centering computes positive left padding");
}

// -------------------------------------------------------------
// Test Suite 4: Configuration Deserialization Baseline
// -------------------------------------------------------------
static void test_config_baseline(void) {
    TEST_SECTION("Configuration File Parsing Baseline");

    const char *cfg_content = 
        "# Baseline Fetch Config Test\n"
        "os\n"
        "cpu\n"
        "gpu\n"
        "memory\n"
        "swap\n"
        "disk\n"
        "\n"
        "# Appearance settings\n"
        "label_color=cyan (blue, cyan, etc)\n"
        "separator=─\n"
        "box=1\n"
        "speed=1.5\n"
        "size=1.2\n"
        "depth=2.0\n"
        "spin=y\n"
        "shading_mode=blocks\n"
        "shading=.,-~:;=!*#$@\n"
        "disk=/home/data\n"
        "disk=/mnt/backup\n"
        "v_alignment=bottom\n"
        "h_alignment=right\n";

    FILE *fp = fopen("test_fetch_config.tmp", "w");
    TEST_ASSERT(fp != NULL, "Creates temporary config file");
    fputs(cfg_content, fp);
    fclose(fp);

    #ifdef _WIN32
    system("mkdir .config 2>nul");
    system("mkdir .config\\fetch 2>nul");
    system("copy /Y test_fetch_config.tmp .config\\fetch\\config >nul");
    #else
    system("mkdir -p .config/fetch");
    system("cp test_fetch_config.tmp .config/fetch/config");
    #endif

    char orig_home[256] = "";
    char *cur_home = getenv("HOME");
    if (cur_home) strncpy(orig_home, cur_home, sizeof(orig_home)-1);

    // On POSIX the lookup prefers XDG_CONFIG_HOME over HOME, so pinning HOME
    // alone would leave the test reading whatever the environment exports (CI
    // images commonly do) and fail to find the fixture.
    #ifndef _WIN32
    char orig_xdg[256] = "";
    char *cur_xdg = getenv("XDG_CONFIG_HOME");
    if (cur_xdg) strncpy(orig_xdg, cur_xdg, sizeof(orig_xdg)-1);
    unsetenv("XDG_CONFIG_HOME");
    #endif

    #ifdef _WIN32
    _putenv("HOME=.");
    #else
    setenv("HOME", ".", 1);
    #endif

    config_defaults();
    load_config();

    // Verify fields enabled and order
    TEST_ASSERT(field_count == 6, "Parses exactly 6 active fields");
    TEST_ASSERT(field_order[0] == F_OS, "Field 0 is OS");
    TEST_ASSERT(field_order[1] == F_CPU, "Field 1 is CPU");
    TEST_ASSERT(field_order[2] == F_GPU, "Field 2 is GPU");
    TEST_ASSERT(field_order[3] == F_MEMORY, "Field 3 is Memory");
    TEST_ASSERT(field_order[4] == F_SWAP, "Field 4 is Swap");
    TEST_ASSERT(field_order[5] == F_DISK, "Field 5 is Disk");
    TEST_ASSERT(field_enabled[F_KERNEL] == 0, "Unlisted field (Kernel) is disabled");

    // Verify appearance settings
    TEST_ASSERT(strcmp(label_color, "36") == 0, "label_color=cyan parsed to ANSI 36 (hint stripped)");
    TEST_ASSERT(strcmp(config_separator, "─") == 0, "separator=─ parsed cleanly");
    TEST_ASSERT(config_box == 1, "box=1 enabled border box");
    TEST_ASSERT(fabsf(config_speed - 1.5f) < 0.01f, "speed=1.5 parsed accurately");
    TEST_ASSERT(fabsf(size_scale - 1.2f) < 0.01f, "size=1.2 parsed accurately");
    TEST_ASSERT(fabsf(config_depth - 2.0f) < 0.01f, "depth=2.0 parsed accurately");
    TEST_ASSERT(config_spin_x == 0 && config_spin_y == 1, "spin=y locks rotation to Y axis");
    TEST_ASSERT(strcmp(config_shading_mode, "blocks") == 0, "shading_mode=blocks parsed");
    TEST_ASSERT(strcmp(config_shading, ".,-~:;=!*#$@") == 0, "shading ramp string parsed without trimming");

    // Verify extra disks
    TEST_ASSERT(extra_disk_count == 2, "extra_disks parsed 2 additional mount points");
    TEST_ASSERT(strcmp(extra_disks[0], "/home/data") == 0, "extra_disks[0] is /home/data");
    TEST_ASSERT(strcmp(extra_disks[1], "/mnt/backup") == 0, "extra_disks[1] is /mnt/backup");

    // Verify alignments
    TEST_ASSERT(config_v_alignment == V_ALIGN_BOTTOM, "v_alignment=bottom parsed");
    TEST_ASSERT(config_h_alignment == H_ALIGN_RIGHT, "h_alignment=right parsed");

    // Clean up
    #ifndef _WIN32
    if (orig_xdg[0]) setenv("XDG_CONFIG_HOME", orig_xdg, 1);
    #endif
    if (orig_home[0]) {
        #ifdef _WIN32
        char env_buf[300];
        snprintf(env_buf, sizeof(env_buf), "HOME=%s", orig_home);
        _putenv(env_buf);
        #else
        setenv("HOME", orig_home, 1);
        #endif
    }
    remove("test_fetch_config.tmp");
    remove(".config/fetch/config");
    #ifdef _WIN32
    system("rmdir .config\\fetch 2>nul");
    system("rmdir .config 2>nul");
    #else
    system("rm -rf .config");
    #endif
}

// -------------------------------------------------------------
// Test Suite 5: CLI Options Parsing & Snapshots
// -------------------------------------------------------------
static void test_cli_options(void) {
    TEST_SECTION("CLI Argument Parsing & Snapshot Tests");

    // Test --help / -h
    char *help_args[] = {"fetch", "--help"};
    fflush(stdout);
    int res = fetch_main(2, help_args);
    TEST_ASSERT(res == 0, "fetch --help returns exit code 0");

    // Test --version / -V
    char *ver_args[] = {"fetch", "--version"};
    res = fetch_main(2, ver_args);
    TEST_ASSERT(res == 0, "fetch --version returns exit code 0");

    // Test invalid option
    char *bad_args[] = {"fetch", "--nonexistent-option-xyz"};
    res = fetch_main(2, bad_args);
    TEST_ASSERT(res == 1, "Unknown option returns exit code 1");

    // Test option missing argument
    char *miss_args[] = {"fetch", "--speed"};
    res = fetch_main(2, miss_args);
    TEST_ASSERT(res == 1, "--speed without argument returns exit code 1");

    char *miss_logo[] = {"fetch", "-l"};
    res = fetch_main(2, miss_logo);
    TEST_ASSERT(res == 1, "-l without argument returns exit code 1");

    char *miss_height[] = {"fetch", "--height"};
    res = fetch_main(2, miss_height);
    TEST_ASSERT(res == 1, "--height without argument returns exit code 1");

    char *miss_frames[] = {"fetch", "--frames"};
    res = fetch_main(2, miss_frames);
    TEST_ASSERT(res == 1, "--frames without argument returns exit code 1");
}

// -------------------------------------------------------------
// Test Suite 6: Box Wrapping & Formatting
// -------------------------------------------------------------
static void test_box_wrapping(void) {
    TEST_SECTION("Box Border Wrapping Baseline");

    fetch_line_count = 0;
    add_line("user@host");
    add_line("---------");
    add_info("OS", "Linux 6.8.0");
    add_info("CPU", "Test Processor @ 3.0 GHz");
    add_info("Memory", "4.00 GiB / 16.00 GiB (25%)");

    int initial_count = fetch_line_count;
    box_wrap_lines();

    TEST_ASSERT(fetch_line_count == initial_count + 2, "Box wrap adds top and bottom borders");
    TEST_ASSERT(strstr(fetch_lines[2], "\xe2\x95\xad") != NULL, "Line 2 is top border (╭)");
    TEST_ASSERT(strstr(fetch_lines[3], "\xe2\x94\x82") != NULL, "Line 3 starts with box vertical bar (│)");
    TEST_ASSERT(strstr(fetch_lines[fetch_line_count - 1], "\xe2\x95\xb0") != NULL, "Last line is bottom border (╰)");
}

// -------------------------------------------------------------
// Test Suite 7: Custom Logo Loading Baseline
// -------------------------------------------------------------
static void test_custom_logo_baseline(void) {
    TEST_SECTION("Custom Logo File Loading Baseline");

    const char *logo_art =
        "# distro: arch\n"
        "      /\\\n"
        "     /  \\\n"
        "    /\\   \\\n"
        "   /  __  \\\n"
        "  /  (  )  \\\n"
        " /.-'    '-.\\\n";

    FILE *fp = fopen("test_logo.tmp", "w");
    TEST_ASSERT(fp != NULL, "Creates temporary logo file");
    fputs(logo_art, fp);
    fclose(fp);

    #ifdef _WIN32
    system("mkdir .config 2>nul");
    system("mkdir .config\\fetch 2>nul");
    system("copy /Y test_logo.tmp .config\\fetch\\logo.txt >nul");
    #else
    system("mkdir -p .config/fetch");
    system("cp test_logo.tmp .config/fetch/logo.txt");
    #endif

    char orig_home[256] = "";
    char *cur_home = getenv("HOME");
    if (cur_home) strncpy(orig_home, cur_home, sizeof(orig_home)-1);

    // On POSIX the lookup prefers XDG_CONFIG_HOME over HOME, so pinning HOME
    // alone would leave the test reading whatever the environment exports (CI
    // images commonly do) and fail to find the fixture.
    #ifndef _WIN32
    char orig_xdg[256] = "";
    char *cur_xdg = getenv("XDG_CONFIG_HOME");
    if (cur_xdg) strncpy(orig_xdg, cur_xdg, sizeof(orig_xdg)-1);
    unsetenv("XDG_CONFIG_HOME");
    #endif

    #ifdef _WIN32
    _putenv("HOME=.");
    #else
    setenv("HOME", ".", 1);
    #endif

    logo_rows = 0;
    file_distro[0] = '\0';
    int loaded = load_logo_file();
    TEST_ASSERT(loaded == 1, "load_logo_file successfully loads custom logo");
    TEST_ASSERT(strcmp(file_distro, "arch") == 0, "Extracts '# distro: arch' header");
    TEST_ASSERT(logo_rows == 6, "Logo loads exactly 6 rows of art");

    // Clean up
    #ifndef _WIN32
    if (orig_xdg[0]) setenv("XDG_CONFIG_HOME", orig_xdg, 1);
    #endif
    if (orig_home[0]) {
        #ifdef _WIN32
        char env_buf[300];
        snprintf(env_buf, sizeof(env_buf), "HOME=%s", orig_home);
        _putenv(env_buf);
        #else
        setenv("HOME", orig_home, 1);
        #endif
    }
    remove("test_logo.tmp");
    remove(".config/fetch/logo.txt");
    #ifdef _WIN32
    system("rmdir .config\\fetch 2>nul");
    system("rmdir .config 2>nul");
    #else
    system("rm -rf .config");
    #endif
}

// -------------------------------------------------------------
// Main Test Runner
// -------------------------------------------------------------
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("====================================================\n");
    printf("  FETCH BASELINE REGRESSION TEST HARNESS (PHASE 0)  \n");
    printf("  Testing against unmodified upstream fetch.c      \n");
    printf("====================================================\n");

    test_utf8_ansi();
    test_renderer_baseline();
    test_layout_sizing();
    test_config_baseline();
    test_cli_options();
    test_box_wrapping();
    test_custom_logo_baseline();

    printf("\n====================================================\n");
    printf("  BASELINE TEST SUMMARY: %d / %d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(" (%d FAILED)", tests_failed);
    } else {
        printf(" (100%% SUCCESS)");
    }
    printf("\n====================================================\n");

    return tests_failed == 0 ? 0 : 1;
}
