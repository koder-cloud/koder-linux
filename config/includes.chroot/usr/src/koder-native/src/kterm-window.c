#include "kterm-window.h"
#include "kterm-terminal.h"
#include "kterm-explorer.h"
#include "kterm-browser.h"
#include "kterm-settings.h"
#include "kterm-preferences.h"
#include <libintl.h>
#include <json-glib/json-glib.h>

#define _(x) gettext(x)

struct _KtermWindow {
    GtkApplicationWindow parent_instance;
    GtkNotebook *notebook;
    VteTerminal *active_terminal;
    GtkWidget   *active_pane;       /* Currently focused pane box (terminal or explorer) */

    /* Sidebar */
    GtkWidget   *main_paned;       /* GtkPaned: sidebar | notebook */
    GtkWidget   *sidebar;          /* Vertical box containing header + scrolled tab list */
    GtkWidget   *sidebar_tab_list; /* Vertical box inside scrolled window with thumbnails */
    GtkWidget   *sidebar_count_label; /* "N tabs" label in sidebar header */
    guint        thumb_timer_id;   /* Periodic thumbnail refresh timer */
};

G_DEFINE_TYPE(KtermWindow, kterm_window, GTK_TYPE_APPLICATION_WINDOW)

/* ── Forward declarations ─────────────────────────────────────────── */

static void     add_tab(KtermWindow *self);
static void     close_current_pane(KtermWindow *self);
static void     split_terminal(KtermWindow *self, GtkOrientation orientation);
static void     split_with_explorer(KtermWindow *self, GtkOrientation orientation);
static void     navigate(KtermWindow *self, int dx, int dy);
static void     setup_focus_tracking(KtermWindow *self, GtkWidget *terminal_box);
static void     setup_explorer_tracking(KtermWindow *self, GtkWidget *explorer_box);
static void     setup_browser_tracking(KtermWindow *self, GtkWidget *browser_box);
static void     split_with_browser(KtermWindow *self, GtkOrientation orientation);
static void     on_terminal_exited(GtkWidget *terminal_box, gpointer user_data);
static void     collect_panes(GtkWidget *widget, GPtrArray *panes);
static GtkWidget *find_terminal_box(VteTerminal *vte);
static void     update_tab_title(VteTerminal *vte, GParamSpec *pspec, gpointer user_data);
static void     show_edit_title_dialog(KtermWindow *self);
static void     setup_title_bar_dnd(GtkWidget *pane_box);
static void     on_notebook_double_click(GtkGestureClick *gesture, int n_press,
                                         double x, double y, gpointer user_data);
static void     on_separator_double_click(GtkGestureClick *gesture, int n_press,
                                          double x, double y, gpointer user_data);
static void     on_title_bar_click(GtkGestureClick *gesture, int n_press,
                                   double x, double y, gpointer user_data);
static void     sidebar_rebuild(KtermWindow *self);
static void     sidebar_update_active(KtermWindow *self, int active_page);
static void     sidebar_set_position(KtermWindow *self, int position);

/* Focus the appropriate widget inside a pane box (terminal or explorer) */
static void
focus_pane(GtkWidget *pane_box)
{
    if (kterm_explorer_is_explorer(pane_box)) {
        GtkWidget *w = kterm_explorer_get_focusable(pane_box);
        if (w) gtk_widget_grab_focus(w);
    } else if (kterm_browser_is_browser(pane_box)) {
        GtkWidget *w = kterm_browser_get_focusable(pane_box);
        if (w) gtk_widget_grab_focus(w);
    } else {
        VteTerminal *vte = kterm_terminal_get_vte(pane_box);
        if (vte) gtk_widget_grab_focus(GTK_WIDGET(vte));
    }
}

/* ── Session persistence ──────────────────────────────────────────── */

static char *
get_session_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "kterm", "session.json", NULL);
}

/* Recursively serialize a pane tree to JSON */
static JsonNode *
serialize_pane_tree(GtkWidget *widget)
{
    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);

    if (GTK_IS_PANED(widget)) {
        json_builder_set_member_name(b, "type");
        json_builder_add_string_value(b, "split");

        GtkOrientation orient = gtk_orientable_get_orientation(
            GTK_ORIENTABLE(widget));
        json_builder_set_member_name(b, "orientation");
        json_builder_add_string_value(b,
            orient == GTK_ORIENTATION_HORIZONTAL ? "horizontal" : "vertical");

        json_builder_set_member_name(b, "position");
        json_builder_add_int_value(b, gtk_paned_get_position(GTK_PANED(widget)));

        GtkWidget *start = gtk_paned_get_start_child(GTK_PANED(widget));
        GtkWidget *end = gtk_paned_get_end_child(GTK_PANED(widget));

        json_builder_set_member_name(b, "start");
        if (start) {
            JsonNode *sn = serialize_pane_tree(start);
            json_builder_add_value(b, sn);
        } else {
            json_builder_add_null_value(b);
        }

        json_builder_set_member_name(b, "end");
        if (end) {
            JsonNode *en = serialize_pane_tree(end);
            json_builder_add_value(b, en);
        } else {
            json_builder_add_null_value(b);
        }
    } else if (kterm_browser_is_browser(widget)) {
        json_builder_set_member_name(b, "type");
        json_builder_add_string_value(b, "browser");

        const char *url = kterm_browser_get_url(widget);
        json_builder_set_member_name(b, "url");
        json_builder_add_string_value(b, url ? url : "https://www.google.com");
    } else if (kterm_explorer_is_explorer(widget)) {
        json_builder_set_member_name(b, "type");
        json_builder_add_string_value(b, "explorer");

        const char *path = kterm_explorer_get_path(widget);
        json_builder_set_member_name(b, "path");
        json_builder_add_string_value(b, path ? path : g_get_home_dir());
    } else {
        /* Terminal */
        json_builder_set_member_name(b, "type");
        json_builder_add_string_value(b, "terminal");

        VteTerminal *vte = kterm_terminal_get_vte(widget);
        char *cwd = vte ? kterm_terminal_get_cwd(vte) : NULL;
        json_builder_set_member_name(b, "cwd");
        json_builder_add_string_value(b, cwd ? cwd : g_get_home_dir());
        g_free(cwd);
    }

    json_builder_end_object(b);
    JsonNode *node = json_builder_get_root(b);
    g_object_unref(b);
    return node;
}

/* Find the root pane widget inside a tab's content box */
static GtkWidget *
get_tab_root_pane(GtkWidget *content_box)
{
    for (GtkWidget *child = gtk_widget_get_first_child(content_box);
         child; child = gtk_widget_get_next_sibling(child)) {
        if (GTK_IS_PANED(child) || GTK_IS_BOX(child))
            return child;
    }
    return NULL;
}

static void
save_session(KtermWindow *self)
{
    g_autofree char *path = get_session_path();

    /* If no tabs remain, remove the session file so next launch starts fresh */
    int n_pages = gtk_notebook_get_n_pages(self->notebook);
    if (n_pages <= 0) {
        g_autoptr(GFile) f = g_file_new_for_path(path);
        g_file_delete(f, NULL, NULL);
        return;
    }

    g_autofree char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);

    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);

    /* Window geometry */
    json_builder_set_member_name(b, "maximized");
    json_builder_add_boolean_value(b, gtk_window_is_maximized(GTK_WINDOW(self)));

    int w, h;
    gtk_window_get_default_size(GTK_WINDOW(self), &w, &h);
    json_builder_set_member_name(b, "width");
    json_builder_add_int_value(b, w > 0 ? w : 800);
    json_builder_set_member_name(b, "height");
    json_builder_add_int_value(b, h > 0 ? h : 600);

    /* Active tab */
    json_builder_set_member_name(b, "active_tab");
    json_builder_add_int_value(b,
        gtk_notebook_get_current_page(self->notebook));

    /* Tabs */
    json_builder_set_member_name(b, "tabs");
    json_builder_begin_array(b);

    int n = gtk_notebook_get_n_pages(self->notebook);
    for (int i = 0; i < n; i++) {
        GtkWidget *content = gtk_notebook_get_nth_page(self->notebook, i);
        GtkWidget *root = get_tab_root_pane(content);
        if (root) {
            JsonNode *pane_node = serialize_pane_tree(root);
            json_builder_add_value(b, pane_node);
        }
    }

    json_builder_end_array(b);
    json_builder_end_object(b);

    JsonNode *root = json_builder_get_root(b);
    g_object_unref(b);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_pretty(gen, TRUE);
    json_generator_set_root(gen, root);
    json_generator_to_file(gen, path, NULL);
    g_object_unref(gen);
    json_node_unref(root);
}

static void
on_surface_notify(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    KtermWindow *self = KTERM_WINDOW(user_data);
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(obj));
    if (!surface)
        return;
    int w = gdk_surface_get_width(surface);
    int h = gdk_surface_get_height(surface);
    if (!gtk_window_is_maximized(GTK_WINDOW(self)) && w > 0 && h > 0)
        gtk_window_set_default_size(GTK_WINDOW(self), w, h);
}

static gboolean
on_close_request(GtkWindow *window, gpointer user_data)
{
    (void)user_data;
    KtermWindow *self = KTERM_WINDOW(window);

    /* Persist sidebar width */
    KtermSettings *s = kterm_settings_get_default();
    if (self->main_paned) {
        int pos = gtk_paned_get_position(GTK_PANED(self->main_paned));
        if (pos > 0) s->sidebar_width = pos;
    }
    s->sidebar_visible = self->sidebar ? gtk_widget_get_visible(self->sidebar) : TRUE;
    kterm_settings_save(s);

    /* Stop thumbnail timer */
    if (self->thumb_timer_id) {
        g_source_remove(self->thumb_timer_id);
        self->thumb_timer_id = 0;
    }

    save_session(self);
    return FALSE;
}

/* ── Tab label with close button ──────────────────────────────────── */

static void
on_tab_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    GtkWidget *tab_content = GTK_WIDGET(user_data);
    GtkWidget *nb = gtk_widget_get_ancestor(tab_content, GTK_TYPE_NOTEBOOK);
    if (!nb) return;
    GtkNotebook *notebook = GTK_NOTEBOOK(nb);
    int page = gtk_notebook_page_num(notebook, tab_content);
    if (page >= 0) {
        gtk_notebook_remove_page(notebook, page);
        if (gtk_notebook_get_n_pages(notebook) == 0) {
            GtkWidget *win = gtk_widget_get_ancestor(GTK_WIDGET(notebook),
                                                      KTERM_TYPE_WINDOW);
            if (win) {
                save_session(KTERM_WINDOW(win));
                gtk_window_destroy(GTK_WINDOW(win));
            }
        }
    }
}

static GtkWidget *
create_tab_label(const char *title, GtkWidget *tab_content)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *label = gtk_label_new(title);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(label), 15);
    GtkWidget *button = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), button);
    g_signal_connect(button, "clicked", G_CALLBACK(on_tab_close_clicked), tab_content);
    g_object_set_data(G_OBJECT(tab_content), "tab-label-widget", label);
    return box;
}

/* ── Focus tracking ───────────────────────────────────────────────── */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static const char *
get_terminal_title(VteTerminal *vte)
{
    return vte_terminal_get_window_title(vte);
}

#pragma GCC diagnostic pop

static void
on_focus_enter(GtkEventControllerFocus *ctrl, gpointer user_data)
{
    (void)ctrl;
    VteTerminal *vte = VTE_TERMINAL(user_data);
    GtkWidget *win = gtk_widget_get_ancestor(GTK_WIDGET(vte), KTERM_TYPE_WINDOW);
    if (!win) return;

    KtermWindow *self = KTERM_WINDOW(win);
    self->active_terminal = vte;
    self->active_pane = find_terminal_box(vte);

    const char *title = get_terminal_title(vte);
    if (title && *title)
        gtk_window_set_title(GTK_WINDOW(self), title);

    /* Update active state for all panes in current tab */
    int page_num = gtk_notebook_get_current_page(self->notebook);
    GtkWidget *tab = gtk_notebook_get_nth_page(self->notebook, page_num);
    if (tab) {
        GPtrArray *panes = g_ptr_array_new();
        collect_panes(tab, panes);
        for (guint i = 0; i < panes->len; i++) {
            GtkWidget *tb = g_ptr_array_index(panes, i);
            gboolean is_active = (tb == self->active_pane);

            /* Title bar highlight */
            GtkWidget *lbl = g_object_get_data(G_OBJECT(tb), "kterm-title-label");
            if (lbl) {
                GtkWidget *tbar = gtk_widget_get_parent(lbl);
                if (tbar && tbar != tb) {
                    if (is_active)
                        gtk_widget_add_css_class(tbar, "active");
                    else
                        gtk_widget_remove_css_class(tbar, "active");
                }
            }

            /* Pane border highlight */
            if (is_active)
                gtk_widget_add_css_class(tb, "kterm-pane-active");
            else
                gtk_widget_remove_css_class(tb, "kterm-pane-active");
        }
        g_ptr_array_free(panes, TRUE);
    }
}

