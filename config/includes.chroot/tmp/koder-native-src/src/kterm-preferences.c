#include "kterm-preferences.h"
#include "kterm-settings.h"
#include "kterm-window.h"
#include <libintl.h>
#include <math.h>

#define _(x) gettext(x)

/* ── Theme presets ────────────────────────────────────────────────── */

typedef struct {
    const char *name;
    const char *fg;
    const char *bg;
    const char *palette[KTERM_PALETTE_SIZE];
} ThemePreset;

static const ThemePreset theme_presets[] = {
    /* ── Dark themes ─────────────────────────────────────────────── */
    { "Catppuccin Mocha", "#cdd6f4", "#1e1e2e", {
        "#45475a","#f38ba8","#a6e3a1","#f9e2af","#89b4fa","#f5c2e7","#94e2d5","#bac2de",
        "#585b70","#f38ba8","#a6e3a1","#f9e2af","#89b4fa","#f5c2e7","#94e2d5","#a6adc8" }},
    { "Catppuccin Macchiato", "#cad3f5", "#24273a", {
        "#494d64","#ed8796","#a6da95","#eed49f","#8aadf4","#f5bde6","#8bd5ca","#b8c0e0",
        "#5b6078","#ed8796","#a6da95","#eed49f","#8aadf4","#f5bde6","#8bd5ca","#a5adcb" }},
    { "Catppuccin Frapp\u00e9", "#c6d0f5", "#303446", {
        "#51576d","#e78284","#a6d189","#e5c890","#8caaee","#f4b8e4","#81c8be","#b5bfe2",
        "#626880","#e78284","#a6d189","#e5c890","#8caaee","#f4b8e4","#81c8be","#a5adce" }},
    { "Dracula", "#f8f8f2", "#282a36", {
        "#21222c","#ff5555","#50fa7b","#f1fa8c","#bd93f9","#ff79c6","#8be9fd","#f8f8f2",
        "#6272a4","#ff6e6e","#69ff94","#ffffa5","#d6acff","#ff92df","#a4ffff","#ffffff" }},
    { "Nord", "#d8dee9", "#2e3440", {
        "#3b4252","#bf616a","#a3be8c","#ebcb8b","#81a1c1","#b48ead","#88c0d0","#e5e9f0",
        "#4c566a","#bf616a","#a3be8c","#ebcb8b","#81a1c1","#b48ead","#8fbcbb","#eceff4" }},
    { "Solarized Dark", "#839496", "#002b36", {
        "#073642","#dc322f","#859900","#b58900","#268bd2","#d33682","#2aa198","#eee8d5",
        "#002b36","#cb4b16","#586e75","#657b83","#839496","#6c71c4","#93a1a1","#fdf6e3" }},
    { "Gruvbox Dark", "#ebdbb2", "#282828", {
        "#282828","#cc241d","#98971a","#d79921","#458588","#b16286","#689d6a","#a89984",
        "#928374","#fb4934","#b8bb26","#fabd2f","#83a598","#d3869b","#8ec07c","#ebdbb2" }},
    { "One Dark", "#abb2bf", "#282c34", {
        "#282c34","#e06c75","#98c379","#e5c07b","#61afef","#c678dd","#56b6c2","#abb2bf",
        "#545862","#e06c75","#98c379","#e5c07b","#61afef","#c678dd","#56b6c2","#c8ccd4" }},
    { "Tokyo Night", "#a9b1d6", "#1a1b26", {
        "#15161e","#f7768e","#9ece6a","#e0af68","#7aa2f7","#bb9af7","#7dcfff","#a9b1d6",
        "#414868","#f7768e","#9ece6a","#e0af68","#7aa2f7","#bb9af7","#7dcfff","#c0caf5" }},
    { "Monokai Pro", "#fcfcfa", "#2d2a2e", {
        "#2d2a2e","#ff6188","#a9dc76","#ffd866","#fc9867","#ab9df2","#78dce8","#fcfcfa",
        "#727072","#ff6188","#a9dc76","#ffd866","#fc9867","#ab9df2","#78dce8","#fcfcfa" }},
    { "Kanagawa", "#dcd7ba", "#1f1f28", {
        "#16161d","#c34043","#76946a","#c0a36e","#7e9cd8","#957fb8","#6a9589","#dcd7ba",
        "#727169","#e82424","#98bb6c","#e6c384","#7fb4ca","#938aa9","#7aa89f","#c8c093" }},
    { "Ros\u00e9 Pine", "#e0def4", "#191724", {
        "#26233a","#eb6f92","#31748f","#f6c177","#9ccfd8","#c4a7e7","#ebbcba","#e0def4",
        "#6e6a86","#eb6f92","#31748f","#f6c177","#9ccfd8","#c4a7e7","#ebbcba","#e0def4" }},
    { "Everforest Dark", "#d3c6aa", "#2d353b", {
        "#4b565c","#e67e80","#a7c080","#dbbc7f","#7fbbb3","#d699b6","#83c092","#d3c6aa",
        "#859289","#e67e80","#a7c080","#dbbc7f","#7fbbb3","#d699b6","#83c092","#d3c6aa" }},
    { "Ayu Dark", "#bfbdb6", "#0d1017", {
        "#0d1017","#f07178","#aad94c","#e6b450","#39bae6","#d2a6ff","#95e6cb","#bfbdb6",
        "#636a72","#ff3333","#b8cc52","#ffb454","#36a3d9","#f07178","#95e6cb","#e6e1cf" }},
    { "Material Ocean", "#8f93a2", "#0f111a", {
        "#0f111a","#ff5370","#c3e88d","#ffcb6b","#82aaff","#c792ea","#89ddff","#8f93a2",
        "#464b5d","#ff5370","#c3e88d","#ffcb6b","#82aaff","#c792ea","#89ddff","#d0d0d0" }},
    { "Nightfox", "#cdcecf", "#192330", {
        "#393b44","#c94f6d","#81b29a","#dbc074","#719cd6","#9d79d6","#63cdcf","#dfdfe0",
        "#575860","#d16983","#8ebaa4","#e0c989","#86abdc","#baa1e2","#7ad5d6","#e4e4e5" }},
    { "Palenight", "#a6accd", "#292d3e", {
        "#292d3e","#ff5370","#c3e88d","#ffcb6b","#82aaff","#c792ea","#89ddff","#a6accd",
        "#676e95","#ff5370","#c3e88d","#ffcb6b","#82aaff","#c792ea","#89ddff","#ffffff" }},
    { "Synthwave 84", "#f92aad", "#2b213a", {
        "#2b213a","#fe4450","#72f1b8","#fede5d","#03edf9","#ff7edb","#03edf9","#f92aad",
        "#614d85","#fe4450","#72f1b8","#f97e72","#03edf9","#ff7edb","#03edf9","#ffffff" }},
    { "Cyberpunk", "#00ff9c", "#000b1e", {
        "#000b1e","#ff003c","#00ff9c","#fffc58","#0abdc6","#711c91","#00ff9c","#d7d7d5",
        "#383838","#ff003c","#00ff9c","#fffc58","#0abdc6","#ea00d9","#00ff9c","#ffffff" }},
    { "GNOME Dark", "#d0cfcc", "#171421", {
        "#171421","#c01c28","#26a269","#a2734c","#12488b","#a347ba","#2aa1b3","#d0cfcc",
        "#5e5c64","#f66151","#33d17a","#e5a50a","#2a7bde","#c061cb","#33c7de","#ffffff" }},
    { "Horizon Dark", "#e0e0e0", "#1c1e26", {
        "#16161c","#e95678","#29d398","#fab795","#26bbd9","#ee64ac","#59e3e3","#d5d8da",
        "#232530","#ec6a88","#3fdaa4","#fbc3a7","#3fc4de","#f075b5","#6be4e6","#cbced0" }},
    { "Doom One", "#bbc2cf", "#282c34", {
        "#1b2229","#ff6c6b","#98be65","#ecbe7b","#51afef","#c678dd","#46d9ff","#bbc2cf",
        "#3f444a","#ff6c6b","#98be65","#ecbe7b","#51afef","#c678dd","#46d9ff","#dfdfdf" }},
    { "Cobalt2", "#ffffff", "#193549", {
        "#000000","#ff0000","#38de21","#ffe50a","#1460d2","#ff005d","#00bbbb","#bbbbbb",
        "#555555","#f40e17","#3bd01d","#edc809","#5555ff","#ff55ff","#6ae3fa","#ffffff" }},
    { "Zenburn", "#dcdccc", "#3f3f3f", {
        "#3f3f3f","#cc9393","#7f9f7f","#e3ceab","#dfaf8f","#dc8cc3","#93e0e3","#dcdccc",
        "#6f6f6f","#dca3a3","#bfebbf","#f0dfaf","#93b6ff","#dc8cc3","#93e0e3","#ffffff" }},
    { "Sonokai", "#e2e2e3", "#2c2e34", {
        "#414550","#fc5d7c","#9ed072","#e7c664","#76cce0","#b39df3","#f39660","#e2e2e3",
        "#7f8490","#fc5d7c","#9ed072","#e7c664","#76cce0","#b39df3","#f39660","#e2e2e3" }},
    { "Afterglow", "#d0d0d0", "#212121", {
        "#151515","#ac4142","#7e8e50","#e5b567","#6c99bb","#9f4e85","#7dd6cf","#d0d0d0",
        "#505050","#ac4142","#7e8e50","#e5b567","#6c99bb","#9f4e85","#7dd6cf","#f5f5f5" }},
    { "Iceberg Dark", "#c6c8d1", "#161821", {
        "#1e2132","#e27878","#b4be82","#e2a478","#84a0c6","#a093c7","#89b8c2","#c6c8d1",
        "#6b7089","#e98989","#c0ca8e","#e9b189","#91acd1","#ada0d3","#95c4ce","#d2d4de" }},
    { "Vitesse Dark", "#dbd7ca", "#121212", {
        "#121212","#cb7676","#4d9375","#e6cc77","#6394bf","#d9739f","#5eaab5","#dbd7ca",
        "#393a34","#cb7676","#4d9375","#e6cc77","#6394bf","#d9739f","#5eaab5","#dbd7ca" }},
    { "Tender", "#eeeeee", "#282828", {
        "#282828","#f43753","#c9d05c","#ffc24b","#b3deef","#d3b987","#73cef4","#eeeeee",
        "#4c4c4c","#f43753","#c9d05c","#ffc24b","#b3deef","#d3b987","#73cef4","#ffffff" }},
    { "Poimandres", "#e4f0fb", "#1b1e28", {
        "#1b1e28","#d0679d","#5de4c7","#fffac2","#89ddff","#fcc5e9","#add7ff","#a6accd",
        "#506477","#d0679d","#5de4c7","#fffac2","#89ddff","#fcc5e9","#add7ff","#e4f0fb" }},
    /* ── Light themes ────────────────────────────────────────────── */
    { "Catppuccin Latte", "#4c4f69", "#eff1f5", {
        "#5c5f77","#d20f39","#40a02b","#df8e1d","#1e66f5","#8839ef","#179299","#acb0be",
        "#6c6f85","#d20f39","#40a02b","#df8e1d","#1e66f5","#8839ef","#179299","#bcc0cc" }},
    { "Solarized Light", "#657b83", "#fdf6e3", {
        "#073642","#dc322f","#859900","#b58900","#268bd2","#d33682","#2aa198","#eee8d5",
        "#002b36","#cb4b16","#586e75","#657b83","#839496","#6c71c4","#93a1a1","#fdf6e3" }},
    { "Gruvbox Light", "#3c3836", "#fbf1c7", {
        "#282828","#cc241d","#98971a","#d79921","#458588","#b16286","#689d6a","#a89984",
        "#928374","#9d0006","#79740e","#b57614","#076678","#8f3f71","#427b58","#3c3836" }},
    { "One Light", "#383a42", "#fafafa", {
        "#383a42","#e45649","#50a14f","#c18401","#4078f2","#a626a4","#0184bc","#a0a1a7",
        "#696c77","#e45649","#50a14f","#c18401","#4078f2","#a626a4","#0184bc","#202227" }},
    { "Nord Light", "#2e3440", "#eceff4", {
        "#2e3440","#bf616a","#a3be8c","#ebcb8b","#81a1c1","#b48ead","#88c0d0","#d8dee9",
        "#4c566a","#bf616a","#a3be8c","#ebcb8b","#81a1c1","#b48ead","#8fbcbb","#eceff4" }},
    { "Ros\u00e9 Pine Dawn", "#575279", "#faf4ed", {
        "#575279","#b4637a","#286983","#ea9d34","#56949f","#907aa9","#d7827e","#575279",
        "#9893a5","#b4637a","#286983","#ea9d34","#56949f","#907aa9","#d7827e","#575279" }},
    { "Everforest Light", "#5c6a72", "#fdf6e3", {
        "#5c6a72","#f85552","#8da101","#dfa000","#3a94c5","#df69ba","#35a77c","#dfd7c0",
        "#829181","#f85552","#8da101","#dfa000","#3a94c5","#df69ba","#35a77c","#5c6a72" }},
    { "Ayu Light", "#5c6166", "#fafafa", {
        "#5c6166","#f07178","#86b300","#f2ae49","#36a3d9","#a37acc","#4dbf99","#828c99",
        "#8a9199","#ff3333","#b8cc52","#ffb454","#36a3d9","#a37acc","#4dbf99","#5c6166" }},
    { "Tokyo Night Light", "#343b58", "#d5d6db", {
        "#343b58","#8c4351","#485e30","#8f5e15","#34548a","#5a4a78","#0f4b6e","#343b58",
        "#634f30","#8c4351","#485e30","#8f5e15","#34548a","#5a4a78","#0f4b6e","#343b58" }},
    { "GitHub Light", "#24292e", "#ffffff", {
        "#24292e","#cb2431","#22863a","#b08800","#0366d6","#6f42c1","#1b7c83","#6a737d",
        "#959da5","#cb2431","#28a745","#dbab09","#2188ff","#8a63d2","#3192aa","#24292e" }},
    { "Paper", "#444444", "#eeeeee", {
        "#444444","#cc3e28","#216609","#b58900","#1e6fcc","#5c21a5","#158c86","#aaaaaa",
        "#666666","#cc3e28","#216609","#b58900","#1e6fcc","#5c21a5","#158c86","#444444" }},
    { "Tango Light", "#2e3436", "#ffffff", {
        "#2e3436","#cc0000","#4e9a06","#c4a000","#3465a4","#75507b","#06989a","#d3d7cf",
        "#555753","#ef2929","#8ae234","#fce94f","#729fcf","#ad7fa8","#34e2e2","#eeeeec" }},
};

