#include "kterm-terminal.h"
#include "kterm-settings.h"
#include <libintl.h>
#include <stdio.h>

#define _(x) gettext(x)
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

static void
on_child_exited(VteTerminal *vte, int status, gpointer user_data)
{
    (void)vte;
    (void)status;
    GtkWidget *terminal_box = GTK_WIDGET(user_data);
    g_signal_emit_by_name(terminal_box, "terminal-exited");
}

static void
on_close_button_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    GtkWidget *terminal_box = GTK_WIDGET(user_data);
    g_signal_emit_by_name(terminal_box, "terminal-exited");
}

static void
spawn_callback(VteTerminal *terminal, GPid pid, GError *error, gpointer user_data)
{
    (void)user_data;
    if (error) {
        g_warning("Failed to spawn shell: %s", error->message);
        return;
    }
    g_object_set_data(G_OBJECT(terminal), "child-pid", GINT_TO_POINTER((int)pid));
}

VteTerminal *
kterm_terminal_get_vte(GtkWidget *terminal_box)
{
    for (GtkWidget *child = gtk_widget_get_first_child(terminal_box);
         child; child = gtk_widget_get_next_sibling(child)) {
        if (GTK_IS_SCROLLED_WINDOW(child)) {
            GtkWidget *sw_child =
                gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(child));
            if (sw_child && VTE_IS_TERMINAL(sw_child))
                return VTE_TERMINAL(sw_child);
        }
    }
    return NULL;
}

GtkWidget *
kterm_terminal_get_title_label(GtkWidget *terminal_box)
{
    return g_object_get_data(G_OBJECT(terminal_box), "kterm-title-label");
}

char *
kterm_terminal_get_cwd(VteTerminal *vte)
{
    /* Try foreground process group via PTY */
    VtePty *pty = vte_terminal_get_pty(vte);
    if (pty) {
        int fd = vte_pty_get_fd(pty);
        if (fd >= 0) {
            pid_t fg = tcgetpgrp(fd);
            if (fg > 0) {
                char path[64];
                snprintf(path, sizeof(path), "/proc/%d/cwd", (int)fg);
                char *cwd = g_file_read_link(path, NULL);
                if (cwd) return cwd;
            }
        }
    }

    /* Fallback: stored child PID */
    int pid = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(vte), "child-pid"));
    if (pid > 0) {
        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/cwd", pid);
        return g_file_read_link(path, NULL);
    }

    return NULL;
}

GtkWidget *
kterm_terminal_new(void)
{
    /* Outer container: vertical box with title bar + scrolled terminal */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_vexpand(box, TRUE);

    /* Register "terminal-exited" signal on GtkBox */
    static gboolean signal_registered = FALSE;
    if (!signal_registered) {
        g_signal_new("terminal-exited",
                      GTK_TYPE_BOX,
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
        signal_registered = TRUE;
    }

    /* Title bar: horizontal box with label + close button */
    GtkWidget *title_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(title_bar, "kterm-title-bar");

    GtkWidget *title_label = gtk_label_new(_("Terminal"));
    gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
    gtk_widget_set_hexpand(title_label, TRUE);
    gtk_box_append(GTK_BOX(title_bar), title_label);

    GtkWidget *close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(close_btn), FALSE);
    gtk_widget_set_tooltip_text(close_btn, _("Close Terminal"));
    gtk_widget_set_valign(close_btn, GTK_ALIGN_CENTER);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_close_button_clicked), box);
    gtk_box_append(GTK_BOX(title_bar), close_btn);

    gtk_box_append(GTK_BOX(box), title_bar);

    /* Store title label reference on the container */
    g_object_set_data(G_OBJECT(box), "kterm-title-label", title_label);

    /* Scrolled window with VTE terminal */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);

    GtkWidget *vte = vte_terminal_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), vte);
    gtk_widget_set_hexpand(vte, TRUE);
    gtk_widget_set_vexpand(vte, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_box_append(GTK_BOX(box), scrolled);

    /* Apply user settings */
    KtermSettings *settings = kterm_settings_get_default();
    kterm_settings_apply_to_terminal(settings, VTE_TERMINAL(vte));

    vte_terminal_set_scroll_on_output(VTE_TERMINAL(vte), FALSE);
    vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(vte), TRUE);

    /* Connect child-exited to propagate via outer box */
    g_signal_connect(vte, "child-exited", G_CALLBACK(on_child_exited), box);

    /* Spawn user's shell */
    const char *shell = g_getenv("SHELL");
    if (!shell)
        shell = "/bin/bash";

    char *argv[] = { (char *)shell, NULL };

    vte_terminal_spawn_async(VTE_TERMINAL(vte),
                             VTE_PTY_DEFAULT,
                             NULL,       /* working directory (inherit) */
                             argv,
                             NULL,       /* envv (inherit) */
                             G_SPAWN_DEFAULT,
                             NULL, NULL, /* child setup */
                             NULL,       /* child setup destroy */
                             -1,         /* timeout */
                             NULL,       /* cancellable */
                             spawn_callback,
                             NULL);

    return box;
}

GtkWidget *
kterm_terminal_new_with_cwd(const char *cwd)
{
    GtkWidget *box = kterm_terminal_new();
    if (!cwd) return box;

    /* The terminal was already spawned with inherited CWD.
     * We need to respawn in the desired directory. */
    VteTerminal *vte = kterm_terminal_get_vte(box);
    if (!vte) return box;

    /* Reset the terminal and respawn with the correct CWD */
    vte_terminal_reset(vte, TRUE, TRUE);

    const char *shell = g_getenv("SHELL");
    if (!shell) shell = "/bin/bash";

    char *argv[] = { (char *)shell, NULL };

    vte_terminal_spawn_async(vte,
                             VTE_PTY_DEFAULT,
                             cwd,
                             argv,
                             NULL,
                             G_SPAWN_DEFAULT,
                             NULL, NULL,
                             NULL,
                             -1,
                             NULL,
                             spawn_callback,
                             NULL);

    return box;
}