/* ── Context menu (right-click) ───────────────────────────────────── */

static void
popdown_and_unparent(GtkWidget *widget)
{
    GtkWidget *popover = gtk_widget_get_ancestor(widget, GTK_TYPE_POPOVER);
    if (popover)
        gtk_popover_popdown(GTK_POPOVER(popover));
    /* Actual unparent happens in the "closed" signal handler */
}

static void
menu_split_v(GtkButton *btn, gpointer ud)
{ popdown_and_unparent(GTK_WIDGET(btn)); split_terminal(KTERM_WINDOW(ud), GTK_ORIENTATION_HORIZONTAL); }
static void
menu_split_h(GtkButton *btn, gpointer ud)
{ popdown_and_unparent(GTK_WIDGET(btn)); split_terminal(KTERM_WINDOW(ud), GTK_ORIENTATION_VERTICAL); }
static void
menu_close(GtkButton *btn, gpointer ud)
{ popdown_and_unparent(GTK_WIDGET(btn)); close_current_pane(KTERM_WINDOW(ud)); }
static void
menu_edit_title(GtkButton *btn, gpointer ud)
{ popdown_and_unparent(GTK_WIDGET(btn)); show_edit_title_dialog(KTERM_WINDOW(ud)); }

static void menu_file_manager(GtkButton *btn, gpointer ud);

static void
menu_copy(GtkButton *btn, gpointer ud)
{
    popdown_and_unparent(GTK_WIDGET(btn));
    KtermWindow *self = KTERM_WINDOW(ud);
    if (self->active_terminal)
        vte_terminal_copy_clipboard_format(self->active_terminal, VTE_FORMAT_TEXT);
}

static void
menu_paste(GtkButton *btn, gpointer ud)
{
    popdown_and_unparent(GTK_WIDGET(btn));
    KtermWindow *self = KTERM_WINDOW(ud);
    if (self->active_terminal)
        vte_terminal_paste_clipboard(self->active_terminal);
}

static void
on_paste_enter_clipboard_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
    VteTerminal *vte = VTE_TERMINAL(user_data);
    char *text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, NULL);
    if (text) {
        vte_terminal_feed_child(vte, text, -1);
        vte_terminal_feed_child(vte, "\n", 1);
        g_free(text);
    }
}

static void
menu_paste_enter(GtkButton *btn, gpointer ud)
{
    popdown_and_unparent(GTK_WIDGET(btn));
    KtermWindow *self = KTERM_WINDOW(ud);
    if (!self->active_terminal) return;
    GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(self->active_terminal));
    gdk_clipboard_read_text_async(clipboard, NULL,
                                  on_paste_enter_clipboard_ready,
                                  self->active_terminal);
}

static void
menu_preferences(GtkButton *btn, gpointer ud)
{ popdown_and_unparent(GTK_WIDGET(btn)); kterm_preferences_show(GTK_WINDOW(ud)); }

static void
menu_explorer_v(GtkButton *btn, gpointer ud)
{ popdown_and_unparent(GTK_WIDGET(btn)); split_with_explorer(KTERM_WINDOW(ud), GTK_ORIENTATION_HORIZONTAL); }
static void
menu_explorer_h(GtkButton *btn, gpointer ud)
{ popdown_and_unparent(GTK_WIDGET(btn)); split_with_explorer(KTERM_WINDOW(ud), GTK_ORIENTATION_VERTICAL); }
static void
menu_browser_v(GtkButton *btn, gpointer ud)
{ popdown_and_unparent(GTK_WIDGET(btn)); split_with_browser(KTERM_WINDOW(ud), GTK_ORIENTATION_HORIZONTAL); }
static void
menu_browser_h(GtkButton *btn, gpointer ud)
{ popdown_and_unparent(GTK_WIDGET(btn)); split_with_browser(KTERM_WINDOW(ud), GTK_ORIENTATION_VERTICAL); }

static GtkWidget *
menu_item(const char *label, GCallback callback, gpointer ud)
{
    GtkWidget *btn = gtk_button_new_with_label(label);
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    gtk_widget_set_halign(btn, GTK_ALIGN_FILL);
    gtk_label_set_xalign(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(btn))), 0.0);
    g_signal_connect(btn, "clicked", callback, ud);
    return btn;
}

static GtkWidget *
build_context_menu(KtermWindow *self)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    gtk_box_append(GTK_BOX(box), menu_item(_("Split |"),            G_CALLBACK(menu_split_v), self));
    gtk_box_append(GTK_BOX(box), menu_item(_("Split -"),            G_CALLBACK(menu_split_h), self));
    gtk_box_append(GTK_BOX(box), menu_item(_("Split | [Files]"),    G_CALLBACK(menu_explorer_v), self));
    gtk_box_append(GTK_BOX(box), menu_item(_("Split - [Files]"),    G_CALLBACK(menu_explorer_h), self));
    gtk_box_append(GTK_BOX(box), menu_item(_("Split | [Browser]"),  G_CALLBACK(menu_browser_v), self));
    gtk_box_append(GTK_BOX(box), menu_item(_("Split - [Browser]"),  G_CALLBACK(menu_browser_h), self));
    gtk_box_append(GTK_BOX(box), menu_item(_("Close Terminal"),       G_CALLBACK(menu_close), self));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), menu_item(_("Edit Title\u2026"),       G_CALLBACK(menu_edit_title), self));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), menu_item(_("Copy"),  G_CALLBACK(menu_copy), self));
    gtk_box_append(GTK_BOX(box), menu_item(_("Paste"), G_CALLBACK(menu_paste), self));
    gtk_box_append(GTK_BOX(box), menu_item(_("Paste + ENTER"), G_CALLBACK(menu_paste_enter), self));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), menu_item(_("Open File Manager"), G_CALLBACK(menu_file_manager), self));
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), menu_item(_("Preferences\u2026"), G_CALLBACK(menu_preferences), self));

    return box;
}

static void
show_popup_at(GtkWidget *parent, double x, double y, KtermWindow *self)
{
    GtkWidget *menu = build_context_menu(self);

    GtkWidget *popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(popover), menu);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_widget_set_parent(popover, parent);

    g_signal_connect(popover, "closed",
                     G_CALLBACK(gtk_widget_unparent), NULL);

    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);

    gtk_popover_popup(GTK_POPOVER(popover));
}

static void
on_right_click(GtkGestureClick *gesture, int n_press, double x, double y,
               gpointer user_data)
{
    (void)n_press;
    (void)user_data;

    GtkWidget *vte = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    gtk_widget_grab_focus(vte);

    GtkWidget *win = gtk_widget_get_ancestor(vte, KTERM_TYPE_WINDOW);
    if (!win) return;

    show_popup_at(vte, x, y, KTERM_WINDOW(win));
}

static void
on_title_bar_click(GtkGestureClick *gesture, int n_press, double x, double y,
                   gpointer user_data)
{
    (void)n_press;
    (void)user_data;

    GtkWidget *title_bar = gtk_event_controller_get_widget(
        GTK_EVENT_CONTROLLER(gesture));

    /* Find the pane box (terminal_box or explorer_box) — the parent */
    GtkWidget *pane_box = gtk_widget_get_parent(title_bar);
    if (!pane_box) return;

    GtkWidget *win = gtk_widget_get_ancestor(pane_box, KTERM_TYPE_WINDOW);
    if (!win) return;
    KtermWindow *self = KTERM_WINDOW(win);

    /* Set this pane as active */
    focus_pane(pane_box);

    show_popup_at(title_bar, x, y, self);
}

/* ── Ctrl+Scroll font zoom ────────────────────────────────────────── */

static gboolean
on_ctrl_scroll(GtkEventControllerScroll *ctrl, double dx, double dy,
               gpointer user_data)
{
    (void)dx;
    (void)user_data;

    GdkModifierType state =
        gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(ctrl));
    if (!(state & GDK_CONTROL_MASK))
        return FALSE;

    KtermSettings *s = kterm_settings_get_default();
    if (dy < 0)
        s->font_size += 1.0;   /* scroll up = larger */
    else if (dy > 0)
        s->font_size -= 1.0;   /* scroll down = smaller */

    if (s->font_size < 6.0)  s->font_size = 6.0;
    if (s->font_size > 72.0) s->font_size = 72.0;

    GtkWidget *win = gtk_widget_get_ancestor(
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(ctrl)),
        KTERM_TYPE_WINDOW);
    if (win) {
        kterm_window_apply_settings(KTERM_WINDOW(win));
        kterm_settings_save(s);
    }
    return TRUE;
}

/* ── Setup per-terminal controllers ───────────────────────────────── */

static void
setup_focus_tracking(KtermWindow *self, GtkWidget *terminal_box)
{
    (void)self;
    VteTerminal *vte = kterm_terminal_get_vte(terminal_box);

    /* Focus tracking */
    GtkEventController *focus_ctrl = gtk_event_controller_focus_new();
    g_signal_connect(focus_ctrl, "enter", G_CALLBACK(on_focus_enter), vte);
    gtk_widget_add_controller(GTK_WIDGET(vte), focus_ctrl);

    /* Window title tracking + internal title bar update */
    g_signal_connect(vte, "notify::window-title", G_CALLBACK(update_tab_title), NULL);

    /* Right-click context menu */
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 3);
    g_signal_connect(click, "pressed", G_CALLBACK(on_right_click), NULL);
    gtk_widget_add_controller(GTK_WIDGET(vte), GTK_EVENT_CONTROLLER(click));

    /* Ctrl+scroll font zoom */
    GtkEventController *scroll_ctrl =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    gtk_event_controller_set_propagation_phase(scroll_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(scroll_ctrl, "scroll", G_CALLBACK(on_ctrl_scroll), NULL);
    gtk_widget_add_controller(GTK_WIDGET(vte), scroll_ctrl);

    /* Title bar click (any button) shows context menu */
    GtkWidget *title_bar = gtk_widget_get_first_child(terminal_box);
    if (title_bar) {
        GtkGesture *title_click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(title_click), 3);
        g_signal_connect(title_click, "pressed",
                         G_CALLBACK(on_title_bar_click), NULL);
        gtk_widget_add_controller(title_bar, GTK_EVENT_CONTROLLER(title_click));
    }

    /* Drag-and-drop tile swapping */
    setup_title_bar_dnd(terminal_box);
}

static void
update_tab_title(VteTerminal *vte, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    (void)user_data;
    const char *title = get_terminal_title(vte);
    if (!title || !*title)
        return;

    GtkWidget *win = gtk_widget_get_ancestor(GTK_WIDGET(vte), KTERM_TYPE_WINDOW);
    if (win) {
        KtermWindow *self = KTERM_WINDOW(win);
        if (self->active_terminal == vte)
            gtk_window_set_title(GTK_WINDOW(self), title);
    }

    /* Update internal title bar label */
    GtkWidget *terminal_box = find_terminal_box(vte);
    if (terminal_box) {
        GtkWidget *title_label = kterm_terminal_get_title_label(terminal_box);
        if (title_label)
            gtk_label_set_text(GTK_LABEL(title_label), title);
    }

    /* Update tab title */
    if (!terminal_box)
        return;
    GtkWidget *notebook_widget = gtk_widget_get_ancestor(terminal_box, GTK_TYPE_NOTEBOOK);
    if (!notebook_widget)
        return;
    GtkNotebook *notebook = GTK_NOTEBOOK(notebook_widget);

    GtkWidget *page = terminal_box;
    while (page && gtk_widget_get_parent(page) != GTK_WIDGET(notebook))
        page = gtk_widget_get_parent(page);
    if (!page)
        return;

    /* Don't overwrite custom title */
    if (g_object_get_data(G_OBJECT(page), "custom-title"))
        return;

    GtkWidget *label = g_object_get_data(G_OBJECT(page), "tab-label-widget");
    if (label)
        gtk_label_set_text(GTK_LABEL(label), title);
}

/* ── Terminal box lookup ───────────────────────────────────────────── */

static GtkWidget *
find_terminal_box(VteTerminal *vte)
{
    GtkWidget *w = gtk_widget_get_parent(GTK_WIDGET(vte));
    while (w) {
        if (GTK_IS_BOX(w) && g_object_get_data(G_OBJECT(w), "kterm-title-label"))
            return w;
        w = gtk_widget_get_parent(w);
    }
    return NULL;
}

