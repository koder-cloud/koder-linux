#include "kterm-application.h"
#include <locale.h>
#include <libintl.h>

int
main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    textdomain(GETTEXT_PACKAGE);

    g_autoptr(KtermApplication) app = kterm_application_new();
    return g_application_run(G_APPLICATION(app), argc, argv);
}