#define N_PRESETS (int)(sizeof(theme_presets) / sizeof(theme_presets[0]))

/* ── Preferences dialog state ─────────────────────────────────────── */

typedef struct {
    GtkWindow   *dialog;
    GtkWindow   *parent;
    GtkNotebook *notebook;
    /* Color page widgets */
    GtkWidget   *fg_button;
    GtkWidget   *bg_button;
    /* Font page widgets */
    GtkWidget   *font_button;
    /* General page widgets */
    GtkWidget   *scrollback_spin;
    GtkWidget   *unlimited_sw;
    GtkWidget   *bell_sw;
    GtkWidget   *border_spin;
    GtkWidget   *outline_sw;
    /* Sidebar widgets */
    GtkWidget   *sidebar_sw;
    GtkWidget   *sidebar_pos_dd;
    /* Cursor page widgets */
    GtkWidget   *shape_dd;
    GtkWidget   *blink_dd;
    /* Active theme tracking */
    GtkWidget   *active_theme_btn;
    /* State */
    KtermSettings backup;       /* Snapshot taken when dialog opens */
    gboolean      committed;    /* TRUE after Ok or Apply was clicked */
} PrefsData;

/* ── Settings snapshot helpers ────────────────────────────────────── */

static void
settings_snapshot(KtermSettings *dst, const KtermSettings *src)
{
    *dst = *src;
    dst->font_family = g_strdup(src->font_family);
}