/* Swap two pane boxes in the widget tree */
static void
swap_pane_boxes(GtkWidget *a, GtkWidget *b)
{
    if (a == b) return;

    GtkWidget *parent_a = gtk_widget_get_parent(a);
    GtkWidget *parent_b = gtk_widget_get_parent(b);
    if (!parent_a || !parent_b) return;

    /* Determine slot for a */
    gboolean a_is_start = FALSE;
    gboolean a_in_paned = GTK_IS_PANED(parent_a);
    if (a_in_paned)
        a_is_start = (gtk_paned_get_start_child(GTK_PANED(parent_a)) == a);

    /* Determine slot for b */
    gboolean b_is_start = FALSE;
    gboolean b_in_paned = GTK_IS_PANED(parent_b);
    if (b_in_paned)
        b_is_start = (gtk_paned_get_start_child(GTK_PANED(parent_b)) == b);

    g_object_ref(a);
    g_object_ref(b);

    /* Detach both */
    if (a_in_paned) {
        if (a_is_start)
            gtk_paned_set_start_child(GTK_PANED(parent_a), NULL);
        else
            gtk_paned_set_end_child(GTK_PANED(parent_a), NULL);
    } else if (GTK_IS_BOX(parent_a)) {
        gtk_box_remove(GTK_BOX(parent_a), a);
    }

    if (b_in_paned) {
        if (b_is_start)
            gtk_paned_set_start_child(GTK_PANED(parent_b), NULL);
        else
            gtk_paned_set_end_child(GTK_PANED(parent_b), NULL);
    } else if (GTK_IS_BOX(parent_b)) {
        gtk_box_remove(GTK_BOX(parent_b), b);
    }

    /* Re-attach swapped: b goes into a's old slot, a goes into b's old slot */
    if (a_in_paned) {
        if (a_is_start)
            gtk_paned_set_start_child(GTK_PANED(parent_a), b);
        else
            gtk_paned_set_end_child(GTK_PANED(parent_a), b);
    } else if (GTK_IS_BOX(parent_a)) {
        gtk_box_append(GTK_BOX(parent_a), b);
    }

    if (b_in_paned) {
        if (b_is_start)
            gtk_paned_set_start_child(GTK_PANED(parent_b), a);
        else
            gtk_paned_set_end_child(GTK_PANED(parent_b), a);
    } else if (GTK_IS_BOX(parent_b)) {
        gtk_box_append(GTK_BOX(parent_b), a);
    }

    g_object_unref(a);
    g_object_unref(b);
}

/* ── Terminal exit handling ────────────────────────────────────────── */

static void
on_terminal_exited(GtkWidget *pane_box, gpointer user_data)
{
    (void)user_data;
    GtkWidget *win = gtk_widget_get_ancestor(pane_box, KTERM_TYPE_WINDOW);
    if (!win)
        return;
    KtermWindow *self = KTERM_WINDOW(win);

    VteTerminal *vte = kterm_terminal_get_vte(pane_box);
    if (vte && self->active_terminal == vte)
        self->active_terminal = NULL;
    if (self->active_pane == pane_box)
        self->active_pane = NULL;

    GtkWidget *parent = gtk_widget_get_parent(pane_box);
    if (!parent)
        return;

    if (GTK_IS_PANED(parent)) {
        GtkPaned *paned = GTK_PANED(parent);
        GtkWidget *start = gtk_paned_get_start_child(paned);
        GtkWidget *end = gtk_paned_get_end_child(paned);
        GtkWidget *sibling = (pane_box == start) ? end : start;

        g_object_ref(sibling);
        gtk_paned_set_start_child(paned, NULL);
        gtk_paned_set_end_child(paned, NULL);

        GtkWidget *grandparent = gtk_widget_get_parent(GTK_WIDGET(paned));
        if (GTK_IS_PANED(grandparent)) {
            GtkPaned *gp = GTK_PANED(grandparent);
            if (gtk_paned_get_start_child(gp) == GTK_WIDGET(paned))
                gtk_paned_set_start_child(gp, sibling);
            else
                gtk_paned_set_end_child(gp, sibling);
        } else if (GTK_IS_BOX(grandparent)) {
            gtk_box_remove(GTK_BOX(grandparent), GTK_WIDGET(paned));
            gtk_box_append(GTK_BOX(grandparent), sibling);
        }
        g_object_unref(sibling);

        GPtrArray *panes = g_ptr_array_new();
        collect_panes(sibling, panes);
        if (panes->len > 0)
            focus_pane(g_ptr_array_index(panes, 0));
        g_ptr_array_free(panes, TRUE);
    } else if (GTK_IS_BOX(parent)) {
        /* parent is the tab content box */
        GtkWidget *nb = gtk_widget_get_ancestor(parent, GTK_TYPE_NOTEBOOK);
        if (nb) {
            GtkNotebook *notebook = GTK_NOTEBOOK(nb);
            int page = gtk_notebook_page_num(notebook, parent);
            if (page >= 0)
                gtk_notebook_remove_page(notebook, page);
            if (gtk_notebook_get_n_pages(notebook) == 0) {
                save_session(KTERM_WINDOW(win));
                gtk_window_destroy(GTK_WINDOW(win));
            }
        }
    }
}

/* ── Tab management ───────────────────────────────────────────────── */

static void
add_tab(KtermWindow *self)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_vexpand(box, TRUE);

    GtkWidget *terminal_box = kterm_terminal_new();
    setup_focus_tracking(self, terminal_box);
    g_signal_connect(terminal_box, "terminal-exited", G_CALLBACK(on_terminal_exited), self);
    gtk_box_append(GTK_BOX(box), terminal_box);

    GtkWidget *tab_label = create_tab_label(_("Terminal"), box);
    int page = gtk_notebook_append_page(self->notebook, box, tab_label);
    gtk_notebook_set_tab_reorderable(self->notebook, box, TRUE);
    gtk_notebook_set_current_page(self->notebook, page);

    VteTerminal *vte = kterm_terminal_get_vte(terminal_box);
    gtk_widget_grab_focus(GTK_WIDGET(vte));
}

/* ── Collect all pane boxes (terminals + explorers) recursively ────── */

static void
collect_panes(GtkWidget *widget, GPtrArray *panes)
{
    if (GTK_IS_BOX(widget) && g_object_get_data(G_OBJECT(widget), "kterm-title-label")) {
        /* This is a pane box (terminal or explorer) — add it */
        g_ptr_array_add(panes, widget);
    } else if (GTK_IS_PANED(widget)) {
        GtkWidget *start = gtk_paned_get_start_child(GTK_PANED(widget));
        GtkWidget *end = gtk_paned_get_end_child(GTK_PANED(widget));
        if (start) collect_panes(start, panes);
        if (end)   collect_panes(end, panes);
    } else if (GTK_IS_BOX(widget)) {
        for (GtkWidget *child = gtk_widget_get_first_child(widget);
             child;
             child = gtk_widget_get_next_sibling(child)) {
            collect_panes(child, panes);
        }
    }
}

/* ── Split ────────────────────────────────────────────────────────── */

static void
split_terminal(KtermWindow *self, GtkOrientation orientation)
{
    if (!self->active_terminal)
        return;

    GtkWidget *terminal_box = find_terminal_box(self->active_terminal);
    if (!terminal_box)
        return;

    GtkWidget *parent = gtk_widget_get_parent(terminal_box);
    if (!parent)
        return;

    /* Measure before reparenting (widget still has valid allocation) */
    int size = (orientation == GTK_ORIENTATION_HORIZONTAL)
        ? gtk_widget_get_width(terminal_box)
        : gtk_widget_get_height(terminal_box);

    GtkWidget *paned = gtk_paned_new(orientation);
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);

    GtkWidget *new_terminal_box = kterm_terminal_new();
    setup_focus_tracking(self, new_terminal_box);
    g_signal_connect(new_terminal_box, "terminal-exited", G_CALLBACK(on_terminal_exited), self);

    g_object_ref(terminal_box);

    if (GTK_IS_PANED(parent)) {
        GtkPaned *pp = GTK_PANED(parent);
        if (gtk_paned_get_start_child(pp) == terminal_box)
            gtk_paned_set_start_child(pp, paned);
        else
            gtk_paned_set_end_child(pp, paned);
    } else if (GTK_IS_BOX(parent)) {
        gtk_box_remove(GTK_BOX(parent), terminal_box);
        gtk_box_append(GTK_BOX(parent), paned);
    }

    gtk_paned_set_start_child(GTK_PANED(paned), terminal_box);
    gtk_paned_set_end_child(GTK_PANED(paned), new_terminal_box);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);

    if (size > 0)
        gtk_paned_set_position(GTK_PANED(paned), size / 2);


    g_object_unref(terminal_box);

    VteTerminal *vte = kterm_terminal_get_vte(new_terminal_box);
    gtk_widget_grab_focus(GTK_WIDGET(vte));
}

/* ── Split with file explorer ─────────────────────────────────────── */

static void
on_explorer_focus_enter(GtkEventControllerFocus *ctrl, gpointer user_data)
{
    (void)ctrl;
    GtkWidget *explorer_box = GTK_WIDGET(user_data);
    GtkWidget *win = gtk_widget_get_ancestor(explorer_box, KTERM_TYPE_WINDOW);
    if (!win) return;

    KtermWindow *self = KTERM_WINDOW(win);
    self->active_pane = explorer_box;
    /* Don't clear active_terminal — keep it for terminal-specific actions */

    /* Update active state for all panes in current tab */
    int page_num = gtk_notebook_get_current_page(self->notebook);
    GtkWidget *tab = gtk_notebook_get_nth_page(self->notebook, page_num);
    if (tab) {
        GPtrArray *panes = g_ptr_array_new();
        collect_panes(tab, panes);
        for (guint i = 0; i < panes->len; i++) {
            GtkWidget *tb = g_ptr_array_index(panes, i);
            gboolean is_active = (tb == explorer_box);

            /* Title bar highlight */
            GtkWidget *lbl = g_object_get_data(G_OBJECT(tb), "kterm-title-label");
            if (lbl) {
                GtkWidget *tbar = gtk_widget_get_parent(lbl);
                if (tbar && tbar != tb) {
                    if (is_active)
                        gtk_widget_add_css_class(tbar, "active");
                    else
                        gtk_widget_remove_css_class(tbar, "active");
                }
            }

            /* Pane border highlight */
            if (is_active)
                gtk_widget_add_css_class(tb, "kterm-pane-active");
            else
                gtk_widget_remove_css_class(tb, "kterm-pane-active");
        }
        g_ptr_array_free(panes, TRUE);
    }
}

static void
setup_explorer_tracking(KtermWindow *self, GtkWidget *explorer_box)
{
    (void)self;
    GtkWidget *list_view = kterm_explorer_get_list_focusable(explorer_box);
    if (list_view) {
        GtkEventController *fc = gtk_event_controller_focus_new();
        g_signal_connect(fc, "enter", G_CALLBACK(on_explorer_focus_enter), explorer_box);
        gtk_widget_add_controller(list_view, fc);
    }

    GtkWidget *grid_view = kterm_explorer_get_grid_focusable(explorer_box);
    if (grid_view) {
        GtkEventController *fc = gtk_event_controller_focus_new();
        g_signal_connect(fc, "enter", G_CALLBACK(on_explorer_focus_enter), explorer_box);
        gtk_widget_add_controller(grid_view, fc);
    }

    GtkWidget *left = kterm_explorer_get_left_focusable(explorer_box);
    if (left) {
        GtkEventController *fc = gtk_event_controller_focus_new();
        g_signal_connect(fc, "enter", G_CALLBACK(on_explorer_focus_enter), explorer_box);
        gtk_widget_add_controller(left, fc);
    }

    /* Title bar click (any button) shows context menu */
    GtkWidget *explorer_title_bar = gtk_widget_get_first_child(explorer_box);
    if (explorer_title_bar) {
        GtkGesture *title_click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(title_click), 3);
        g_signal_connect(title_click, "pressed",
                         G_CALLBACK(on_title_bar_click), NULL);
        gtk_widget_add_controller(explorer_title_bar,
                                  GTK_EVENT_CONTROLLER(title_click));
    }

    /* Drag-and-drop tile swapping */
    setup_title_bar_dnd(explorer_box);
}

