#pragma once

#include <gtk/gtk.h>
#include <vte/vte.h>

#define KTERM_PALETTE_SIZE 16

typedef struct {
    char           *font_family;
    double          font_size;
    GdkRGBA         foreground;
    GdkRGBA         background;
    GdkRGBA         palette[KTERM_PALETTE_SIZE];
    VteCursorShape  cursor_shape;
    VteCursorBlinkMode cursor_blink;
    int             scrollback_lines;
    gboolean        audible_bell;
    int             pane_border_size;
    gboolean        show_pane_outline;
    gboolean        sidebar_visible;
    int             sidebar_position;  /* 0 = left, 1 = right */
    int             sidebar_width;
} KtermSettings;

KtermSettings *kterm_settings_get_default(void);
void           kterm_settings_load(KtermSettings *s);
void           kterm_settings_save(KtermSettings *s);
void           kterm_settings_apply_to_terminal(KtermSettings *s, VteTerminal *vte);
