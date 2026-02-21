#include "kterm-settings.h"
#include <string.h>
#include <stdio.h>

/* Default palette: Material Ocean */
static const char *default_palette_hex[KTERM_PALETTE_SIZE] = {
    "#0f111a", "#ff5370", "#c3e88d", "#ffcb6b",
    "#82aaff", "#c792ea", "#89ddff", "#8f93a2",
    "#464b5d", "#ff5370", "#c3e88d", "#ffcb6b",
    "#82aaff", "#c792ea", "#89ddff", "#d0d0d0",
};

static KtermSettings default_settings = {
    .font_family     = NULL,
    .font_size       = 11.0,
    .foreground      = { 0.561, 0.576, 0.635, 1.0 },   /* #8f93a2 */
    .background      = { 0.059, 0.067, 0.102, 1.0 },   /* #0f111a */
    .cursor_shape    = VTE_CURSOR_SHAPE_IBEAM,
    .cursor_blink    = VTE_CURSOR_BLINK_SYSTEM,
    .scrollback_lines = -1,
    .audible_bell    = FALSE,
    .pane_border_size = 2,
    .show_pane_outline = FALSE,
    .sidebar_visible = FALSE,
    .sidebar_position = 0,
    .sidebar_width = 180,
};

static gboolean palette_initialized = FALSE;

static void
ensure_palette(void)
{
    if (palette_initialized) return;
    for (int i = 0; i < KTERM_PALETTE_SIZE; i++)
        gdk_rgba_parse(&default_settings.palette[i], default_palette_hex[i]);
    palette_initialized = TRUE;
}

KtermSettings *
kterm_settings_get_default(void)
{
    if (!default_settings.font_family)
        default_settings.font_family = g_strdup("Monospace");
    ensure_palette();
    return &default_settings;
}

static char *
get_settings_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "kterm", "settings.ini", NULL);
}

void
kterm_settings_load(KtermSettings *s)
{
    g_autofree char *path = get_settings_path();
    g_autoptr(GKeyFile) kf = g_key_file_new();

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL))
        return;

    g_autoptr(GError) err = NULL;
    char *val;

    val = g_key_file_get_string(kf, "font", "family", NULL);
    if (val) { g_free(s->font_family); s->font_family = val; }

    double d = g_key_file_get_double(kf, "font", "size", &err);
    if (!err && d > 0) s->font_size = d;
    g_clear_error(&err);

    val = g_key_file_get_string(kf, "colors", "foreground", NULL);
    if (val) { gdk_rgba_parse(&s->foreground, val); g_free(val); }

    val = g_key_file_get_string(kf, "colors", "background", NULL);
    if (val) { gdk_rgba_parse(&s->background, val); g_free(val); }

    /* Load 16-color palette */
    for (int i = 0; i < KTERM_PALETTE_SIZE; i++) {
        char key[16];
        snprintf(key, sizeof(key), "color%d", i);
        val = g_key_file_get_string(kf, "palette", key, NULL);
        if (val) { gdk_rgba_parse(&s->palette[i], val); g_free(val); }
    }

    val = g_key_file_get_string(kf, "cursor", "shape", NULL);
    if (val) {
        if (g_strcmp0(val, "ibeam") == 0)
            s->cursor_shape = VTE_CURSOR_SHAPE_IBEAM;
        else if (g_strcmp0(val, "underline") == 0)
            s->cursor_shape = VTE_CURSOR_SHAPE_UNDERLINE;
        else
            s->cursor_shape = VTE_CURSOR_SHAPE_BLOCK;
        g_free(val);
    }

    val = g_key_file_get_string(kf, "cursor", "blink", NULL);
    if (val) {
        if (g_strcmp0(val, "on") == 0)
            s->cursor_blink = VTE_CURSOR_BLINK_ON;
        else if (g_strcmp0(val, "off") == 0)
            s->cursor_blink = VTE_CURSOR_BLINK_OFF;
        else
            s->cursor_blink = VTE_CURSOR_BLINK_SYSTEM;
        g_free(val);
    }

    int n = g_key_file_get_integer(kf, "general", "scrollback", &err);
    if (!err && n > 0) s->scrollback_lines = n;
    g_clear_error(&err);

    gboolean bell = g_key_file_get_boolean(kf, "general", "bell", &err);
    if (!err) s->audible_bell = bell;
    g_clear_error(&err);

    int border = g_key_file_get_integer(kf, "general", "pane_border", &err);
    if (!err && border >= 0) s->pane_border_size = border;
    g_clear_error(&err);

    gboolean outline = g_key_file_get_boolean(kf, "general", "pane_outline", &err);
    if (!err) s->show_pane_outline = outline;
    g_clear_error(&err);

    gboolean sidebar_vis = g_key_file_get_boolean(kf, "general", "sidebar_visible", &err);
    if (!err) s->sidebar_visible = sidebar_vis;
    g_clear_error(&err);

    int sidebar_pos = g_key_file_get_integer(kf, "general", "sidebar_position", &err);
    if (!err && (sidebar_pos == 0 || sidebar_pos == 1)) s->sidebar_position = sidebar_pos;
    g_clear_error(&err);

    int sidebar_w = g_key_file_get_integer(kf, "general", "sidebar_width", &err);
    if (!err && sidebar_w > 0) s->sidebar_width = sidebar_w;
    g_clear_error(&err);
}