static void
settings_restore(KtermSettings *dst, const KtermSettings *src)
{
    g_free(dst->font_family);
    *dst = *src;
    dst->font_family = g_strdup(src->font_family);
}

static void
settings_snapshot_free(KtermSettings *s)
{
    g_free(s->font_family);
    s->font_family = NULL;
}

/* ── Live-apply helper ─────────────────────────────────────────────── */

static void
live_apply(PrefsData *pd)
{
    if (pd->parent && KTERM_IS_WINDOW(pd->parent))
        kterm_window_apply_settings(KTERM_WINDOW(pd->parent));
}

/* ── Callbacks (modify settings and apply live for instant preview) ── */

static void
on_font_changed(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();
    const PangoFontDescription *desc =
        gtk_font_dialog_button_get_font_desc(GTK_FONT_DIALOG_BUTTON(obj));
    if (!desc) return;

    g_free(s->font_family);
    s->font_family = g_strdup(pango_font_description_get_family(desc));
    int sz = pango_font_description_get_size(desc);
    if (sz > 0)
        s->font_size = (double)sz / PANGO_SCALE;
    live_apply(pd);
}

static void
on_fg_color_changed(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();
    const GdkRGBA *c = gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(obj));
    s->foreground = *c;
    live_apply(pd);
}

