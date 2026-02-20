#pragma once

#include <gtk/gtk.h>
#include <vte/vte.h>

G_BEGIN_DECLS

/* Creates a terminal container: GtkBox with a title bar label + GtkScrolledWindow
 * containing a VteTerminal with shell spawned.
 * The container emits "terminal-exited" signal when the child process exits. */
GtkWidget *kterm_terminal_new(void);

/* Like kterm_terminal_new(), but spawns the shell in the given working directory. */
GtkWidget *kterm_terminal_new_with_cwd(const char *cwd);

/* Returns the VteTerminal inside a terminal container created by kterm_terminal_new(). */
VteTerminal *kterm_terminal_get_vte(GtkWidget *terminal_box);

/* Returns the title GtkLabel inside a terminal container. */
GtkWidget *kterm_terminal_get_title_label(GtkWidget *terminal_box);

/* Returns the current working directory of the terminal's foreground process.
 * Caller must g_free() the result. Returns NULL on failure. */
char *kterm_terminal_get_cwd(VteTerminal *vte);

G_END_DECLS