static void
rgba_to_hex(const GdkRGBA *c, char *buf, size_t len)
{
    snprintf(buf, len, "#%02x%02x%02x",
             (int)(c->red * 255), (int)(c->green * 255), (int)(c->blue * 255));
}

void
kterm_settings_save(KtermSettings *s)
{
    g_autofree char *path = get_settings_path();
    g_autofree char *dir  = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);

    g_autoptr(GKeyFile) kf = g_key_file_new();

    g_key_file_set_string(kf, "font", "family", s->font_family);
    g_key_file_set_double(kf, "font", "size", s->font_size);

    char hex[8];
    rgba_to_hex(&s->foreground, hex, sizeof(hex));
    g_key_file_set_string(kf, "colors", "foreground", hex);
    rgba_to_hex(&s->background, hex, sizeof(hex));
    g_key_file_set_string(kf, "colors", "background", hex);

    /* Save 16-color palette */
    for (int i = 0; i < KTERM_PALETTE_SIZE; i++) {
        char key[16];
        snprintf(key, sizeof(key), "color%d", i);
        rgba_to_hex(&s->palette[i], hex, sizeof(hex));
        g_key_file_set_string(kf, "palette", key, hex);
    }

    const char *shape = "block";
    if (s->cursor_shape == VTE_CURSOR_SHAPE_IBEAM)      shape = "ibeam";
    if (s->cursor_shape == VTE_CURSOR_SHAPE_UNDERLINE)   shape = "underline";
    g_key_file_set_string(kf, "cursor", "shape", shape);

    const char *blink = "system";
    if (s->cursor_blink == VTE_CURSOR_BLINK_ON)   blink = "on";
    if (s->cursor_blink == VTE_CURSOR_BLINK_OFF)  blink = "off";
    g_key_file_set_string(kf, "cursor", "blink", blink);

    g_key_file_set_integer(kf, "general", "scrollback", s->scrollback_lines);
    g_key_file_set_boolean(kf, "general", "bell", s->audible_bell);
    g_key_file_set_integer(kf, "general", "pane_border", s->pane_border_size);
    g_key_file_set_boolean(kf, "general", "pane_outline", s->show_pane_outline);
    g_key_file_set_boolean(kf, "general", "sidebar_visible", s->sidebar_visible);
    g_key_file_set_integer(kf, "general", "sidebar_position", s->sidebar_position);
    g_key_file_set_integer(kf, "general", "sidebar_width", s->sidebar_width);

    g_key_file_save_to_file(kf, path, NULL);
}

void
kterm_settings_apply_to_terminal(KtermSettings *s, VteTerminal *vte)
{
    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, s->font_family);
    pango_font_description_set_size(desc, (int)(s->font_size * PANGO_SCALE));
    vte_terminal_set_font(vte, desc);
    pango_font_description_free(desc);

    /* Set full palette including foreground, background, and ANSI colors */
    vte_terminal_set_colors(vte, &s->foreground, &s->background,
                            s->palette, KTERM_PALETTE_SIZE);

    vte_terminal_set_cursor_shape(vte, s->cursor_shape);
    vte_terminal_set_cursor_blink_mode(vte, s->cursor_blink);
    vte_terminal_set_scrollback_lines(vte, s->scrollback_lines);
    vte_terminal_set_audible_bell(vte, s->audible_bell);
}