static void
on_bg_color_changed(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();
    const GdkRGBA *c = gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(obj));
    s->background = *c;
    live_apply(pd);
}

static void
on_cursor_shape_changed(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
    switch (sel) {
    case 0:  s->cursor_shape = VTE_CURSOR_SHAPE_BLOCK;     break;
    case 1:  s->cursor_shape = VTE_CURSOR_SHAPE_IBEAM;     break;
    case 2:  s->cursor_shape = VTE_CURSOR_SHAPE_UNDERLINE;  break;
    }
    live_apply(pd);
}

static void
on_cursor_blink_changed(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
    switch (sel) {
    case 0:  s->cursor_blink = VTE_CURSOR_BLINK_SYSTEM;  break;
    case 1:  s->cursor_blink = VTE_CURSOR_BLINK_ON;      break;
    case 2:  s->cursor_blink = VTE_CURSOR_BLINK_OFF;     break;
    }
    live_apply(pd);
}

static void
on_scrollback_changed(GtkSpinButton *spin, gpointer user_data)
{
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();
    s->scrollback_lines = (int)gtk_spin_button_get_value(spin);
    live_apply(pd);
}

static void
on_unlimited_scroll_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();
    GtkWidget *spin = g_object_get_data(G_OBJECT(obj), "spin");
    gboolean active = gtk_switch_get_active(GTK_SWITCH(obj));

    if (active) {
        s->scrollback_lines = -1;
        gtk_widget_set_sensitive(spin, FALSE);
    } else {
        s->scrollback_lines = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
        gtk_widget_set_sensitive(spin, TRUE);
    }
    live_apply(pd);
}

static void
on_bell_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();
    s->audible_bell = gtk_switch_get_active(GTK_SWITCH(obj));
    live_apply(pd);
}

static void
on_pane_border_changed(GtkSpinButton *spin, gpointer user_data)
{
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();
    s->pane_border_size = (int)gtk_spin_button_get_value(spin);
    live_apply(pd);
}

static void
on_pane_outline_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();
    s->show_pane_outline = gtk_switch_get_active(GTK_SWITCH(obj));
    live_apply(pd);
}

static void
on_sidebar_toggled(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();
    s->sidebar_visible = gtk_switch_get_active(GTK_SWITCH(obj));
    live_apply(pd);
}

static void
on_sidebar_position_changed(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
    s->sidebar_position = (int)sel;
    live_apply(pd);
}

static void
on_theme_preset_clicked(GtkButton *button, gpointer user_data)
{
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "preset-index"));
    const ThemePreset *p = &theme_presets[idx];

    gdk_rgba_parse(&s->foreground, p->fg);
    gdk_rgba_parse(&s->background, p->bg);

    /* Apply the theme's 16-color ANSI palette */
    for (int i = 0; i < KTERM_PALETTE_SIZE; i++)
        gdk_rgba_parse(&s->palette[i], p->palette[i]);

    /* Update the color buttons to reflect the new colors */
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(pd->fg_button), &s->foreground);
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(pd->bg_button), &s->background);

    /* Highlight the selected theme button */
    if (pd->active_theme_btn)
        gtk_widget_remove_css_class(pd->active_theme_btn, "theme-selected");
    pd->active_theme_btn = GTK_WIDGET(button);
    gtk_widget_add_css_class(GTK_WIDGET(button), "theme-selected");

    live_apply(pd);
}

/* ── Ok / Cancel button handlers ──────────────────────────────────── */

static void
on_ok_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();

    /* Settings are already applied live; just persist to disk */
    kterm_settings_save(s);
    pd->committed = TRUE;
    gtk_window_close(pd->dialog);
}

static void
on_cancel_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();

    /* Restore settings to the state before dialog opened (or last Apply) */
    settings_restore(s, &pd->backup);
    if (pd->parent && KTERM_IS_WINDOW(pd->parent))
        kterm_window_apply_settings(KTERM_WINDOW(pd->parent));

    pd->committed = TRUE;   /* Prevent double-restore in destroy handler */
    gtk_window_close(pd->dialog);
}