static void
split_with_explorer(KtermWindow *self, GtkOrientation orientation)
{
    if (!self->active_pane && !self->active_terminal)
        return;

    GtkWidget *current_box = self->active_pane;
    if (!current_box && self->active_terminal)
        current_box = find_terminal_box(self->active_terminal);
    if (!current_box)
        return;

    GtkWidget *parent = gtk_widget_get_parent(current_box);
    if (!parent)
        return;

    /* Measure before reparenting (widget still has valid allocation) */
    int size = (orientation == GTK_ORIENTATION_HORIZONTAL)
        ? gtk_widget_get_width(current_box)
        : gtk_widget_get_height(current_box);

    /* Determine initial path: CWD of active terminal, or HOME */
    char *cwd = NULL;
    if (self->active_terminal)
        cwd = kterm_terminal_get_cwd(self->active_terminal);
    if (!cwd)
        cwd = g_strdup(g_get_home_dir());

    GtkWidget *paned = gtk_paned_new(orientation);
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);

    GtkWidget *explorer_box = kterm_explorer_new(cwd);
    g_free(cwd);
    setup_explorer_tracking(self, explorer_box);
    g_signal_connect(explorer_box, "terminal-exited", G_CALLBACK(on_terminal_exited), self);

    g_object_ref(current_box);

    if (GTK_IS_PANED(parent)) {
        GtkPaned *pp = GTK_PANED(parent);
        if (gtk_paned_get_start_child(pp) == current_box)
            gtk_paned_set_start_child(pp, paned);
        else
            gtk_paned_set_end_child(pp, paned);
    } else if (GTK_IS_BOX(parent)) {
        gtk_box_remove(GTK_BOX(parent), current_box);
        gtk_box_append(GTK_BOX(parent), paned);
    }

    gtk_paned_set_start_child(GTK_PANED(paned), current_box);
    gtk_paned_set_end_child(GTK_PANED(paned), explorer_box);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);

    if (size > 0)
        gtk_paned_set_position(GTK_PANED(paned), size / 2);


    g_object_unref(current_box);

    focus_pane(explorer_box);
}

/* ── Split with web browser ───────────────────────────────────────── */

static void
on_browser_focus_enter(GtkEventControllerFocus *ctrl, gpointer user_data)
{
    (void)ctrl;
    GtkWidget *browser_box = GTK_WIDGET(user_data);
    GtkWidget *win = gtk_widget_get_ancestor(browser_box, KTERM_TYPE_WINDOW);
    if (!win) return;

    KtermWindow *self = KTERM_WINDOW(win);
    self->active_pane = browser_box;

    /* Update active state for all panes in current tab */
    int page_num = gtk_notebook_get_current_page(self->notebook);
    GtkWidget *tab = gtk_notebook_get_nth_page(self->notebook, page_num);
    if (tab) {
        GPtrArray *panes = g_ptr_array_new();
        collect_panes(tab, panes);
        for (guint i = 0; i < panes->len; i++) {
            GtkWidget *tb = g_ptr_array_index(panes, i);
            gboolean is_active = (tb == browser_box);

            GtkWidget *lbl = g_object_get_data(G_OBJECT(tb), "kterm-title-label");
            if (lbl) {
                GtkWidget *tbar = gtk_widget_get_parent(lbl);
                if (tbar && tbar != tb) {
                    if (is_active)
                        gtk_widget_add_css_class(tbar, "active");
                    else
                        gtk_widget_remove_css_class(tbar, "active");
                }
            }

            if (is_active)
                gtk_widget_add_css_class(tb, "kterm-pane-active");
            else
                gtk_widget_remove_css_class(tb, "kterm-pane-active");
        }
        g_ptr_array_free(panes, TRUE);
    }
}

static void
setup_browser_tracking(KtermWindow *self, GtkWidget *browser_box)
{
    (void)self;
    GtkWidget *web_view = kterm_browser_get_focusable(browser_box);
    if (web_view) {
        GtkEventController *fc = gtk_event_controller_focus_new();
        g_signal_connect(fc, "enter", G_CALLBACK(on_browser_focus_enter), browser_box);
        gtk_widget_add_controller(web_view, fc);
    }

    /* Title bar right-click gesture */
    GtkWidget *browser_title_bar = gtk_widget_get_first_child(browser_box);
    if (browser_title_bar) {
        GtkGesture *title_click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(title_click), 3);
        g_signal_connect(title_click, "pressed",
                         G_CALLBACK(on_title_bar_click), NULL);
        gtk_widget_add_controller(browser_title_bar,
                                  GTK_EVENT_CONTROLLER(title_click));
    }

    /* Drag-and-drop tile swapping */
    setup_title_bar_dnd(browser_box);
}

static void
split_with_browser(KtermWindow *self, GtkOrientation orientation)
{
    if (!self->active_pane && !self->active_terminal)
        return;

    GtkWidget *current_box = self->active_pane;
    if (!current_box && self->active_terminal)
        current_box = find_terminal_box(self->active_terminal);
    if (!current_box)
        return;

    GtkWidget *parent = gtk_widget_get_parent(current_box);
    if (!parent)
        return;

    int size = (orientation == GTK_ORIENTATION_HORIZONTAL)
        ? gtk_widget_get_width(current_box)
        : gtk_widget_get_height(current_box);

    GtkWidget *paned = gtk_paned_new(orientation);
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);

    GtkWidget *browser_box = kterm_browser_new(NULL);
    setup_browser_tracking(self, browser_box);
    g_signal_connect(browser_box, "terminal-exited", G_CALLBACK(on_terminal_exited), self);

    g_object_ref(current_box);

    if (GTK_IS_PANED(parent)) {
        GtkPaned *pp = GTK_PANED(parent);
        if (gtk_paned_get_start_child(pp) == current_box)
            gtk_paned_set_start_child(pp, paned);
        else
            gtk_paned_set_end_child(pp, paned);
    } else if (GTK_IS_BOX(parent)) {
        gtk_box_remove(GTK_BOX(parent), current_box);
        gtk_box_append(GTK_BOX(parent), paned);
    }

    gtk_paned_set_start_child(GTK_PANED(paned), current_box);
    gtk_paned_set_end_child(GTK_PANED(paned), browser_box);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);

    if (size > 0)
        gtk_paned_set_position(GTK_PANED(paned), size / 2);


    g_object_unref(current_box);

    focus_pane(browser_box);
}

/* ── Close current pane (terminal or explorer) ────────────────────── */

static void
close_current_pane(KtermWindow *self)
{
    GtkWidget *pane_box = self->active_pane;
    if (!pane_box) {
        /* Fallback: try finding via active_terminal */
        if (!self->active_terminal) return;
        pane_box = find_terminal_box(self->active_terminal);
        if (!pane_box) return;
    }

    VteTerminal *vte = kterm_terminal_get_vte(pane_box);
    if (vte && self->active_terminal == vte)
        self->active_terminal = NULL;
    self->active_pane = NULL;

    GtkWidget *parent = gtk_widget_get_parent(pane_box);
    if (!parent)
        return;

    if (GTK_IS_PANED(parent)) {
        GtkPaned *paned = GTK_PANED(parent);
        GtkWidget *start = gtk_paned_get_start_child(paned);
        GtkWidget *end = gtk_paned_get_end_child(paned);
        GtkWidget *sibling = (pane_box == start) ? end : start;

        g_object_ref(sibling);
        gtk_paned_set_start_child(paned, NULL);
        gtk_paned_set_end_child(paned, NULL);

        GtkWidget *grandparent = gtk_widget_get_parent(GTK_WIDGET(paned));
        if (GTK_IS_PANED(grandparent)) {
            GtkPaned *gp = GTK_PANED(grandparent);
            if (gtk_paned_get_start_child(gp) == GTK_WIDGET(paned))
                gtk_paned_set_start_child(gp, sibling);
            else
                gtk_paned_set_end_child(gp, sibling);
        } else if (GTK_IS_BOX(grandparent)) {
            gtk_box_remove(GTK_BOX(grandparent), GTK_WIDGET(paned));
            gtk_box_append(GTK_BOX(grandparent), sibling);
        }
        g_object_unref(sibling);

        GPtrArray *panes = g_ptr_array_new();
        collect_panes(sibling, panes);
        if (panes->len > 0)
            focus_pane(g_ptr_array_index(panes, 0));
        g_ptr_array_free(panes, TRUE);

    } else if (GTK_IS_BOX(parent)) {
        GtkWidget *nb = gtk_widget_get_ancestor(parent, GTK_TYPE_NOTEBOOK);
        if (nb) {
            GtkNotebook *notebook = GTK_NOTEBOOK(nb);
            int page = gtk_notebook_page_num(notebook, parent);
            if (page >= 0)
                gtk_notebook_remove_page(notebook, page);
            if (gtk_notebook_get_n_pages(notebook) == 0) {
                save_session(self);
                gtk_window_destroy(GTK_WINDOW(self));
            }
        }
    }
}

/* ── Spatial navigation ───────────────────────────────────────────── */

static void
navigate(KtermWindow *self, int dx, int dy)
{
    if (!self->active_pane && !self->active_terminal)
        return;

    int page = gtk_notebook_get_current_page(self->notebook);
    GtkWidget *tab = gtk_notebook_get_nth_page(self->notebook, page);
    if (!tab)
        return;

    GPtrArray *panes = g_ptr_array_new();
    collect_panes(tab, panes);

    if (panes->len <= 1) {
        g_ptr_array_free(panes, TRUE);
        return;
    }

    GtkWidget *active_box = self->active_pane;
    if (!active_box && self->active_terminal)
        active_box = find_terminal_box(self->active_terminal);
    if (!active_box) {
        g_ptr_array_free(panes, TRUE);
        return;
    }

    graphene_rect_t active_bounds;
    if (!gtk_widget_compute_bounds(active_box, GTK_WIDGET(self), &active_bounds)) {
        g_ptr_array_free(panes, TRUE);
        return;
    }

    float ax = active_bounds.origin.x + active_bounds.size.width / 2;
    float ay = active_bounds.origin.y + active_bounds.size.height / 2;

    GtkWidget *best = NULL;
    float best_dist = G_MAXFLOAT;

    for (guint i = 0; i < panes->len; i++) {
        GtkWidget *tb = g_ptr_array_index(panes, i);
        if (tb == active_box)
            continue;

        graphene_rect_t bounds;
        if (!gtk_widget_compute_bounds(tb, GTK_WIDGET(self), &bounds))
            continue;

        float cx = bounds.origin.x + bounds.size.width / 2;
        float cy = bounds.origin.y + bounds.size.height / 2;
        float rel_x = cx - ax;
        float rel_y = cy - ay;

        gboolean in_direction = FALSE;
        if (dx > 0 && rel_x > 1)        in_direction = TRUE;
        else if (dx < 0 && rel_x < -1)  in_direction = TRUE;
        else if (dy > 0 && rel_y > 1)   in_direction = TRUE;
        else if (dy < 0 && rel_y < -1)  in_direction = TRUE;

        if (!in_direction)
            continue;

        float dist = rel_x * rel_x + rel_y * rel_y;
        if (dist < best_dist) {
            best_dist = dist;
            best = tb;
        }
    }

    g_ptr_array_free(panes, TRUE);

    if (best)
        focus_pane(best);
}

/* ── Pane border CSS ──────────────────────────────────────────────── */

static GtkCssProvider *pane_outline_provider = NULL;

