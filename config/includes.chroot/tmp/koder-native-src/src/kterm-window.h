#pragma once

#include <gtk/gtk.h>
#include "kterm-application.h"

G_BEGIN_DECLS

#define KTERM_TYPE_WINDOW (kterm_window_get_type())
G_DECLARE_FINAL_TYPE(KtermWindow, kterm_window, KTERM, WINDOW, GtkApplicationWindow)

KtermWindow *kterm_window_new(KtermApplication *app);
void         kterm_window_apply_settings(KtermWindow *self);

G_END_DECLS