/* ── Default palette (Material Ocean) for restore ─────────────────── */

static const char *default_palette_hex[KTERM_PALETTE_SIZE] = {
    "#0f111a", "#ff5370", "#c3e88d", "#ffcb6b",
    "#82aaff", "#c792ea", "#89ddff", "#8f93a2",
    "#464b5d", "#ff5370", "#c3e88d", "#ffcb6b",
    "#82aaff", "#c792ea", "#89ddff", "#d0d0d0",
};

static void
on_defaults_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    PrefsData *pd = user_data;
    KtermSettings *s = kterm_settings_get_default();
    int page = gtk_notebook_get_current_page(pd->notebook);

    switch (page) {
    case 0: /* General */
        s->scrollback_lines = 10000;
        s->audible_bell = FALSE;
        s->pane_border_size = 2;
        s->show_pane_outline = FALSE;
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(pd->scrollback_spin), 10000);
        gtk_widget_set_sensitive(pd->scrollback_spin, TRUE);
        gtk_switch_set_active(GTK_SWITCH(pd->unlimited_sw), FALSE);
        gtk_switch_set_active(GTK_SWITCH(pd->bell_sw), FALSE);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(pd->border_spin), 2);
        gtk_switch_set_active(GTK_SWITCH(pd->outline_sw), FALSE);
        s->sidebar_visible = TRUE;
        s->sidebar_position = 0;
        gtk_switch_set_active(GTK_SWITCH(pd->sidebar_sw), TRUE);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(pd->sidebar_pos_dd), 0);
        break;

    case 1: { /* Font */
        g_free(s->font_family);
        s->font_family = g_strdup("Monospace");
        s->font_size = 11.0;
        PangoFontDescription *desc = pango_font_description_new();
        pango_font_description_set_family(desc, "Monospace");
        pango_font_description_set_size(desc, (int)(11.0 * PANGO_SCALE));
        gtk_font_dialog_button_set_font_desc(
            GTK_FONT_DIALOG_BUTTON(pd->font_button), desc);
        pango_font_description_free(desc);
        break;
    }

    case 2: /* Colors */
        gdk_rgba_parse(&s->foreground, "#8f93a2");
        gdk_rgba_parse(&s->background, "#0f111a");
        for (int i = 0; i < KTERM_PALETTE_SIZE; i++)
            gdk_rgba_parse(&s->palette[i], default_palette_hex[i]);
        gtk_color_dialog_button_set_rgba(
            GTK_COLOR_DIALOG_BUTTON(pd->fg_button), &s->foreground);
        gtk_color_dialog_button_set_rgba(
            GTK_COLOR_DIALOG_BUTTON(pd->bg_button), &s->background);
        break;

    case 3: /* Cursor */
        s->cursor_shape = VTE_CURSOR_SHAPE_IBEAM;
        s->cursor_blink = VTE_CURSOR_BLINK_SYSTEM;
        gtk_drop_down_set_selected(GTK_DROP_DOWN(pd->shape_dd), 1);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(pd->blink_dd), 0);
        break;
    }
    live_apply(pd);
}

static void
on_prefs_destroy(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    PrefsData *pd = user_data;

    if (!pd->committed) {
        /* Window closed via X button without Ok/Apply — revert changes */
        KtermSettings *s = kterm_settings_get_default();
        settings_restore(s, &pd->backup);
        if (pd->parent && KTERM_IS_WINDOW(pd->parent))
            kterm_window_apply_settings(KTERM_WINDOW(pd->parent));
    }

    settings_snapshot_free(&pd->backup);
    g_free(pd);
}

/* ── Build pages ──────────────────────────────────────────────────── */

static GtkWidget *
make_label(const char *text)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    return l;
}