static void
update_pane_outline_css(void)
{
    KtermSettings *s = kterm_settings_get_default();
    if (!pane_outline_provider) {
        pane_outline_provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(pane_outline_provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
    }
    if (s->show_pane_outline)
        gtk_css_provider_load_from_string(pane_outline_provider,
            ".kterm-pane-active { outline: 0.5px solid #ffffff; outline-offset: -0.5px; }");
    else
        gtk_css_provider_load_from_string(pane_outline_provider,
            ".kterm-pane-active { outline: none; }");
}

static GtkCssProvider *pane_border_provider = NULL;

static void
update_pane_border_css(void)
{
    KtermSettings *s = kterm_settings_get_default();
    if (!pane_border_provider) {
        pane_border_provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(pane_border_provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    char css[128];
    snprintf(css, sizeof(css),
             "paned > separator { min-width: %dpx; min-height: %dpx; }",
             s->pane_border_size, s->pane_border_size);
    gtk_css_provider_load_from_string(pane_border_provider, css);
}

/* ── Drag-and-drop tile swapping via title bars ───────────────────── */

static GdkContentProvider *
on_drag_prepare(GtkDragSource *source, double x, double y, gpointer user_data)
{
    (void)source; (void)x; (void)y;
    g_warning("DND: on_drag_prepare called");
    GtkWidget *title_bar = GTK_WIDGET(user_data);
    GtkWidget *pane_box = gtk_widget_get_parent(title_bar);
    if (!pane_box) { g_warning("DND: no pane_box"); return NULL; }

    guint64 ptr = (guint64)(uintptr_t)pane_box;
    GValue val = G_VALUE_INIT;
    g_value_init(&val, G_TYPE_UINT64);
    g_value_set_uint64(&val, ptr);
    return gdk_content_provider_new_for_value(&val);
}

static void
on_drag_begin(GtkDragSource *source, GdkDrag *drag, gpointer user_data)
{
    (void)source;
    GtkWidget *title_bar = GTK_WIDGET(user_data);
    GtkWidget *pane_box = gtk_widget_get_parent(title_bar);
    if (!pane_box) return;

    /* Snapshot the entire pane (not just the title bar) for the drag icon */
    int w = gtk_widget_get_width(pane_box);
    int h = gtk_widget_get_height(pane_box);
    if (w <= 0 || h <= 0) return;

    GtkSnapshot *snapshot = gtk_snapshot_new();
    GtkWidget *pane_parent = gtk_widget_get_parent(pane_box);
    if (pane_parent) {
        graphene_point_t child_pos;
        if (gtk_widget_compute_point(pane_box, pane_parent,
                &GRAPHENE_POINT_INIT(0, 0), &child_pos)) {
            gtk_snapshot_translate(snapshot,
                &GRAPHENE_POINT_INIT(-child_pos.x, -child_pos.y));
        }
        gtk_widget_snapshot_child(pane_parent, pane_box, snapshot);
    }
    GdkPaintable *paintable = gtk_snapshot_free_to_paintable(snapshot, &GRAPHENE_SIZE_INIT(w, h));
    if (paintable) {
        /* Compute hotspot: map drag start point from title_bar to pane_box coords */
        double sx, sy;
        gtk_gesture_drag_get_start_point(GTK_GESTURE_DRAG(source), &sx, &sy);
        graphene_point_t pt;
        if (gtk_widget_compute_point(title_bar, pane_box,
                &GRAPHENE_POINT_INIT((float)sx, (float)sy), &pt)) {
            gtk_drag_source_set_icon(source, paintable, (int)pt.x, (int)pt.y);
        } else {
            gtk_drag_source_set_icon(source, paintable, w / 2, 0);
        }
        g_object_unref(paintable);
    }
    /* Dim the source pane while dragging */
    gtk_widget_set_opacity(pane_box, 0.4);
    (void)drag;
}

static void
on_drag_end(GtkDragSource *source, GdkDrag *drag, gboolean delete_data,
            gpointer user_data)
{
    (void)source; (void)drag; (void)delete_data;
    GtkWidget *title_bar = GTK_WIDGET(user_data);
    GtkWidget *pane_box = gtk_widget_get_parent(title_bar);
    if (pane_box)
        gtk_widget_set_opacity(pane_box, 1.0);
}

static GdkDragAction
on_drop_enter(GtkDropTarget *target, double x, double y, gpointer user_data)
{
    (void)target; (void)x; (void)y;
    GtkWidget *title_bar = GTK_WIDGET(user_data);
    GtkWidget *pane_box = gtk_widget_get_parent(title_bar);
    if (pane_box)
        gtk_widget_add_css_class(pane_box, "kterm-drop-target");
    return GDK_ACTION_MOVE;
}

static void
on_drop_leave(GtkDropTarget *target, gpointer user_data)
{
    (void)target;
    GtkWidget *title_bar = GTK_WIDGET(user_data);
    GtkWidget *pane_box = gtk_widget_get_parent(title_bar);
    if (pane_box)
        gtk_widget_remove_css_class(pane_box, "kterm-drop-target");
}

static gboolean
on_drop_received(GtkDropTarget *target, const GValue *value,
                 double x, double y, gpointer user_data)
{
    (void)target; (void)x; (void)y;
    GtkWidget *dest_title_bar = GTK_WIDGET(user_data);
    GtkWidget *dest_pane = gtk_widget_get_parent(dest_title_bar);
    if (dest_pane)
        gtk_widget_remove_css_class(dest_pane, "kterm-drop-target");

    if (!G_VALUE_HOLDS_UINT64(value)) return FALSE;

    guint64 ptr = g_value_get_uint64(value);
    GtkWidget *source_box = (GtkWidget *)(uintptr_t)ptr;
    GtkWidget *dest_box = gtk_widget_get_parent(dest_title_bar);

    if (!source_box || !dest_box || source_box == dest_box) return FALSE;

    /* Validate source is still a live pane box */
    if (!GTK_IS_BOX(source_box) ||
        !g_object_get_data(G_OBJECT(source_box), "kterm-title-label"))
        return FALSE;

    swap_pane_boxes(source_box, dest_box);
    return TRUE;
}

static void
on_pane_click_focus(GtkGestureClick *gesture, int n_press,
                    double x, double y, gpointer user_data)
{
    (void)n_press; (void)x; (void)y;
    GtkWidget *pane_box = GTK_WIDGET(user_data);
    focus_pane(pane_box);
    /* Deny so child widgets (VTE, list view, drag source, etc.) still work */
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
}

static void
setup_title_bar_dnd(GtkWidget *pane_box)
{
    GtkWidget *title_bar = gtk_widget_get_first_child(pane_box);
    if (!title_bar) return;

    /* Drag source */
    GtkDragSource *drag_source = gtk_drag_source_new();
    gtk_drag_source_set_actions(drag_source, GDK_ACTION_MOVE);
    g_signal_connect(drag_source, "prepare", G_CALLBACK(on_drag_prepare), title_bar);
    g_signal_connect(drag_source, "drag-begin", G_CALLBACK(on_drag_begin), title_bar);
    g_signal_connect(drag_source, "drag-end", G_CALLBACK(on_drag_end), title_bar);
    gtk_widget_add_controller(title_bar, GTK_EVENT_CONTROLLER(drag_source));

    /* Drop target */
    GtkDropTarget *drop_target = gtk_drop_target_new(G_TYPE_UINT64, GDK_ACTION_MOVE);
    g_signal_connect(drop_target, "drop", G_CALLBACK(on_drop_received), title_bar);
    g_signal_connect(drop_target, "enter", G_CALLBACK(on_drop_enter), title_bar);
    g_signal_connect(drop_target, "leave", G_CALLBACK(on_drop_leave), title_bar);
    gtk_widget_add_controller(title_bar, GTK_EVENT_CONTROLLER(drop_target));

    /* Click anywhere on the pane focuses it (capture phase, then deny) */
    GtkGesture *focus_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(focus_click), 1);
    gtk_event_controller_set_propagation_phase(
        GTK_EVENT_CONTROLLER(focus_click), GTK_PHASE_CAPTURE);
    g_signal_connect(focus_click, "pressed",
                     G_CALLBACK(on_pane_click_focus), pane_box);
    gtk_widget_add_controller(pane_box, GTK_EVENT_CONTROLLER(focus_click));
}

/* ── Title bar CSS ────────────────────────────────────────────────── */

static void
load_title_bar_css(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        ".kterm-title-bar {"
        "  padding: 1px 8px;"
        "  font-size: small;"
        "  background: alpha(currentColor, 0.06);"
        "}"
        ".kterm-title-bar.active {"
        "  background: alpha(currentColor, 0.15);"
        "  font-weight: bold;"
        "}"
        ".kterm-drop-target {"
        "  outline: 2px solid @accent_color;"
        "  outline-offset: -2px;"
        "}");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/* ── Apply settings to all terminals ──────────────────────────────── */

void
kterm_window_apply_settings(KtermWindow *self)
{
    KtermSettings *s = kterm_settings_get_default();
    int n = gtk_notebook_get_n_pages(self->notebook);
    for (int i = 0; i < n; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(self->notebook, i);
        GPtrArray *panes = g_ptr_array_new();
        collect_panes(page, panes);
        for (guint j = 0; j < panes->len; j++) {
            GtkWidget *tb = g_ptr_array_index(panes, j);
            if (kterm_explorer_is_explorer(tb) || kterm_browser_is_browser(tb))
                continue;
            VteTerminal *vte = kterm_terminal_get_vte(tb);
            if (vte)
                kterm_settings_apply_to_terminal(s, vte);
        }
        g_ptr_array_free(panes, TRUE);
    }
    update_pane_border_css();
    update_pane_outline_css();

    /* Sidebar visibility and position */
    if (self->sidebar) {
        gtk_widget_set_visible(self->sidebar, s->sidebar_visible);
        sidebar_set_position(self, s->sidebar_position);
    }
}

/* ── Edit title dialog ────────────────────────────────────────────── */

static void
on_title_ok(GtkButton *button, gpointer user_data)
{
    (void)button;
    GtkWindow *dialog = GTK_WINDOW(user_data);
    GtkWidget *entry = g_object_get_data(G_OBJECT(dialog), "entry");
    GtkWidget *page  = g_object_get_data(G_OBJECT(dialog), "page");
    GtkWidget *label = g_object_get_data(G_OBJECT(dialog), "label");

    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (text && *text) {
        gtk_label_set_text(GTK_LABEL(label), text);
        g_object_set_data(G_OBJECT(page), "custom-title", GINT_TO_POINTER(TRUE));
    }
    gtk_window_destroy(dialog);
}

static void
show_edit_title_dialog(KtermWindow *self)
{
    int page_num = gtk_notebook_get_current_page(self->notebook);
    GtkWidget *page = gtk_notebook_get_nth_page(self->notebook, page_num);
    if (!page) return;

    GtkWidget *label = g_object_get_data(G_OBJECT(page), "tab-label-widget");
    const char *current = label ? gtk_label_get_text(GTK_LABEL(label)) : "Terminal";

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), _("Edit Title"));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 300, -1);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);

    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), current);
    gtk_box_append(GTK_BOX(box), entry);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);

    GtkWidget *cancel = gtk_button_new_with_label(_("Cancel"));
    GtkWidget *ok = gtk_button_new_with_label(_("OK"));
    gtk_widget_add_css_class(ok, "suggested-action");

    gtk_box_append(GTK_BOX(btn_box), cancel);
    gtk_box_append(GTK_BOX(btn_box), ok);
    gtk_box_append(GTK_BOX(box), btn_box);
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    g_object_set_data(G_OBJECT(dialog), "entry", entry);
    g_object_set_data(G_OBJECT(dialog), "page", page);
    g_object_set_data(G_OBJECT(dialog), "label", label);

    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    g_signal_connect(ok, "clicked", G_CALLBACK(on_title_ok), dialog);
    g_signal_connect(entry, "activate", G_CALLBACK(on_title_ok), dialog);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void
on_file_manager_launch_done(GObject *source, GAsyncResult *result, gpointer user_data)
{
    (void)user_data;
    GtkFileLauncher *launcher = GTK_FILE_LAUNCHER(source);
    g_autoptr(GError) err = NULL;
    gtk_file_launcher_launch_finish(launcher, result, &err);
    if (err)
        g_warning("Failed to open file manager: %s", err->message);
    g_object_unref(launcher);
}

static void
menu_file_manager(GtkButton *btn, gpointer ud)
{
    popdown_and_unparent(GTK_WIDGET(btn));
    KtermWindow *self = KTERM_WINDOW(ud);
    if (!self->active_terminal) return;

    g_autofree char *cwd = kterm_terminal_get_cwd(self->active_terminal);
    if (!cwd) return;

    g_autoptr(GFile) file = g_file_new_for_path(cwd);
    GtkFileLauncher *launcher = gtk_file_launcher_new(file);
    gtk_file_launcher_launch(launcher, GTK_WINDOW(self), NULL,
                             on_file_manager_launch_done, NULL);
}

/* ── Sidebar: thumbnail generation & management ───────────────────── */

static GdkPaintable *
snapshot_tab_content(GtkWidget *content_box)
{
    int w = gtk_widget_get_width(content_box);
    int h = gtk_widget_get_height(content_box);
    if (w <= 0 || h <= 0) return NULL;

    GtkSnapshot *snap = gtk_snapshot_new();
    GtkWidget *parent = gtk_widget_get_parent(content_box);
    if (parent) {
        graphene_point_t pos;
        if (gtk_widget_compute_point(content_box, parent,
                &GRAPHENE_POINT_INIT(0, 0), &pos))
            gtk_snapshot_translate(snap, &GRAPHENE_POINT_INIT(-pos.x, -pos.y));
        gtk_widget_snapshot_child(parent, content_box, snap);
    }
    GdkPaintable *paintable = gtk_snapshot_free_to_paintable(snap,
        &GRAPHENE_SIZE_INIT(w, h));
    return paintable;
}

static void
on_sidebar_tab_clicked(GtkGestureClick *gesture, int n_press,
                       double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press; (void)x; (void)y;
    GtkWidget *item = GTK_WIDGET(user_data);
    int page_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "page-index"));

    GtkWidget *win = gtk_widget_get_ancestor(item, KTERM_TYPE_WINDOW);
    if (!win) return;
    KtermWindow *self = KTERM_WINDOW(win);
    gtk_notebook_set_current_page(self->notebook, page_idx);
}

static void
on_sidebar_tab_close(GtkButton *button, gpointer user_data)
{
    (void)button;
    GtkWidget *item = GTK_WIDGET(user_data);
    int page_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "page-index"));

    GtkWidget *win = gtk_widget_get_ancestor(item, KTERM_TYPE_WINDOW);
    if (!win) return;
    KtermWindow *self = KTERM_WINDOW(win);

    if (page_idx >= 0 && page_idx < gtk_notebook_get_n_pages(self->notebook)) {
        gtk_notebook_remove_page(self->notebook, page_idx);
        if (gtk_notebook_get_n_pages(self->notebook) == 0) {
            save_session(self);
            gtk_window_destroy(GTK_WINDOW(self));
        } else
            sidebar_rebuild(self);
    }
}

