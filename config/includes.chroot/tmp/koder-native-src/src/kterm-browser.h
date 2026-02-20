#pragma once

#include <gtk/gtk.h>

GtkWidget  *kterm_browser_new(const char *url);
GtkWidget  *kterm_browser_get_focusable(GtkWidget *browser_box);
GtkWidget  *kterm_browser_get_title_label(GtkWidget *browser_box);
const char *kterm_browser_get_url(GtkWidget *browser_box);
gboolean    kterm_browser_is_browser(GtkWidget *box);