static GtkWidget *
build_general_page(PrefsData *pd)
{
    KtermSettings *s = kterm_settings_get_default();

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_start(grid, 18);
    gtk_widget_set_margin_end(grid, 18);
    gtk_widget_set_margin_top(grid, 18);
    gtk_widget_set_margin_bottom(grid, 18);

    int row = 0;

    gtk_grid_attach(GTK_GRID(grid), make_label(_("Scrollback lines")), 0, row, 1, 1);
    GtkWidget *spin = gtk_spin_button_new_with_range(100, 1000000, 1000);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin),
                              s->scrollback_lines > 0 ? s->scrollback_lines : 10000);
    if (s->scrollback_lines < 0)
        gtk_widget_set_sensitive(spin, FALSE);
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_scrollback_changed), pd);
    gtk_grid_attach(GTK_GRID(grid), spin, 1, row++, 1, 1);
    pd->scrollback_spin = spin;

    gtk_grid_attach(GTK_GRID(grid), make_label(_("Unlimited scrollback")), 0, row, 1, 1);
    GtkWidget *unlimited_sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(unlimited_sw), s->scrollback_lines < 0);
    gtk_widget_set_halign(unlimited_sw, GTK_ALIGN_START);
    g_object_set_data(G_OBJECT(unlimited_sw), "spin", spin);
    g_signal_connect(unlimited_sw, "notify::active",
                     G_CALLBACK(on_unlimited_scroll_toggled), pd);
    gtk_grid_attach(GTK_GRID(grid), unlimited_sw, 1, row++, 1, 1);
    pd->unlimited_sw = unlimited_sw;

    gtk_grid_attach(GTK_GRID(grid), make_label(_("Audible bell")), 0, row, 1, 1);
    GtkWidget *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), s->audible_bell);
    gtk_widget_set_halign(sw, GTK_ALIGN_START);
    g_signal_connect(sw, "notify::active", G_CALLBACK(on_bell_toggled), pd);
    gtk_grid_attach(GTK_GRID(grid), sw, 1, row++, 1, 1);
    pd->bell_sw = sw;

    gtk_grid_attach(GTK_GRID(grid), make_label(_("Pane border (px)")), 0, row, 1, 1);
    GtkWidget *border_spin = gtk_spin_button_new_with_range(0, 20, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(border_spin), s->pane_border_size);
    g_signal_connect(border_spin, "value-changed", G_CALLBACK(on_pane_border_changed), pd);
    gtk_grid_attach(GTK_GRID(grid), border_spin, 1, row++, 1, 1);
    pd->border_spin = border_spin;

    gtk_grid_attach(GTK_GRID(grid), make_label(_("Focus outline")), 0, row, 1, 1);
    GtkWidget *outline_sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(outline_sw), s->show_pane_outline);
    gtk_widget_set_halign(outline_sw, GTK_ALIGN_START);
    gtk_widget_set_tooltip_text(outline_sw,
        _("Show an outline around the focused tile. When off, only the title bar is highlighted."));
    g_signal_connect(outline_sw, "notify::active",
                     G_CALLBACK(on_pane_outline_toggled), pd);
    gtk_grid_attach(GTK_GRID(grid), outline_sw, 1, row++, 1, 1);
    pd->outline_sw = outline_sw;

    /* Sidebar separator */
    GtkWidget *sidebar_sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_grid_attach(GTK_GRID(grid), sidebar_sep, 0, row++, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), make_label(_("Show sidebar")), 0, row, 1, 1);
    GtkWidget *sidebar_sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sidebar_sw), s->sidebar_visible);
    gtk_widget_set_halign(sidebar_sw, GTK_ALIGN_START);
    g_signal_connect(sidebar_sw, "notify::active",
                     G_CALLBACK(on_sidebar_toggled), pd);
    gtk_grid_attach(GTK_GRID(grid), sidebar_sw, 1, row++, 1, 1);
    pd->sidebar_sw = sidebar_sw;

    gtk_grid_attach(GTK_GRID(grid), make_label(_("Sidebar position")), 0, row, 1, 1);
    const char *positions[] = { _("Left"), _("Right"), NULL };
    GtkWidget *sidebar_pos_dd = gtk_drop_down_new_from_strings(positions);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(sidebar_pos_dd), (guint)s->sidebar_position);
    g_signal_connect(sidebar_pos_dd, "notify::selected",
                     G_CALLBACK(on_sidebar_position_changed), pd);
    gtk_grid_attach(GTK_GRID(grid), sidebar_pos_dd, 1, row++, 1, 1);
    pd->sidebar_pos_dd = sidebar_pos_dd;

    /* Wrap in a scrolled window so the page can scroll if needed */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), grid);
    gtk_widget_set_vexpand(scrolled, TRUE);

    return scrolled;
}

static GtkWidget *
build_font_page(PrefsData *pd)
{
    KtermSettings *s = kterm_settings_get_default();

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_start(grid, 18);
    gtk_widget_set_margin_end(grid, 18);
    gtk_widget_set_margin_top(grid, 18);
    gtk_widget_set_margin_bottom(grid, 18);

    gtk_grid_attach(GTK_GRID(grid), make_label(_("Terminal font")), 0, 0, 1, 1);

    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, s->font_family);
    pango_font_description_set_size(desc, (int)(s->font_size * PANGO_SCALE));

    GtkFontDialog *fd = gtk_font_dialog_new();
    GtkWidget *fb = gtk_font_dialog_button_new(fd);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(fb), desc);
    pango_font_description_free(desc);
    g_signal_connect(fb, "notify::font-desc", G_CALLBACK(on_font_changed), pd);
    gtk_widget_set_hexpand(fb, TRUE);
    gtk_grid_attach(GTK_GRID(grid), fb, 1, 0, 1, 1);
    pd->font_button = fb;

    return grid;
}

/* Check if current settings match a given theme preset (fg + bg) */
static gboolean
colors_match_preset(const KtermSettings *s, const ThemePreset *p)
{
    GdkRGBA pfg, pbg;
    gdk_rgba_parse(&pfg, p->fg);
    gdk_rgba_parse(&pbg, p->bg);
    /* Compare with ~1/256 tolerance to handle float rounding */
    return (fabs(s->foreground.red - pfg.red) < 0.005 &&
            fabs(s->foreground.green - pfg.green) < 0.005 &&
            fabs(s->foreground.blue - pfg.blue) < 0.005 &&
            fabs(s->background.red - pbg.red) < 0.005 &&
            fabs(s->background.green - pbg.green) < 0.005 &&
            fabs(s->background.blue - pbg.blue) < 0.005);
}