/* ── Sidebar DnD: reorder tabs by dragging thumbnails ──────────────── */

static GdkContentProvider *
on_sidebar_drag_prepare(GtkDragSource *source, double x, double y,
                        gpointer user_data)
{
    (void)source; (void)x; (void)y;
    GtkWidget *item = GTK_WIDGET(user_data);
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "page-index"));
    GValue val = G_VALUE_INIT;
    g_value_init(&val, G_TYPE_INT);
    g_value_set_int(&val, idx);
    return gdk_content_provider_new_for_value(&val);
}

static void
on_sidebar_drag_begin(GtkDragSource *source, GdkDrag *drag, gpointer user_data)
{
    (void)drag;
    GtkWidget *item = GTK_WIDGET(user_data);
    int w = gtk_widget_get_width(item);
    int h = gtk_widget_get_height(item);
    if (w <= 0 || h <= 0) return;

    GtkSnapshot *snap = gtk_snapshot_new();
    GtkWidget *parent = gtk_widget_get_parent(item);
    if (parent) {
        graphene_point_t pos;
        if (gtk_widget_compute_point(item, parent,
                &GRAPHENE_POINT_INIT(0, 0), &pos))
            gtk_snapshot_translate(snap, &GRAPHENE_POINT_INIT(-pos.x, -pos.y));
        gtk_widget_snapshot_child(parent, item, snap);
    }
    GdkPaintable *paintable = gtk_snapshot_free_to_paintable(snap,
        &GRAPHENE_SIZE_INIT(w, h));
    if (paintable) {
        gtk_drag_source_set_icon(source, paintable, w / 2, h / 2);
        g_object_unref(paintable);
    }
    gtk_widget_set_opacity(item, 0.4);
}

static void
on_sidebar_drag_end(GtkDragSource *source, GdkDrag *drag,
                    gboolean delete_data, gpointer user_data)
{
    (void)source; (void)drag; (void)delete_data;
    GtkWidget *item = GTK_WIDGET(user_data);
    gtk_widget_set_opacity(item, 1.0);
}

static GdkDragAction
on_sidebar_drop_enter(GtkDropTarget *target, double x, double y,
                      gpointer user_data)
{
    (void)target; (void)x; (void)y;
    GtkWidget *item = GTK_WIDGET(user_data);
    gtk_widget_add_css_class(item, "drag-hover");
    return GDK_ACTION_MOVE;
}

static void
on_sidebar_drop_leave(GtkDropTarget *target, gpointer user_data)
{
    (void)target;
    GtkWidget *item = GTK_WIDGET(user_data);
    gtk_widget_remove_css_class(item, "drag-hover");
}

static gboolean
on_sidebar_drop_received(GtkDropTarget *target, const GValue *value,
                         double x, double y, gpointer user_data)
{
    (void)target; (void)x; (void)y;
    GtkWidget *dest_item = GTK_WIDGET(user_data);
    gtk_widget_remove_css_class(dest_item, "drag-hover");

    if (!G_VALUE_HOLDS_INT(value)) return FALSE;

    int src_idx = g_value_get_int(value);
    int dest_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dest_item), "page-index"));

    if (src_idx == dest_idx) return FALSE;

    GtkWidget *win = gtk_widget_get_ancestor(dest_item, KTERM_TYPE_WINDOW);
    if (!win) return FALSE;
    KtermWindow *self = KTERM_WINDOW(win);

    /* Reorder the notebook page */
    GtkWidget *page = gtk_notebook_get_nth_page(self->notebook, src_idx);
    if (!page) return FALSE;

    gtk_notebook_reorder_child(self->notebook, page, dest_idx);
    /* sidebar_rebuild will be triggered by page-reordered signal */
    return TRUE;
}

static void
setup_sidebar_item_dnd(GtkWidget *item)
{
    /* Drag source */
    GtkDragSource *drag_src = gtk_drag_source_new();
    gtk_drag_source_set_actions(drag_src, GDK_ACTION_MOVE);
    g_signal_connect(drag_src, "prepare", G_CALLBACK(on_sidebar_drag_prepare), item);
    g_signal_connect(drag_src, "drag-begin", G_CALLBACK(on_sidebar_drag_begin), item);
    g_signal_connect(drag_src, "drag-end", G_CALLBACK(on_sidebar_drag_end), item);
    gtk_widget_add_controller(item, GTK_EVENT_CONTROLLER(drag_src));

    /* Drop target */
    GtkDropTarget *drop_tgt = gtk_drop_target_new(G_TYPE_INT, GDK_ACTION_MOVE);
    g_signal_connect(drop_tgt, "drop", G_CALLBACK(on_sidebar_drop_received), item);
    g_signal_connect(drop_tgt, "enter", G_CALLBACK(on_sidebar_drop_enter), item);
    g_signal_connect(drop_tgt, "leave", G_CALLBACK(on_sidebar_drop_leave), item);
    gtk_widget_add_controller(item, GTK_EVENT_CONTROLLER(drop_tgt));
}

static void
sidebar_update_count(KtermWindow *self)
{
    if (!self->sidebar_count_label) return;
    int n = gtk_notebook_get_n_pages(self->notebook);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d tab%s", n, n == 1 ? "" : "s");
    gtk_label_set_text(GTK_LABEL(self->sidebar_count_label), buf);
}

static void
sidebar_rebuild(KtermWindow *self)
{
    if (!self->sidebar_tab_list) return;

    /* Remove all children */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(self->sidebar_tab_list)))
        gtk_box_remove(GTK_BOX(self->sidebar_tab_list), child);

    int n = gtk_notebook_get_n_pages(self->notebook);
    int current = gtk_notebook_get_current_page(self->notebook);

    for (int i = 0; i < n; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(self->notebook, i);

        /* Item box: vertical, holds thumbnail overlay + label */
        GtkWidget *item = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_add_css_class(item, "sidebar-tab-item");
        gtk_widget_set_vexpand(item, FALSE);
        g_object_set_data(G_OBJECT(item), "page-index", GINT_TO_POINTER(i));

        if (i == current)
            gtk_widget_add_css_class(item, "active");

        /* Overlay: picture + close button */
        GtkWidget *overlay = gtk_overlay_new();

        /* Thumbnail picture — FILL prevents shrinking when scrollbar appears */
        GtkWidget *picture = gtk_picture_new();
        gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_FILL);
        gtk_widget_set_size_request(picture, -1, 90);
        gtk_widget_set_hexpand(picture, TRUE);

        /* Use cached thumbnail if available, only active page can be snapshotted live */
        GdkPaintable *cached = g_object_get_data(G_OBJECT(page), "sidebar-thumbnail");
        if (i == current) {
            GdkPaintable *paintable = snapshot_tab_content(page);
            if (paintable) {
                gtk_picture_set_paintable(GTK_PICTURE(picture), paintable);
                /* Cache it on the page for later reuse */
                g_object_set_data_full(G_OBJECT(page), "sidebar-thumbnail",
                    paintable, g_object_unref);
            } else if (cached) {
                gtk_picture_set_paintable(GTK_PICTURE(picture), cached);
            }
        } else if (cached) {
            gtk_picture_set_paintable(GTK_PICTURE(picture), cached);
        }

        gtk_overlay_set_child(GTK_OVERLAY(overlay), picture);

        /* Close button overlay */
        GtkWidget *close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
        gtk_button_set_has_frame(GTK_BUTTON(close_btn), FALSE);
        gtk_widget_set_halign(close_btn, GTK_ALIGN_END);
        gtk_widget_set_valign(close_btn, GTK_ALIGN_START);
        gtk_widget_add_css_class(close_btn, "sidebar-close-btn");
        g_signal_connect(close_btn, "clicked", G_CALLBACK(on_sidebar_tab_close), item);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), close_btn);

        gtk_box_append(GTK_BOX(item), overlay);

        /* Label */
        GtkWidget *label_widget = g_object_get_data(G_OBJECT(page), "tab-label-widget");
        const char *title = label_widget ? gtk_label_get_text(GTK_LABEL(label_widget)) : _("Terminal");
        GtkWidget *label = gtk_label_new(title);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(label), 20);
        gtk_box_append(GTK_BOX(item), label);

        /* Click gesture to switch tab */
        GtkGesture *click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
        g_signal_connect(click, "pressed", G_CALLBACK(on_sidebar_tab_clicked), item);
        gtk_widget_add_controller(item, GTK_EVENT_CONTROLLER(click));

        /* DnD to reorder tabs */
        setup_sidebar_item_dnd(item);

        gtk_box_append(GTK_BOX(self->sidebar_tab_list), item);
    }

    sidebar_update_count(self);
}

static void
sidebar_update_active(KtermWindow *self, int active_page)
{
    if (!self->sidebar_tab_list) return;

    int i = 0;
    for (GtkWidget *child = gtk_widget_get_first_child(self->sidebar_tab_list);
         child; child = gtk_widget_get_next_sibling(child), i++) {
        if (i == active_page)
            gtk_widget_add_css_class(child, "active");
        else
            gtk_widget_remove_css_class(child, "active");
    }
}

static gboolean
sidebar_refresh_active_thumbnail(gpointer user_data)
{
    KtermWindow *self = KTERM_WINDOW(user_data);
    if (!self->sidebar_tab_list || !gtk_widget_get_visible(self->sidebar))
        return G_SOURCE_CONTINUE;

    int current = gtk_notebook_get_current_page(self->notebook);
    if (current < 0) return G_SOURCE_CONTINUE;

    GtkWidget *page = gtk_notebook_get_nth_page(self->notebook, current);
    if (!page) return G_SOURCE_CONTINUE;

    /* Find the corresponding sidebar item */
    int i = 0;
    for (GtkWidget *child = gtk_widget_get_first_child(self->sidebar_tab_list);
         child; child = gtk_widget_get_next_sibling(child), i++) {
        if (i == current) {
            /* child is the item box; first child is the overlay; overlay child is picture */
            GtkWidget *overlay = gtk_widget_get_first_child(child);
            if (!overlay) break;
            GtkWidget *picture = gtk_overlay_get_child(GTK_OVERLAY(overlay));
            if (!picture || !GTK_IS_PICTURE(picture)) break;

            GdkPaintable *paintable = snapshot_tab_content(page);
            if (paintable) {
                gtk_picture_set_paintable(GTK_PICTURE(picture), paintable);
                /* Cache for when this page becomes inactive */
                g_object_set_data_full(G_OBJECT(page), "sidebar-thumbnail",
                    g_object_ref(paintable), g_object_unref);
                g_object_unref(paintable);
            }
            break;
        }
    }
    return G_SOURCE_CONTINUE;
}

static void
on_notebook_switch_page(GtkNotebook *notebook, GtkWidget *page,
                        guint page_num, gpointer user_data)
{
    (void)page;
    KtermWindow *self = KTERM_WINDOW(user_data);

    /* Capture thumbnail of the page we're leaving (it's still visible now) */
    int old_page = gtk_notebook_get_current_page(notebook);
    if (old_page >= 0 && old_page != (int)page_num) {
        GtkWidget *old = gtk_notebook_get_nth_page(notebook, old_page);
        if (old) {
            GdkPaintable *paintable = snapshot_tab_content(old);
            if (paintable)
                g_object_set_data_full(G_OBJECT(old), "sidebar-thumbnail",
                    paintable, g_object_unref);
        }
    }

    sidebar_update_active(self, (int)page_num);
}

static void
on_notebook_page_added(GtkNotebook *notebook, GtkWidget *child,
                       guint page_num, gpointer user_data)
{
    (void)notebook; (void)child; (void)page_num;
    KtermWindow *self = KTERM_WINDOW(user_data);
    sidebar_rebuild(self);
}

static void
on_notebook_page_removed(GtkNotebook *notebook, GtkWidget *child,
                         guint page_num, gpointer user_data)
{
    (void)notebook; (void)child; (void)page_num;
    KtermWindow *self = KTERM_WINDOW(user_data);
    sidebar_rebuild(self);
}

static void
on_notebook_page_reordered(GtkNotebook *notebook, GtkWidget *child,
                           guint page_num, gpointer user_data)
{
    (void)notebook; (void)child; (void)page_num;
    KtermWindow *self = KTERM_WINDOW(user_data);
    sidebar_rebuild(self);
}

static void
act_toggle_sidebar(GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    KtermWindow *self = KTERM_WINDOW(ud);
    KtermSettings *s = kterm_settings_get_default();
    gboolean visible = gtk_widget_get_visible(self->sidebar);
    gtk_widget_set_visible(self->sidebar, !visible);
    s->sidebar_visible = !visible;
}

