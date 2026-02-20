#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define KTERM_TYPE_APPLICATION (kterm_application_get_type())
G_DECLARE_FINAL_TYPE(KtermApplication, kterm_application, KTERM, APPLICATION, GtkApplication)

KtermApplication *kterm_application_new(void);

G_END_DECLS