static GtkWidget *
build_colors_page(PrefsData *pd)
{
    KtermSettings *s = kterm_settings_get_default();

    /* CSS for the selected theme indicator */
    static gboolean theme_css_loaded = FALSE;
    if (!theme_css_loaded) {
        GtkCssProvider *sel_prov = gtk_css_provider_new();
        gtk_css_provider_load_from_string(sel_prov,
            ".theme-selected {"
            "  outline: 3px solid #ffffff;"
            "  outline-offset: 3px;"
            "  border-radius: 8px;"
            "}");
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(sel_prov),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
        g_object_unref(sel_prov);
        theme_css_loaded = TRUE;
    }

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);

    /* Theme presets header */
    GtkWidget *presets_label = gtk_label_new(_("Theme presets"));
    gtk_widget_set_halign(presets_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(presets_label, "heading");
    gtk_box_append(GTK_BOX(box), presets_label);

    /* Dark themes */
    GtkWidget *dark_label = gtk_label_new(_("Dark"));
    gtk_widget_set_halign(dark_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(dark_label, "dim-label");
    gtk_box_append(GTK_BOX(box), dark_label);

    GtkWidget *dark_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(dark_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(dark_flow), 5);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(dark_flow), 6);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(dark_flow), 6);

    for (int i = 0; i < N_PRESETS; i++) {
        GdkRGBA bg;
        gdk_rgba_parse(&bg, theme_presets[i].bg);
        double luma = 0.299 * bg.red + 0.587 * bg.green + 0.114 * bg.blue;
        if (luma >= 0.5) continue;

        GtkWidget *btn = gtk_button_new_with_label(theme_presets[i].name);
        g_object_set_data(G_OBJECT(btn), "preset-index", GINT_TO_POINTER(i));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_theme_preset_clicked), pd);

        char css_name[64];
        snprintf(css_name, sizeof(css_name), "theme-btn-%d", i);
        gtk_widget_set_name(btn, css_name);

        char css[256];
        snprintf(css, sizeof(css),
                 "#%s { background: %s; color: %s; min-width: 110px; }",
                 css_name, theme_presets[i].bg, theme_presets[i].fg);
        GtkCssProvider *prov = gtk_css_provider_new();
        gtk_css_provider_load_from_string(prov, css);
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(prov),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(prov);

        /* Highlight if this preset matches current colors */
        if (!pd->active_theme_btn && colors_match_preset(s, &theme_presets[i])) {
            gtk_widget_add_css_class(btn, "theme-selected");
            pd->active_theme_btn = btn;
        }

        gtk_flow_box_insert(GTK_FLOW_BOX(dark_flow), btn, -1);
    }
    gtk_box_append(GTK_BOX(box), dark_flow);

    /* Light themes */
    GtkWidget *light_label = gtk_label_new(_("Light"));
    gtk_widget_set_halign(light_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(light_label, "dim-label");
    gtk_box_append(GTK_BOX(box), light_label);

    GtkWidget *light_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(light_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(light_flow), 5);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(light_flow), 6);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(light_flow), 6);

    for (int i = 0; i < N_PRESETS; i++) {
        GdkRGBA bg;
        gdk_rgba_parse(&bg, theme_presets[i].bg);
        double luma = 0.299 * bg.red + 0.587 * bg.green + 0.114 * bg.blue;
        if (luma < 0.5) continue;

        GtkWidget *btn = gtk_button_new_with_label(theme_presets[i].name);
        g_object_set_data(G_OBJECT(btn), "preset-index", GINT_TO_POINTER(i));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_theme_preset_clicked), pd);

        char css_name[64];
        snprintf(css_name, sizeof(css_name), "theme-btn-%d", i);
        gtk_widget_set_name(btn, css_name);

        char css[256];
        snprintf(css, sizeof(css),
                 "#%s { background: %s; color: %s; min-width: 110px; }",
                 css_name, theme_presets[i].bg, theme_presets[i].fg);
        GtkCssProvider *prov = gtk_css_provider_new();
        gtk_css_provider_load_from_string(prov, css);
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(prov),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(prov);

        /* Highlight if this preset matches current colors */
        if (!pd->active_theme_btn && colors_match_preset(s, &theme_presets[i])) {
            gtk_widget_add_css_class(btn, "theme-selected");
            pd->active_theme_btn = btn;
        }

        gtk_flow_box_insert(GTK_FLOW_BOX(light_flow), btn, -1);
    }
    gtk_box_append(GTK_BOX(box), light_flow);

    /* Separator */
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Custom color pickers */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

    gtk_grid_attach(GTK_GRID(grid), make_label(_("Foreground")), 0, 0, 1, 1);
    GtkColorDialog *cd1 = gtk_color_dialog_new();
    GtkWidget *fg_btn = gtk_color_dialog_button_new(cd1);
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(fg_btn), &s->foreground);
    g_signal_connect(fg_btn, "notify::rgba", G_CALLBACK(on_fg_color_changed), pd);
    gtk_grid_attach(GTK_GRID(grid), fg_btn, 1, 0, 1, 1);
    pd->fg_button = fg_btn;

    gtk_grid_attach(GTK_GRID(grid), make_label(_("Background")), 0, 1, 1, 1);
    GtkColorDialog *cd2 = gtk_color_dialog_new();
    GtkWidget *bg_btn = gtk_color_dialog_button_new(cd2);
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(bg_btn), &s->background);
    g_signal_connect(bg_btn, "notify::rgba", G_CALLBACK(on_bg_color_changed), pd);
    gtk_grid_attach(GTK_GRID(grid), bg_btn, 1, 1, 1, 1);
    pd->bg_button = bg_btn;

    gtk_box_append(GTK_BOX(box), grid);

    /* Scrolled wrapper */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), box);
    gtk_widget_set_vexpand(scrolled, TRUE);

    return scrolled;
}