static void
sidebar_set_position(KtermWindow *self, int position)
{
    KtermSettings *s = kterm_settings_get_default();
    s->sidebar_position = position;

    /* Reparent: remove both children, re-add in correct order */
    g_object_ref(self->sidebar);
    g_object_ref(GTK_WIDGET(self->notebook));

    gtk_paned_set_start_child(GTK_PANED(self->main_paned), NULL);
    gtk_paned_set_end_child(GTK_PANED(self->main_paned), NULL);

    if (position == 0) {
        /* Left */
        gtk_paned_set_start_child(GTK_PANED(self->main_paned), self->sidebar);
        gtk_paned_set_end_child(GTK_PANED(self->main_paned), GTK_WIDGET(self->notebook));
    } else {
        /* Right */
        gtk_paned_set_start_child(GTK_PANED(self->main_paned), GTK_WIDGET(self->notebook));
        gtk_paned_set_end_child(GTK_PANED(self->main_paned), self->sidebar);
    }

    g_object_unref(self->sidebar);
    g_object_unref(GTK_WIDGET(self->notebook));

    /* Update sidebar border CSS based on position */
    if (position == 0) {
        gtk_widget_remove_css_class(self->sidebar, "kterm-sidebar-right");
        gtk_widget_add_css_class(self->sidebar, "kterm-sidebar-left");
    } else {
        gtk_widget_remove_css_class(self->sidebar, "kterm-sidebar-left");
        gtk_widget_add_css_class(self->sidebar, "kterm-sidebar-right");
    }
}

static void
load_sidebar_css(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        ".kterm-sidebar {"
        "  background: alpha(currentColor, 0.04);"
        "}"
        ".kterm-sidebar-left {"
        "  border-right: 1px solid alpha(currentColor, 0.08);"
        "}"
        ".kterm-sidebar-right {"
        "  border-left: 1px solid alpha(currentColor, 0.08);"
        "}"
        ".sidebar-tab-item {"
        "  padding: 4px;"
        "  border-radius: 6px;"
        "  margin: 2px 4px;"
        "  border: 2px solid transparent;"
        "}"
        ".sidebar-tab-item.active {"
        "  background: alpha(@accent_color, 0.15);"
        "  border: 2px solid alpha(@accent_color, 0.6);"
        "}"
        ".sidebar-tab-item:hover {"
        "  background: alpha(currentColor, 0.06);"
        "}"
        ".sidebar-close-btn {"
        "  opacity: 0.5;"
        "  min-width: 16px;"
        "  min-height: 16px;"
        "  padding: 0;"
        "  margin: 2px;"
        "}"
        ".sidebar-close-btn:hover {"
        "  opacity: 1.0;"
        "}"
        ".sidebar-header {"
        "  padding: 4px;"
        "}"
        ".sidebar-count {"
        "  font-size: small;"
        "  padding: 0 4px;"
        "}"
        ".sidebar-tab-item.drag-hover {"
        "  background: alpha(@accent_color, 0.3);"
        "  border: 2px dashed alpha(@accent_color, 0.8);"
        "}"
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static GtkWidget *
build_sidebar(KtermWindow *self)
{
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sidebar, "kterm-sidebar");
    gtk_widget_set_size_request(sidebar, 120, -1);

    /* Header with + button and toggle button */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(header, "sidebar-header");

    GtkWidget *add_btn = gtk_button_new_from_icon_name("tab-new-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(add_btn), FALSE);
    gtk_widget_set_tooltip_text(add_btn, _("New Tab"));
    g_signal_connect_swapped(add_btn, "clicked", G_CALLBACK(add_tab), self);
    gtk_box_append(GTK_BOX(header), add_btn);

    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(header), spacer);

    GtkWidget *count_label = gtk_label_new("0 tabs");
    gtk_widget_add_css_class(count_label, "dim-label");
    gtk_widget_add_css_class(count_label, "sidebar-count");
    gtk_box_append(GTK_BOX(header), count_label);
    self->sidebar_count_label = count_label;

    gtk_box_append(GTK_BOX(sidebar), header);

    /* Scrolled window with tab list */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled, TRUE);

    GtkWidget *tab_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), tab_list);
    gtk_box_append(GTK_BOX(sidebar), scrolled);

    self->sidebar_tab_list = tab_list;
    return sidebar;
}

/* ── Keyboard shortcut actions (GSimpleAction for GActionMap) ──────── */

static void
act_new_tab(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; add_tab(KTERM_WINDOW(ud)); }
static void
act_close_terminal(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; close_current_pane(KTERM_WINDOW(ud)); }
static void
act_split_vertical(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; split_terminal(KTERM_WINDOW(ud), GTK_ORIENTATION_HORIZONTAL); }
static void
act_split_horizontal(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; split_terminal(KTERM_WINDOW(ud), GTK_ORIENTATION_VERTICAL); }
static void
act_copy(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; KtermWindow *s=KTERM_WINDOW(ud); if(s->active_terminal) vte_terminal_copy_clipboard_format(s->active_terminal,VTE_FORMAT_TEXT); }
static void
act_paste(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; KtermWindow *s=KTERM_WINDOW(ud); if(s->active_terminal) vte_terminal_paste_clipboard(s->active_terminal); }
static void
act_nav_left(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; navigate(KTERM_WINDOW(ud), -1, 0); }
static void
act_nav_right(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; navigate(KTERM_WINDOW(ud), 1, 0); }
static void
act_nav_up(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; navigate(KTERM_WINDOW(ud), 0, -1); }
static void
act_nav_down(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; navigate(KTERM_WINDOW(ud), 0, 1); }
static void
act_next_tab(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; gtk_notebook_next_page(KTERM_WINDOW(ud)->notebook); }
static void
act_prev_tab(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; gtk_notebook_prev_page(KTERM_WINDOW(ud)->notebook); }
static void
act_quit(GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; gtk_window_destroy(GTK_WINDOW(ud)); }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const GActionEntry win_action_entries[] = {
    { "new-tab",          act_new_tab,          NULL, NULL, NULL },
    { "close-terminal",   act_close_terminal,   NULL, NULL, NULL },
    { "split-vertical",   act_split_vertical,   NULL, NULL, NULL },
    { "split-horizontal", act_split_horizontal, NULL, NULL, NULL },
    { "copy",             act_copy,             NULL, NULL, NULL },
    { "paste",            act_paste,            NULL, NULL, NULL },
    { "nav-left",         act_nav_left,         NULL, NULL, NULL },
    { "nav-right",        act_nav_right,        NULL, NULL, NULL },
    { "nav-up",           act_nav_up,           NULL, NULL, NULL },
    { "nav-down",         act_nav_down,         NULL, NULL, NULL },
    { "next-tab",         act_next_tab,         NULL, NULL, NULL },
    { "prev-tab",         act_prev_tab,         NULL, NULL, NULL },
    { "quit",             act_quit,             NULL, NULL, NULL },
    { "toggle-sidebar",   act_toggle_sidebar,   NULL, NULL, NULL },
};
#pragma GCC diagnostic pop

static void
register_actions(KtermWindow *self)
{
    g_action_map_add_action_entries(G_ACTION_MAP(self),
                                    win_action_entries,
                                    G_N_ELEMENTS(win_action_entries),
                                    self);
}

/* ── Session restore ──────────────────────────────────────────────── */

static GtkWidget *
restore_pane_tree(KtermWindow *self, JsonObject *obj)
{
    const char *type = json_object_get_string_member_with_default(obj, "type", "");

    if (g_strcmp0(type, "split") == 0) {
        const char *orient_str = json_object_get_string_member_with_default(
            obj, "orientation", "horizontal");
        GtkOrientation orient = g_strcmp0(orient_str, "vertical") == 0
            ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
        int position = (int)json_object_get_int_member_with_default(obj, "position", 0);

        GtkWidget *paned = gtk_paned_new(orient);
        gtk_widget_set_hexpand(paned, TRUE);
        gtk_widget_set_vexpand(paned, TRUE);

        JsonNode *start_node = json_object_get_member(obj, "start");
        JsonNode *end_node = json_object_get_member(obj, "end");

        GtkWidget *start_w = NULL;
        GtkWidget *end_w = NULL;

        if (start_node && !json_node_is_null(start_node))
            start_w = restore_pane_tree(self, json_node_get_object(start_node));
        if (end_node && !json_node_is_null(end_node))
            end_w = restore_pane_tree(self, json_node_get_object(end_node));

        if (start_w) gtk_paned_set_start_child(GTK_PANED(paned), start_w);
        if (end_w)   gtk_paned_set_end_child(GTK_PANED(paned), end_w);
        gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
        gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
        if (position > 0)
            gtk_paned_set_position(GTK_PANED(paned), position);

    
        return paned;

    } else if (g_strcmp0(type, "browser") == 0) {
        const char *url = json_object_get_string_member_with_default(
            obj, "url", "https://www.google.com");

        GtkWidget *browser_box = kterm_browser_new(url);
        setup_browser_tracking(self, browser_box);
        g_signal_connect(browser_box, "terminal-exited",
                         G_CALLBACK(on_terminal_exited), self);
        return browser_box;

    } else if (g_strcmp0(type, "explorer") == 0) {
        const char *path = json_object_get_string_member_with_default(
            obj, "path", g_get_home_dir());
        if (!g_file_test(path, G_FILE_TEST_IS_DIR))
            path = g_get_home_dir();

        GtkWidget *explorer_box = kterm_explorer_new(path);
        setup_explorer_tracking(self, explorer_box);
        g_signal_connect(explorer_box, "terminal-exited",
                         G_CALLBACK(on_terminal_exited), self);
        return explorer_box;

    } else {
        /* Terminal (default) */
        const char *cwd = json_object_get_string_member_with_default(
            obj, "cwd", g_get_home_dir());
        if (!g_file_test(cwd, G_FILE_TEST_IS_DIR))
            cwd = g_get_home_dir();

        GtkWidget *terminal_box = kterm_terminal_new_with_cwd(cwd);
        setup_focus_tracking(self, terminal_box);
        g_signal_connect(terminal_box, "terminal-exited",
                         G_CALLBACK(on_terminal_exited), self);
        return terminal_box;
    }
}

static gboolean
restore_session(KtermWindow *self)
{
    g_autofree char *path = get_session_path();

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_file(parser, path, NULL)) {
        g_object_unref(parser);
        return FALSE;
    }

    JsonNode *root_node = json_parser_get_root(parser);
    if (!root_node || !JSON_NODE_HOLDS_OBJECT(root_node)) {
        g_object_unref(parser);
        return FALSE;
    }

    JsonObject *root = json_node_get_object(root_node);

    /* Window geometry */
    gboolean maximized = json_object_get_boolean_member_with_default(
        root, "maximized", FALSE);
    int w = (int)json_object_get_int_member_with_default(root, "width", 800);
    int h = (int)json_object_get_int_member_with_default(root, "height", 600);

    if (w > 0 && h > 0)
        gtk_window_set_default_size(GTK_WINDOW(self), w, h);
    if (maximized)
        gtk_window_maximize(GTK_WINDOW(self));

    /* Tabs */
    JsonArray *tabs = json_object_get_array_member(root, "tabs");
    if (!tabs || json_array_get_length(tabs) == 0) {
        g_object_unref(parser);
        return FALSE;
    }

    guint n = json_array_get_length(tabs);
    for (guint i = 0; i < n; i++) {
        JsonNode *tab_node = json_array_get_element(tabs, i);
        if (!tab_node || !JSON_NODE_HOLDS_OBJECT(tab_node))
            continue;

        GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_hexpand(content_box, TRUE);
        gtk_widget_set_vexpand(content_box, TRUE);

        GtkWidget *pane_tree = restore_pane_tree(self,
            json_node_get_object(tab_node));
        if (pane_tree)
            gtk_box_append(GTK_BOX(content_box), pane_tree);

        GtkWidget *tab_label = create_tab_label(_("Terminal"), content_box);
        gtk_notebook_append_page(self->notebook, content_box, tab_label);
        gtk_notebook_set_tab_reorderable(self->notebook, content_box, TRUE);
    }

    /* Restore active tab */
    int active = (int)json_object_get_int_member_with_default(
        root, "active_tab", 0);
    if (active >= 0 && active < (int)n)
        gtk_notebook_set_current_page(self->notebook, active);

    /* Focus the first terminal in the active tab */
    GtkWidget *active_content = gtk_notebook_get_nth_page(
        self->notebook, gtk_notebook_get_current_page(self->notebook));
    if (active_content) {
        g_autoptr(GPtrArray) panes = g_ptr_array_new();
        collect_panes(active_content, panes);
        if (panes->len > 0)
            focus_pane(g_ptr_array_index(panes, 0));
    }

    g_object_unref(parser);
    return TRUE;
}

