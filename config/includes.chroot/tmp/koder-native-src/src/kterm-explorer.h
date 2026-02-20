#pragma once

#include <gtk/gtk.h>

GtkWidget  *kterm_explorer_new(const char *path);
GtkWidget  *kterm_explorer_get_focusable(GtkWidget *explorer_box);
GtkWidget  *kterm_explorer_get_left_focusable(GtkWidget *explorer_box);
GtkWidget  *kterm_explorer_get_list_focusable(GtkWidget *explorer_box);
GtkWidget  *kterm_explorer_get_grid_focusable(GtkWidget *explorer_box);
GtkWidget  *kterm_explorer_get_title_label(GtkWidget *explorer_box);
const char *kterm_explorer_get_path(GtkWidget *explorer_box);
gboolean    kterm_explorer_is_explorer(GtkWidget *box);