static GtkWidget *
build_cursor_page(PrefsData *pd)
{
    KtermSettings *s = kterm_settings_get_default();

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_start(grid, 18);
    gtk_widget_set_margin_end(grid, 18);
    gtk_widget_set_margin_top(grid, 18);
    gtk_widget_set_margin_bottom(grid, 18);

    int row = 0;

    gtk_grid_attach(GTK_GRID(grid), make_label(_("Cursor shape")), 0, row, 1, 1);
    const char *shapes[] = { _("Block"), _("I-Beam"), _("Underline"), NULL };
    GtkWidget *shape_dd = gtk_drop_down_new_from_strings(shapes);
    guint shape_sel = 0;
    if (s->cursor_shape == VTE_CURSOR_SHAPE_IBEAM)     shape_sel = 1;
    if (s->cursor_shape == VTE_CURSOR_SHAPE_UNDERLINE)  shape_sel = 2;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(shape_dd), shape_sel);
    g_signal_connect(shape_dd, "notify::selected", G_CALLBACK(on_cursor_shape_changed), pd);
    gtk_grid_attach(GTK_GRID(grid), shape_dd, 1, row++, 1, 1);
    pd->shape_dd = shape_dd;

    gtk_grid_attach(GTK_GRID(grid), make_label(_("Cursor blink")), 0, row, 1, 1);
    const char *blinks[] = { _("System"), _("On"), _("Off"), NULL };
    GtkWidget *blink_dd = gtk_drop_down_new_from_strings(blinks);
    guint blink_sel = 0;
    if (s->cursor_blink == VTE_CURSOR_BLINK_ON)   blink_sel = 1;
    if (s->cursor_blink == VTE_CURSOR_BLINK_OFF)  blink_sel = 2;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(blink_dd), blink_sel);
    g_signal_connect(blink_dd, "notify::selected", G_CALLBACK(on_cursor_blink_changed), pd);
    gtk_grid_attach(GTK_GRID(grid), blink_dd, 1, row++, 1, 1);
    pd->blink_dd = blink_dd;

    return grid;
}

/* ── Public API ───────────────────────────────────────────────────── */

void
kterm_preferences_show(GtkWindow *parent)
{
    PrefsData *pd = g_new0(PrefsData, 1);
    pd->parent = parent;

    /* Take a snapshot of the current settings for Cancel/revert */
    KtermSettings *s = kterm_settings_get_default();
    settings_snapshot(&pd->backup, s);

    GtkWidget *win = gtk_window_new();
    pd->dialog = GTK_WINDOW(win);
    gtk_window_set_title(GTK_WINDOW(win), _("Preferences"));
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_default_size(GTK_WINDOW(win), 820, 600);

    g_signal_connect(win, "destroy", G_CALLBACK(on_prefs_destroy), pd);

    /* Close on ESC (same as Cancel) */
    GtkEventController *esc_ctrl = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(esc_ctrl),
                                      GTK_SHORTCUT_SCOPE_LOCAL);
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(esc_ctrl),
        gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Escape, 0),
                         gtk_named_action_new("window.close")));
    gtk_widget_add_controller(win, esc_ctrl);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), vbox);

    /* Notebook with setting pages */
    GtkWidget *notebook = gtk_notebook_new();
    pd->notebook = GTK_NOTEBOOK(notebook);
    gtk_widget_set_vexpand(notebook, TRUE);
    gtk_box_append(GTK_BOX(vbox), notebook);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
        build_general_page(pd), gtk_label_new(_("General")));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
        build_font_page(pd), gtk_label_new(_("Font")));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
        build_colors_page(pd), gtk_label_new(_("Colors")));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
        build_cursor_page(pd), gtk_label_new(_("Cursor")));

    /* ── Bottom panel: separator + button bar ──────────────────── */
    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget *btn_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(btn_bar, 12);
    gtk_widget_set_margin_end(btn_bar, 12);
    gtk_widget_set_margin_top(btn_bar, 8);
    gtk_widget_set_margin_bottom(btn_bar, 8);

    /* "Padrão" on the left */
    GtkWidget *defaults_btn = gtk_button_new_with_label(_("Defaults"));
    gtk_widget_add_css_class(defaults_btn, "destructive-action");
    g_signal_connect(defaults_btn, "clicked", G_CALLBACK(on_defaults_clicked), pd);
    gtk_box_append(GTK_BOX(btn_bar), defaults_btn);

    /* Spacer pushes the remaining buttons to the right */
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(btn_bar), spacer);

    /* Ok / Cancel grouped on the right */
    GtkWidget *ok_btn = gtk_button_new_with_label(_("OK"));
    gtk_widget_add_css_class(ok_btn, "suggested-action");
    g_signal_connect(ok_btn, "clicked", G_CALLBACK(on_ok_clicked), pd);

    GtkWidget *cancel_btn = gtk_button_new_with_label(_("Cancel"));
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_cancel_clicked), pd);

    gtk_box_append(GTK_BOX(btn_bar), cancel_btn);
    gtk_box_append(GTK_BOX(btn_bar), ok_btn);

    gtk_box_append(GTK_BOX(vbox), btn_bar);

    gtk_window_present(GTK_WINDOW(win));
}