/* ── Double-click on paned separator → equalize tiles ──────────────── */

/* Count leaf panes in a given orientation direction */
static int
count_leaves_in_direction(GtkWidget *widget, GtkOrientation orient)
{
    if (GTK_IS_PANED(widget) &&
        gtk_orientable_get_orientation(GTK_ORIENTABLE(widget)) == orient) {
        GtkWidget *start = gtk_paned_get_start_child(GTK_PANED(widget));
        GtkWidget *end = gtk_paned_get_end_child(GTK_PANED(widget));
        return count_leaves_in_direction(start, orient) +
               count_leaves_in_direction(end, orient);
    }
    return 1;
}

/* Recursively set paned positions so all leaves get equal space */
static void
equalize_panes(GtkWidget *widget, GtkOrientation orient)
{
    if (!GTK_IS_PANED(widget))
        return;

    GtkOrientation this_orient =
        gtk_orientable_get_orientation(GTK_ORIENTABLE(widget));
    GtkWidget *start = gtk_paned_get_start_child(GTK_PANED(widget));
    GtkWidget *end = gtk_paned_get_end_child(GTK_PANED(widget));

    if (this_orient == orient) {
        int n_start = count_leaves_in_direction(start, orient);
        int n_total = n_start + count_leaves_in_direction(end, orient);
        int size = (orient == GTK_ORIENTATION_HORIZONTAL)
            ? gtk_widget_get_width(widget)
            : gtk_widget_get_height(widget);
        if (n_total > 0 && size > 0)
            gtk_paned_set_position(GTK_PANED(widget), size * n_start / n_total);
    }

    if (start) equalize_panes(start, orient);
    if (end)   equalize_panes(end, orient);
}

/* Find the GtkPaned whose separator contains the given widget */
static GtkPaned *
find_paned_for_separator(GtkWidget *target, GtkWidget *root)
{
    for (GtkWidget *w = target; w && w != root; w = gtk_widget_get_parent(w)) {
        GtkWidget *parent = gtk_widget_get_parent(w);
        if (parent && GTK_IS_PANED(parent)) {
            GtkPaned *paned = GTK_PANED(parent);
            /* If w is neither start nor end child, it must be the separator */
            if (w != gtk_paned_get_start_child(paned) &&
                w != gtk_paned_get_end_child(paned))
                return paned;
        }
    }
    return NULL;
}

/* Manual double-click tracking — GtkPaned's internal drag gesture
   interferes with GtkGestureClick's n_press counting. */
static guint32 last_sep_time = 0;
static double  last_sep_x = 0;
static double  last_sep_y = 0;

static void
on_separator_double_click(GtkGestureClick *gesture, int n_press,
                          double x, double y, gpointer user_data)
{
    (void)n_press;  /* unreliable on paned separators — track manually */

    KtermWindow *self = KTERM_WINDOW(user_data);
    GtkWidget *target = gtk_widget_pick(
        GTK_WIDGET(self), x, y, GTK_PICK_DEFAULT);
    if (!target) { last_sep_time = 0; return; }

    GtkPaned *paned = find_paned_for_separator(target, GTK_WIDGET(self));
    if (!paned) { last_sep_time = 0; return; }

    GdkEvent *event = gtk_event_controller_get_current_event(
        GTK_EVENT_CONTROLLER(gesture));
    guint32 now = event ? gdk_event_get_time(event) : 0;

    double dx = x - last_sep_x;
    double dy = y - last_sep_y;
    gboolean is_double = (now > 0 && last_sep_time > 0 &&
                          (now - last_sep_time) < 500 &&
                          dx * dx + dy * dy < 400);  /* within 20px */

    if (is_double) {
        GtkOrientation orient =
            gtk_orientable_get_orientation(GTK_ORIENTABLE(paned));

        int page = gtk_notebook_get_current_page(self->notebook);
        GtkWidget *tab = gtk_notebook_get_nth_page(self->notebook, page);
        if (!tab) { last_sep_time = 0; return; }
        GtkWidget *root = get_tab_root_pane(tab);
        if (!root) { last_sep_time = 0; return; }

        gtk_gesture_set_state(GTK_GESTURE(gesture),
                              GTK_EVENT_SEQUENCE_CLAIMED);
        equalize_panes(root, orient);
        last_sep_time = 0;
    } else {
        last_sep_time = now;
        last_sep_x = x;
        last_sep_y = y;
    }
}

/* ── Double-click on tab bar ───────────────────────────────────────── */

static void
on_notebook_double_click(GtkGestureClick *gesture, int n_press,
                         double x, double y, gpointer user_data)
{
    if (n_press != 2)
        return;

    KtermWindow *self = KTERM_WINDOW(user_data);
    GtkWidget *notebook = GTK_WIDGET(self->notebook);

    /* Use gtk_widget_pick to find what was clicked */
    GtkWidget *target = gtk_widget_pick(notebook, x, y, GTK_PICK_DEFAULT);

    /* Walk up from target: if it's inside a tab page (the content area),
     * or inside a tab label, don't create a new tab */
    for (GtkWidget *w = target; w && w != notebook; w = gtk_widget_get_parent(w)) {
        /* If we hit any page content, bail out */
        int n = gtk_notebook_get_n_pages(self->notebook);
        for (int i = 0; i < n; i++) {
            if (w == gtk_notebook_get_nth_page(self->notebook, i))
                return;
        }
    }

    /* Clicked on tab bar empty area — create new tab */
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    add_tab(self);
}

/* ── Class init ───────────────────────────────────────────────────── */

static void
kterm_window_class_init(KtermWindowClass *klass)
{
    GtkWidgetClass *wc = GTK_WIDGET_CLASS(klass);

    /* Keyboard bindings — resolve "win.*" via GActionMap at runtime */
    gtk_widget_class_add_binding_action(wc, GDK_KEY_t, GDK_SHIFT_MASK | GDK_CONTROL_MASK, "win.new-tab", NULL);
    gtk_widget_class_add_binding_action(wc, GDK_KEY_w, GDK_SHIFT_MASK | GDK_CONTROL_MASK, "win.close-terminal", NULL);
    gtk_widget_class_add_binding_action(wc, GDK_KEY_e, GDK_SHIFT_MASK | GDK_CONTROL_MASK, "win.split-vertical", NULL);
    gtk_widget_class_add_binding_action(wc, GDK_KEY_o, GDK_SHIFT_MASK | GDK_CONTROL_MASK, "win.split-horizontal", NULL);
    gtk_widget_class_add_binding_action(wc, GDK_KEY_c, GDK_SHIFT_MASK | GDK_CONTROL_MASK, "win.copy", NULL);
    gtk_widget_class_add_binding_action(wc, GDK_KEY_v, GDK_SHIFT_MASK | GDK_CONTROL_MASK, "win.paste", NULL);
    gtk_widget_class_add_binding_action(wc, GDK_KEY_Left,  GDK_ALT_MASK, "win.nav-left", NULL);
    gtk_widget_class_add_binding_action(wc, GDK_KEY_Right, GDK_ALT_MASK, "win.nav-right", NULL);
    gtk_widget_class_add_binding_action(wc, GDK_KEY_Up,    GDK_ALT_MASK, "win.nav-up", NULL);
    gtk_widget_class_add_binding_action(wc, GDK_KEY_Down,  GDK_ALT_MASK, "win.nav-down", NULL);
    gtk_widget_class_add_binding_action(wc, GDK_KEY_Page_Up,   GDK_CONTROL_MASK, "win.prev-tab", NULL);
    gtk_widget_class_add_binding_action(wc, GDK_KEY_Page_Down, GDK_CONTROL_MASK, "win.next-tab", NULL);
    gtk_widget_class_add_binding_action(wc, GDK_KEY_q, GDK_SHIFT_MASK | GDK_CONTROL_MASK, "win.quit", NULL);
    gtk_widget_class_add_binding_action(wc, GDK_KEY_b, GDK_SHIFT_MASK | GDK_CONTROL_MASK, "win.toggle-sidebar", NULL);
}

/* ── Instance init ────────────────────────────────────────────────── */

static void
kterm_window_init(KtermWindow *self)
{
    gtk_window_set_title(GTK_WINDOW(self), "kterm");
    gtk_window_set_default_size(GTK_WINDOW(self), 800, 600);

    g_signal_connect(self, "close-request", G_CALLBACK(on_close_request), NULL);
    g_signal_connect(self, "notify::surface", G_CALLBACK(on_surface_notify), self);

    /* Register all actions on the window's GActionMap (prefix "win.") */
    register_actions(self);

    /* Header bar with version label and sidebar toggle */
    GtkWidget *header = gtk_header_bar_new();
    GtkWidget *version_label = gtk_label_new("kterm [" KTERM_VERSION "]");
    gtk_widget_add_css_class(version_label, "dim-label");
    gtk_widget_set_valign(version_label, GTK_ALIGN_CENTER);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), version_label);

    GtkWidget *sidebar_toggle = gtk_button_new_from_icon_name("sidebar-show-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(sidebar_toggle), FALSE);
    gtk_widget_set_tooltip_text(sidebar_toggle, _("Toggle Sidebar (Ctrl+Shift+B)"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(sidebar_toggle), "win.toggle-sidebar");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), sidebar_toggle);

    gtk_window_set_titlebar(GTK_WINDOW(self), header);

    /* Double-click on paned separator equalizes tiles */
    GtkGesture *sep_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(sep_click), 1);
    gtk_event_controller_set_propagation_phase(
        GTK_EVENT_CONTROLLER(sep_click), GTK_PHASE_CAPTURE);
    g_signal_connect(sep_click, "pressed",
                     G_CALLBACK(on_separator_double_click), self);
    gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(sep_click));

    /* Global CSS providers */
    update_pane_border_css();
    update_pane_outline_css();

    static gboolean title_css_loaded = FALSE;
    if (!title_css_loaded) {
        load_title_bar_css();
        title_css_loaded = TRUE;
    }

    static gboolean sidebar_css_loaded = FALSE;
    if (!sidebar_css_loaded) {
        load_sidebar_css();
        sidebar_css_loaded = TRUE;
    }

    KtermSettings *settings = kterm_settings_get_default();

    /* Notebook (tabs hidden — sidebar replaces the tab bar) */
    self->notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_notebook_set_scrollable(self->notebook, TRUE);
    gtk_notebook_set_show_tabs(self->notebook, FALSE);

    /* Build sidebar */
    self->sidebar = build_sidebar(self);
    gtk_widget_set_visible(self->sidebar, settings->sidebar_visible);

    /* Main paned: sidebar | notebook */
    self->main_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_shrink_start_child(GTK_PANED(self->main_paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(self->main_paned), FALSE);

    if (settings->sidebar_position == 0) {
        gtk_paned_set_start_child(GTK_PANED(self->main_paned), self->sidebar);
        gtk_paned_set_end_child(GTK_PANED(self->main_paned), GTK_WIDGET(self->notebook));
        gtk_widget_add_css_class(self->sidebar, "kterm-sidebar-left");
    } else {
        gtk_paned_set_start_child(GTK_PANED(self->main_paned), GTK_WIDGET(self->notebook));
        gtk_paned_set_end_child(GTK_PANED(self->main_paned), self->sidebar);
        gtk_widget_add_css_class(self->sidebar, "kterm-sidebar-right");
    }

    gtk_paned_set_position(GTK_PANED(self->main_paned), settings->sidebar_width);

    gtk_window_set_child(GTK_WINDOW(self), self->main_paned);

    /* Notebook signals for sidebar sync */
    g_signal_connect(self->notebook, "switch-page",
                     G_CALLBACK(on_notebook_switch_page), self);
    g_signal_connect(self->notebook, "page-added",
                     G_CALLBACK(on_notebook_page_added), self);
    g_signal_connect(self->notebook, "page-removed",
                     G_CALLBACK(on_notebook_page_removed), self);
    g_signal_connect(self->notebook, "page-reordered",
                     G_CALLBACK(on_notebook_page_reordered), self);

    /* Double-click on empty tab bar area creates a new tab */
    GtkGesture *dbl_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(dbl_click), 1);
    g_signal_connect(dbl_click, "pressed",
                     G_CALLBACK(on_notebook_double_click), self);
    gtk_widget_add_controller(GTK_WIDGET(self->notebook),
                              GTK_EVENT_CONTROLLER(dbl_click));

    /* Try restoring previous session; fall back to a fresh tab */
    if (!restore_session(self))
        add_tab(self);

    /* Periodic thumbnail refresh (every 2 seconds) */
    self->thumb_timer_id = g_timeout_add(2000, sidebar_refresh_active_thumbnail, self);
}

KtermWindow *
kterm_window_new(KtermApplication *app)
{
    return g_object_new(KTERM_TYPE_WINDOW,
                        "application", app,
                        NULL);
}
