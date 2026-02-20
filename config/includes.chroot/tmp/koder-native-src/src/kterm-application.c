#include "kterm-application.h"
#include "kterm-window.h"
#include "kterm-settings.h"

struct _KtermApplication {
    GtkApplication parent_instance;
};

G_DEFINE_TYPE(KtermApplication, kterm_application, GTK_TYPE_APPLICATION)

static void
kterm_application_activate(GApplication *app)
{
    KtermSettings *s = kterm_settings_get_default();
    kterm_settings_load(s);

    gtk_window_set_default_icon_name("com.koder.kterm");

    KtermWindow *window = kterm_window_new(KTERM_APPLICATION(app));
    gtk_window_present(GTK_WINDOW(window));
}

static void
kterm_application_class_init(KtermApplicationClass *klass)
{
    G_APPLICATION_CLASS(klass)->activate = kterm_application_activate;
}

static void
kterm_application_init(KtermApplication *self)
{
    (void)self;
}

KtermApplication *
kterm_application_new(void)
{
    return g_object_new(KTERM_TYPE_APPLICATION,
                        "application-id", "com.koder.kterm",
                        "flags", G_APPLICATION_DEFAULT_FLAGS,
                        NULL);
}
