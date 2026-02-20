#include "kterm-explorer.h"
#include <libintl.h>
#include <string.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-desktop-thumbnail.h>

#define _(x) gettext(x)
#define DIR_ATTRS "standard::*,time::modified,thumbnail::path"

/* ── Zoom levels ─────────────────────────────────────────────────── */

static const int GRID_ZOOM_LEVELS[] = { 48, 64, 96, 128, 192, 256, 384, 512 };
static const int GRID_ZOOM_COUNT   = 8;
static const int GRID_ZOOM_DEFAULT = 1;  /* index: 64px */

static const int LIST_ZOOM_LEVELS[] = { 16, 24, 32, 48, 64, 96, 128 };
static const int LIST_ZOOM_COUNT   = 7;
static const int LIST_ZOOM_DEFAULT = 0;  /* index: 16px */

/* ── Thumbnail factory (singleton) ────────────────────────────────── */

static GnomeDesktopThumbnailFactory *thumb_factory = NULL;

static GnomeDesktopThumbnailFactory *
get_thumb_factory(void)
{
    if (!thumb_factory)
        thumb_factory = gnome_desktop_thumbnail_factory_new(
            GNOME_DESKTOP_THUMBNAIL_SIZE_XLARGE);
    return thumb_factory;
}

/* Data passed to async thumbnail generation callback */
typedef struct {
    GtkWidget *image;
    char      *uri;
    time_t     mtime;
    int        pixel_size;
} ThumbGenData;

static void
on_thumb_generated(GObject *source, GAsyncResult *res, gpointer user_data)
{
    ThumbGenData *tgd = user_data;
    GnomeDesktopThumbnailFactory *factory =
        GNOME_DESKTOP_THUMBNAIL_FACTORY(source);

    GError *error = NULL;
    GdkPixbuf *pixbuf =
        gnome_desktop_thumbnail_factory_generate_thumbnail_finish(
            factory, res, &error);

    if (pixbuf) {
        gnome_desktop_thumbnail_factory_save_thumbnail(
            factory, pixbuf, tgd->uri, tgd->mtime, NULL, NULL);

        /* Only update if the widget still expects this URI */
        const char *cur = g_object_get_data(G_OBJECT(tgd->image), "thumb-uri");
        if (cur && g_strcmp0(cur, tgd->uri) == 0) {
            GdkTexture *texture = gdk_texture_new_for_pixbuf(pixbuf);
            gtk_image_set_from_paintable(GTK_IMAGE(tgd->image),
                                          GDK_PAINTABLE(texture));
            gtk_image_set_pixel_size(GTK_IMAGE(tgd->image), tgd->pixel_size);
            g_object_unref(texture);
        }
        g_object_unref(pixbuf);
    } else {
        gnome_desktop_thumbnail_factory_create_failed_thumbnail(
            factory, tgd->uri, tgd->mtime, NULL, NULL);
        if (error) g_error_free(error);
    }

    g_object_unref(tgd->image);
    g_free(tgd->uri);
    g_free(tgd);
}

static void
load_thumbnail(GtkImage *image, GFileInfo *info,
               const char *dir_path, int pixel_size)
{
    const char *name = g_file_info_get_name(info);
    g_autofree char *full_path = g_build_filename(dir_path, name, NULL);
    g_autofree char *uri = g_filename_to_uri(full_path, NULL, NULL);
    if (!uri) return;

    const char *content_type = g_file_info_get_content_type(info);
    GDateTime *dt = g_file_info_get_modification_date_time(info);
    time_t mtime = dt ? g_date_time_to_unix(dt) : 0;
    if (dt) g_date_time_unref(dt);

    GnomeDesktopThumbnailFactory *factory = get_thumb_factory();

    /* 1. Check GIO-cached thumbnail path */
    const char *thumb_path =
        g_file_info_get_attribute_byte_string(info, "thumbnail::path");
    if (thumb_path) {
        GdkTexture *texture = gdk_texture_new_from_filename(thumb_path, NULL);
        if (texture) {
            gtk_image_set_from_paintable(image, GDK_PAINTABLE(texture));
            gtk_image_set_pixel_size(image, pixel_size);
            g_object_unref(texture);
            return;
        }
    }

    /* 2. Check factory lookup (covers all cache sizes) */
    char *cached = gnome_desktop_thumbnail_factory_lookup(factory, uri, mtime);
    if (cached) {
        GdkTexture *texture = gdk_texture_new_from_filename(cached, NULL);
        if (texture) {
            gtk_image_set_from_paintable(image, GDK_PAINTABLE(texture));
            gtk_image_set_pixel_size(image, pixel_size);
            g_object_unref(texture);
        }
        g_free(cached);
        return;
    }

    /* 3. Generate thumbnail asynchronously if possible */
    if (!content_type) return;
    if (gnome_desktop_thumbnail_factory_has_valid_failed_thumbnail(
            factory, uri, mtime))
        return;
    if (!gnome_desktop_thumbnail_factory_can_thumbnail(
            factory, uri, content_type, mtime))
        return;

    /* Tag the image with the URI so callback can verify */
    g_object_set_data_full(G_OBJECT(image), "thumb-uri",
                           g_strdup(uri), g_free);

    ThumbGenData *tgd = g_new0(ThumbGenData, 1);
    tgd->image = GTK_WIDGET(image);
    tgd->uri = g_strdup(uri);
    tgd->mtime = mtime;
    tgd->pixel_size = pixel_size;
    g_object_ref(tgd->image);

    gnome_desktop_thumbnail_factory_generate_thumbnail_async(
        factory, uri, content_type, NULL, on_thumb_generated, tgd);
}

/* ── Explorer state ───────────────────────────────────────────────── */

typedef struct {
    GtkWidget           *box;
    GtkWidget           *title_label;
    GtkWidget           *path_entry;
    GtkWidget           *path_stack;
    GtkWidget           *path_crumbs;
    GtkWidget           *left_listbox;
    GtkWidget           *right_list_view;
    GtkWidget           *grid_view;
    GtkWidget           *left_scrolled;
    GtkWidget           *right_stack;
    GtkDirectoryList    *right_dir_list;
    GtkCustomFilter     *right_filter;
    GtkCustomSorter     *right_sorter;
    char                *current_path;
    char                *right_path;
    /* View settings */
    gboolean             grid_mode;
    int                  grid_zoom;      /* index into GRID_ZOOM_LEVELS */
    int                  list_zoom;      /* index into LIST_ZOOM_LEVELS */
    int                  sort_by;        /* 0=name, 1=size, 2=date, 3=type */
    gboolean             sort_reversed;
    gboolean             show_hidden;
} ExplorerData;

static void
explorer_data_free(gpointer data)
{
    ExplorerData *ed = data;
    g_free(ed->current_path);
    g_free(ed->right_path);
    g_free(ed);
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static void
refresh_sort(ExplorerData *ed)
{
    gtk_sorter_changed(GTK_SORTER(ed->right_sorter), GTK_SORTER_CHANGE_DIFFERENT);
}

static void
refresh_filter(ExplorerData *ed)
{
    gtk_filter_changed(GTK_FILTER(ed->right_filter), GTK_FILTER_CHANGE_DIFFERENT);
}

/* Force list/grid views to re-bind all items (for zoom changes) */
static void
refresh_views(ExplorerData *ed)
{
    GtkSelectionModel *lm = gtk_list_view_get_model(GTK_LIST_VIEW(ed->right_list_view));
    if (lm) {
        g_object_ref(lm);
        gtk_list_view_set_model(GTK_LIST_VIEW(ed->right_list_view), NULL);
        gtk_list_view_set_model(GTK_LIST_VIEW(ed->right_list_view), lm);
        g_object_unref(lm);
    }
    GtkSelectionModel *gm = gtk_grid_view_get_model(GTK_GRID_VIEW(ed->grid_view));
    if (gm) {
        g_object_ref(gm);
        gtk_grid_view_set_model(GTK_GRID_VIEW(ed->grid_view), NULL);
        gtk_grid_view_set_model(GTK_GRID_VIEW(ed->grid_view), gm);
        g_object_unref(gm);
    }
}

/* ── Path bar (Nautilus-style breadcrumb + edit) ───────────────────── */

static void navigate_to(ExplorerData *ed, const char *path);
static void rebuild_breadcrumbs(ExplorerData *ed);
static void path_bar_enter_edit(ExplorerData *ed, int cursor_pos);
static void path_bar_leave_edit(ExplorerData *ed);

static void
on_crumb_pressed(GtkGestureClick *gesture, int n_press, double x, double y,
                 gpointer data)
{
    (void)n_press; (void)y;
    ExplorerData *ed = data;

    GtkWidget *btn = gtk_event_controller_get_widget(
        GTK_EVENT_CONTROLLER(gesture));
    const char *crumb_path = g_object_get_data(G_OBJECT(btn), "crumb-path");
    if (!crumb_path) return;

    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    if (g_strcmp0(crumb_path, ed->current_path) == 0) {
        /* Already showing this directory → enter edit mode with cursor */
        /* Find where this segment's text starts in the full path */
        const char *seg_text = strrchr(crumb_path, '/');
        int seg_start;
        if (seg_text && seg_text[1] != '\0') {
            seg_start = (int)(seg_text - crumb_path + 1);
            seg_text++;
        } else {
            /* root "/" */
            seg_start = 0;
            seg_text = crumb_path;
        }
        int seg_len = (int)strlen(seg_text);

        /* Approximate character position from click x within button */
        int btn_width = gtk_widget_get_width(btn);
        int char_offset = 0;
        if (btn_width > 0 && seg_len > 0)
            char_offset = (int)((x / btn_width) * seg_len + 0.5);
        if (char_offset < 0) char_offset = 0;
        if (char_offset > seg_len) char_offset = seg_len;

        int cursor_pos = seg_start + char_offset;
        if (cursor_pos > (int)strlen(ed->current_path))
            cursor_pos = (int)strlen(ed->current_path);

        path_bar_enter_edit(ed, cursor_pos);
    } else {
        /* Different directory → navigate there */
        navigate_to(ed, crumb_path);
    }
}

static void
on_crumbs_empty_click(GtkGestureClick *gesture, int n_press, double x, double y,
                      gpointer data)
{
    (void)n_press; (void)x; (void)y;
    ExplorerData *ed = data;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    path_bar_enter_edit(ed, -1);
}

static void
path_bar_enter_edit(ExplorerData *ed, int cursor_pos)
{
    gtk_editable_set_text(GTK_EDITABLE(ed->path_entry), ed->current_path);
    gtk_stack_set_visible_child_name(GTK_STACK(ed->path_stack), "entry");
    gtk_widget_grab_focus(ed->path_entry);
    gtk_editable_set_position(GTK_EDITABLE(ed->path_entry),
                              cursor_pos >= 0 ? cursor_pos : -1);
}

static void
path_bar_leave_edit(ExplorerData *ed)
{
    gtk_stack_set_visible_child_name(GTK_STACK(ed->path_stack), "crumbs");
}

static gboolean
on_path_entry_key(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                  GdkModifierType state, gpointer data)
{
    (void)ctrl; (void)keycode; (void)state;
    ExplorerData *ed = data;
    if (keyval == GDK_KEY_Escape) {
        path_bar_leave_edit(ed);
        return TRUE;
    }
    return FALSE;
}

static void
on_path_focus_out(GtkEventControllerFocus *ctrl, gpointer data)
{
    (void)ctrl;
    ExplorerData *ed = data;
    path_bar_leave_edit(ed);
}

static gboolean
scroll_crumbs_idle(gpointer data)
{
    GtkWidget *crumbs = GTK_WIDGET(data);
    GtkWidget *sw = gtk_widget_get_parent(crumbs);
    if (sw && GTK_IS_SCROLLED_WINDOW(sw)) {
        GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment(
            GTK_SCROLLED_WINDOW(sw));
        double upper = gtk_adjustment_get_upper(adj);
        double page = gtk_adjustment_get_page_size(adj);
        gtk_adjustment_set_value(adj, upper - page);
    }
    return G_SOURCE_REMOVE;
}

static void
rebuild_breadcrumbs(ExplorerData *ed)
{
    /* Remove old children */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(ed->path_crumbs)))
        gtk_box_remove(GTK_BOX(ed->path_crumbs), child);

    const char *path = ed->current_path;
    if (!path || path[0] != '/') return;

    /* Build segment arrays */
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(names, g_strdup("/"));
    g_ptr_array_add(paths, g_strdup("/"));

    if (strlen(path) > 1) {
        gchar **parts = g_strsplit(path + 1, "/", -1);
        GString *cum = g_string_new("");
        for (int i = 0; parts[i]; i++) {
            if (parts[i][0] == '\0') continue;
            g_string_append_c(cum, '/');
            g_string_append(cum, parts[i]);
            g_ptr_array_add(names, g_strdup(parts[i]));
            g_ptr_array_add(paths, g_strdup(cum->str));
        }
        g_string_free(cum, TRUE);
        g_strfreev(parts);
    }

    for (guint i = 0; i < names->len; i++) {
        if (i > 0) {
            GtkWidget *sep = gtk_label_new("\u203a"); /* › */
            gtk_widget_add_css_class(sep, "dim-label");
            gtk_box_append(GTK_BOX(ed->path_crumbs), sep);
        }

        const char *name = g_ptr_array_index(names, i);
        GtkWidget *btn = gtk_button_new_with_label(name);
        gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
        g_object_set_data_full(G_OBJECT(btn), "crumb-path",
                               g_strdup(g_ptr_array_index(paths, i)),
                               g_free);

        /* All segments: if path == current dir → edit mode, else navigate */
        GtkGesture *click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
        g_signal_connect(click, "pressed",
                         G_CALLBACK(on_crumb_pressed), ed);
        gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(click));

        gtk_box_append(GTK_BOX(ed->path_crumbs), btn);
    }

    g_ptr_array_free(names, TRUE);
    g_ptr_array_free(paths, TRUE);

    /* Scroll to show the last (current) segment */
    g_idle_add(scroll_crumbs_idle, ed->path_crumbs);
}

/* ── Navigation ───────────────────────────────────────────────────── */

static void
navigate_to(ExplorerData *ed, const char *path)
{
    g_free(ed->current_path);
    ed->current_path = g_strdup(path);

    g_free(ed->right_path);
    ed->right_path = g_strdup(path);

    g_autoptr(GFile) file = g_file_new_for_path(path);
    gtk_directory_list_set_file(ed->right_dir_list, file);

    rebuild_breadcrumbs(ed);
    if (ed->path_stack)
        gtk_stack_set_visible_child_name(GTK_STACK(ed->path_stack), "crumbs");

    g_autofree char *basename = g_path_get_basename(path);
    gtk_label_set_text(GTK_LABEL(ed->title_label), basename);
}

static void on_go_up(GtkButton *btn, gpointer data)
{
    (void)btn;
    ExplorerData *ed = data;
    g_autofree char *parent = g_path_get_dirname(ed->current_path);
    if (parent && g_strcmp0(parent, ed->current_path) != 0)
        navigate_to(ed, parent);
}

static void on_go_home(GtkButton *btn, gpointer data)
{
    (void)btn;
    navigate_to((ExplorerData *)data, g_get_home_dir());
}

static void on_close_explorer(GtkButton *btn, gpointer data)
{
    (void)btn;
    g_signal_emit_by_name(((ExplorerData *)data)->box, "terminal-exited");
}

static void
on_path_activate(GtkEntry *entry, gpointer data)
{
    (void)entry;
    ExplorerData *ed = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(ed->path_entry));
    if (!text || text[0] == '\0') {
        path_bar_leave_edit(ed);
        return;
    }

    g_autofree char *expanded = NULL;
    if (text[0] == '~') {
        expanded = g_build_filename(g_get_home_dir(), text + 1, NULL);
        text = expanded;
    }

    if (g_file_test(text, G_FILE_TEST_IS_DIR))
        navigate_to(ed, text);
    else
        path_bar_leave_edit(ed);
}

/* ── Sidebar toggle ──────────────────────────────────────────────── */

static void
on_toggle_sidebar(GtkToggleButton *btn, gpointer data)
{
    ExplorerData *ed = data;
    gtk_widget_set_visible(ed->left_scrolled, gtk_toggle_button_get_active(btn));
}

/* ── Sidebar row activation ──────────────────────────────────────── */

static void
on_sidebar_row_activated(GtkListBox *listbox, GtkListBoxRow *row, gpointer data)
{
    (void)listbox;
    ExplorerData *ed = data;
    const char *path = g_object_get_data(G_OBJECT(row), "path");
    if (path && g_file_test(path, G_FILE_TEST_IS_DIR))
        navigate_to(ed, path);
}

/* ── View mode toggle ────────────────────────────────────────────── */

static void
on_view_toggle(GtkToggleButton *btn, gpointer data)
{
    ExplorerData *ed = data;
    gboolean is_grid = gtk_toggle_button_get_active(btn);
    ed->grid_mode = is_grid;

    gtk_stack_set_visible_child_name(GTK_STACK(ed->right_stack),
                                     is_grid ? "grid" : "list");
    refresh_sort(ed);
}

/* ── Sort callbacks ──────────────────────────────────────────────── */

static void
on_sort_name(GtkCheckButton *btn, gpointer data)
{
    if (!gtk_check_button_get_active(btn)) return;
    ExplorerData *ed = data;
    ed->sort_by = 0;
    refresh_sort(ed);
}

static void
on_sort_size(GtkCheckButton *btn, gpointer data)
{
    if (!gtk_check_button_get_active(btn)) return;
    ExplorerData *ed = data;
    ed->sort_by = 1;
    refresh_sort(ed);
}

static void
on_sort_date(GtkCheckButton *btn, gpointer data)
{
    if (!gtk_check_button_get_active(btn)) return;
    ExplorerData *ed = data;
    ed->sort_by = 2;
    refresh_sort(ed);
}

static void
on_sort_type(GtkCheckButton *btn, gpointer data)
{
    if (!gtk_check_button_get_active(btn)) return;
    ExplorerData *ed = data;
    ed->sort_by = 3;
    refresh_sort(ed);
}

static void
on_sort_reversed(GtkCheckButton *btn, gpointer data)
{
    ExplorerData *ed = data;
    ed->sort_reversed = gtk_check_button_get_active(btn);
    refresh_sort(ed);
}

/* ── Hamburger menu callbacks ────────────────────────────────────── */

static void
on_show_hidden_toggled(GtkCheckButton *btn, gpointer data)
{
    ExplorerData *ed = data;
    ed->show_hidden = gtk_check_button_get_active(btn);
    refresh_filter(ed);
}

static void
on_zoom_in(GtkButton *btn, gpointer data)
{
    (void)btn;
    ExplorerData *ed = data;
    if (ed->grid_mode) {
        if (ed->grid_zoom < GRID_ZOOM_COUNT - 1) ed->grid_zoom++;
    } else {
        if (ed->list_zoom < LIST_ZOOM_COUNT - 1) ed->list_zoom++;
    }
    refresh_views(ed);
}

static void
on_zoom_out(GtkButton *btn, gpointer data)
{
    (void)btn;
    ExplorerData *ed = data;
    if (ed->grid_mode) {
        if (ed->grid_zoom > 0) ed->grid_zoom--;
    } else {
        if (ed->list_zoom > 0) ed->list_zoom--;
    }
    refresh_views(ed);
}

/* ── Ctrl+Scroll zoom on right pane ──────────────────────────────── */

static gboolean
on_scroll_zoom(GtkEventControllerScroll *ctrl, double dx, double dy, gpointer data)
{
    (void)dx;
    GdkModifierType state = gtk_event_controller_get_current_event_state(
        GTK_EVENT_CONTROLLER(ctrl));
    if (!(state & GDK_CONTROL_MASK))
        return GDK_EVENT_PROPAGATE;

    ExplorerData *ed = data;
    if (dy > 0) {
        /* scroll down (wheel forward) → zoom out (reduce) */
        if (ed->grid_mode) {
            if (ed->grid_zoom > 0) ed->grid_zoom--;
        } else {
            if (ed->list_zoom > 0) ed->list_zoom--;
        }
    } else if (dy < 0) {
        /* scroll up (wheel backward) → zoom in (increase) */
        if (ed->grid_mode) {
            if (ed->grid_zoom < GRID_ZOOM_COUNT - 1) ed->grid_zoom++;
        } else {
            if (ed->list_zoom < LIST_ZOOM_COUNT - 1) ed->list_zoom++;
        }
    }
    refresh_views(ed);
    return GDK_EVENT_STOP;
}

/* ── List item factories ──────────────────────────────────────────── */

/* Right pane — List view (icon + name + size + date) */

static void
setup_item_list(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
    (void)factory; (void)data;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);
    gtk_widget_set_margin_top(box, 2);
    gtk_widget_set_margin_bottom(box, 2);

    GtkWidget *icon = gtk_image_new();
    GtkWidget *name = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(name), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(name, TRUE);

    GtkWidget *size_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(size_label), 1.0);
    gtk_widget_set_size_request(size_label, 70, -1);
    gtk_widget_add_css_class(size_label, "dim-label");

    GtkWidget *date_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(date_label), 1.0);
    gtk_widget_set_size_request(date_label, 120, -1);
    gtk_widget_add_css_class(date_label, "dim-label");

    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), name);
    gtk_box_append(GTK_BOX(box), size_label);
    gtk_box_append(GTK_BOX(box), date_label);
    gtk_list_item_set_child(item, box);
}

static void
bind_item_list(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
    (void)factory;
    ExplorerData *ed = data;
    GFileInfo *info = G_FILE_INFO(gtk_list_item_get_item(item));
    if (!info) return;

    GtkWidget *box = gtk_list_item_get_child(item);
    GtkWidget *icon = gtk_widget_get_first_child(box);
    GtkWidget *name = gtk_widget_get_next_sibling(icon);
    GtkWidget *size_label = gtk_widget_get_next_sibling(name);
    GtkWidget *date_label = gtk_widget_get_next_sibling(size_label);

    int sz = LIST_ZOOM_LEVELS[ed->list_zoom];
    gtk_image_set_pixel_size(GTK_IMAGE(icon), sz);

    /* Clear any pending thumbnail URI from previous bind */
    g_object_set_data(G_OBJECT(icon), "thumb-uri", NULL);

    gboolean is_dir = g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;

    /* Set generic icon first, then try thumbnail for files */
    GIcon *gicon = g_file_info_get_icon(info);
    if (gicon) gtk_image_set_from_gicon(GTK_IMAGE(icon), gicon);

    if (!is_dir)
        load_thumbnail(GTK_IMAGE(icon), info, ed->right_path, sz);

    gtk_label_set_text(GTK_LABEL(name), g_file_info_get_display_name(info));

    if (is_dir) {
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        gtk_label_set_attributes(GTK_LABEL(name), attrs);
        pango_attr_list_unref(attrs);
    } else {
        gtk_label_set_attributes(GTK_LABEL(name), NULL);
    }

    g_autofree char *size_str = g_format_size(g_file_info_get_size(info));
    gtk_label_set_text(GTK_LABEL(size_label), size_str);

    GDateTime *dt = g_file_info_get_modification_date_time(info);
    if (dt) {
        g_autofree char *date_str = g_date_time_format(dt, "%Y-%m-%d %H:%M");
        gtk_label_set_text(GTK_LABEL(date_label), date_str);
        g_date_time_unref(dt);
    } else {
        gtk_label_set_text(GTK_LABEL(date_label), "");
    }
}

/* Right pane — Grid view (large icon + name below) */

static void
setup_item_grid(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
    (void)factory; (void)data;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);

    GtkWidget *icon = gtk_image_new();

    GtkWidget *name = gtk_label_new(NULL);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(name), 14);
    gtk_label_set_lines(GTK_LABEL(name), 2);
    gtk_label_set_wrap(GTK_LABEL(name), TRUE);
    gtk_label_set_justify(GTK_LABEL(name), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(name), 0.5);

    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), name);
    gtk_list_item_set_child(item, box);
}

static void
bind_item_grid(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer data)
{
    (void)factory;
    ExplorerData *ed = data;
    GFileInfo *info = G_FILE_INFO(gtk_list_item_get_item(item));
    if (!info) return;

    GtkWidget *box = gtk_list_item_get_child(item);
    GtkWidget *icon = gtk_widget_get_first_child(box);
    GtkWidget *name = gtk_widget_get_next_sibling(icon);

    int sz = GRID_ZOOM_LEVELS[ed->grid_zoom];
    gtk_image_set_pixel_size(GTK_IMAGE(icon), sz);

    /* Clear any pending thumbnail URI from previous bind */
    g_object_set_data(G_OBJECT(icon), "thumb-uri", NULL);

    gboolean is_dir = g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;

    /* Set generic icon first, then try thumbnail for files */
    GIcon *gicon = g_file_info_get_icon(info);
    if (gicon) gtk_image_set_from_gicon(GTK_IMAGE(icon), gicon);

    if (!is_dir)
        load_thumbnail(GTK_IMAGE(icon), info, ed->right_path, sz);

    gtk_label_set_text(GTK_LABEL(name), g_file_info_get_display_name(info));

    if (is_dir) {
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        gtk_label_set_attributes(GTK_LABEL(name), attrs);
        pango_attr_list_unref(attrs);
    } else {
        gtk_label_set_attributes(GTK_LABEL(name), NULL);
    }
}

/* ── Sorting and filtering ────────────────────────────────────────── */

static int
file_sort_fn(gconstpointer a, gconstpointer b, gpointer data)
{
    ExplorerData *ed = data;
    GFileInfo *fa = G_FILE_INFO((gpointer)a);
    GFileInfo *fb = G_FILE_INFO((gpointer)b);

    gboolean da = g_file_info_get_file_type(fa) == G_FILE_TYPE_DIRECTORY;
    gboolean db = g_file_info_get_file_type(fb) == G_FILE_TYPE_DIRECTORY;
    if (da != db) return da ? -1 : 1;

    int result = 0;
    switch (ed->sort_by) {
    case 1: {
        goffset sa = g_file_info_get_size(fa);
        goffset sb = g_file_info_get_size(fb);
        result = (sa > sb) - (sa < sb);
        break;
    }
    case 2: {
        GDateTime *ta = g_file_info_get_modification_date_time(fa);
        GDateTime *tb = g_file_info_get_modification_date_time(fb);
        if (ta && tb) result = g_date_time_compare(ta, tb);
        if (ta) g_date_time_unref(ta);
        if (tb) g_date_time_unref(tb);
        break;
    }
    case 3: {
        const char *na = g_file_info_get_name(fa);
        const char *nb = g_file_info_get_name(fb);
        const char *ea = strrchr(na, '.');
        const char *eb = strrchr(nb, '.');
        if (ea && eb) result = g_utf8_collate(ea, eb);
        else if (ea) result = 1;
        else if (eb) result = -1;
        break;
    }
    default: break;
    }

    if (result == 0)
        result = g_utf8_collate(g_file_info_get_display_name(fa),
                                g_file_info_get_display_name(fb));

    return ed->sort_reversed ? -result : result;
}

static gboolean
filter_right(gpointer item, gpointer data)
{
    ExplorerData *ed = data;
    if (!ed->show_hidden && g_file_info_get_is_hidden(G_FILE_INFO(item)))
        return FALSE;
    return TRUE;
}

/* ── Item activation ──────────────────────────────────────────────── */

static void
on_file_launch_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void)data;
    gtk_file_launcher_launch_finish(GTK_FILE_LAUNCHER(source), result, NULL);
    g_object_unref(source);
}

static void
activate_right_item(ExplorerData *ed, GtkSelectionModel *model, guint position)
{
    GObject *obj = g_list_model_get_item(G_LIST_MODEL(model), position);
    if (!obj) return;

    GFileInfo *info = G_FILE_INFO(obj);
    GFileType type = g_file_info_get_file_type(info);
    const char *name = g_file_info_get_name(info);

    if (type == G_FILE_TYPE_DIRECTORY) {
        g_autofree char *new_path = g_build_filename(ed->right_path, name, NULL);
        navigate_to(ed, new_path);
    } else {
        g_autofree char *file_path = g_build_filename(ed->right_path, name, NULL);
        g_autoptr(GFile) file = g_file_new_for_path(file_path);
        GtkFileLauncher *launcher = gtk_file_launcher_new(file);
        GtkWidget *win = gtk_widget_get_ancestor(ed->box, GTK_TYPE_WINDOW);
        gtk_file_launcher_launch(launcher, win ? GTK_WINDOW(win) : NULL, NULL,
                                 on_file_launch_done, NULL);
    }
    g_object_unref(obj);
}

static void
on_list_item_activated(GtkListView *lv, guint pos, gpointer data)
{
    activate_right_item(data, gtk_list_view_get_model(lv), pos);
}

static void
on_grid_item_activated(GtkGridView *gv, guint pos, gpointer data)
{
    activate_right_item(data, gtk_grid_view_get_model(gv), pos);
}

/* ── Sidebar (Nautilus-style places panel) ────────────────────────── */

static GtkWidget *
sidebar_row_new(const char *icon_name, const char *label, const char *path)
{
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(hbox, 8);
    gtk_widget_set_margin_end(hbox, 8);
    gtk_widget_set_margin_top(hbox, 4);
    gtk_widget_set_margin_bottom(hbox, 4);

    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
    gtk_box_append(GTK_BOX(hbox), icon);

    GtkWidget *lbl = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_box_append(GTK_BOX(hbox), lbl);

    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), hbox);
    g_object_set_data_full(G_OBJECT(row), "path", g_strdup(path), g_free);

    return row;
}

static void
build_sidebar(GtkListBox *listbox)
{
    /* ── Standard locations ──────────────────────────────────────── */
    const char *home = g_get_home_dir();
    gtk_list_box_append(listbox, sidebar_row_new(
        "user-home-symbolic", _("Home"), home));

    struct { GUserDirectory dir; const char *icon; const char *label; } xdg[] = {
        { G_USER_DIRECTORY_DESKTOP,     "user-desktop-symbolic",       "Desktop"   },
        { G_USER_DIRECTORY_DOCUMENTS,   "folder-documents-symbolic",   "Documents" },
        { G_USER_DIRECTORY_DOWNLOAD,    "folder-download-symbolic",    "Downloads" },
        { G_USER_DIRECTORY_MUSIC,       "folder-music-symbolic",       "Music"     },
        { G_USER_DIRECTORY_PICTURES,    "folder-pictures-symbolic",    "Pictures"  },
        { G_USER_DIRECTORY_VIDEOS,      "folder-videos-symbolic",      "Videos"    },
    };

    for (size_t i = 0; i < G_N_ELEMENTS(xdg); i++) {
        const char *path = g_get_user_special_dir(xdg[i].dir);
        if (path && g_file_test(path, G_FILE_TEST_IS_DIR) &&
            g_strcmp0(path, home) != 0) {
            g_autofree char *basename = g_path_get_basename(path);
            gtk_list_box_append(listbox, sidebar_row_new(
                xdg[i].icon, basename, path));
        }
    }

    /* ── Separator ──────────────────────────────────────────────── */
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *sep1_row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(sep1_row), sep1);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(sep1_row), FALSE);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(sep1_row), FALSE);
    gtk_list_box_append(listbox, sep1_row);

    /* ── User bookmarks from ~/.config/gtk-3.0/bookmarks ─────────── */
    g_autofree char *bm_path = g_build_filename(
        g_get_user_config_dir(), "gtk-3.0", "bookmarks", NULL);
    g_autofree char *contents = NULL;
    if (g_file_get_contents(bm_path, &contents, NULL, NULL) && contents) {
        g_auto(GStrv) lines = g_strsplit(contents, "\n", -1);
        for (int i = 0; lines[i] && lines[i][0]; i++) {
            /* Format: URI [optional display name] */
            char *space = strchr(lines[i], ' ');
            char *display = NULL;
            if (space) {
                *space = '\0';
                display = g_strstrip(space + 1);
                if (display[0] == '\0') display = NULL;
            }

            g_autoptr(GFile) file = g_file_new_for_uri(lines[i]);
            g_autofree char *fpath = g_file_get_path(file);
            if (!fpath || !g_file_test(fpath, G_FILE_TEST_IS_DIR))
                continue;

            const char *label = display;
            g_autofree char *basename = NULL;
            if (!label) {
                basename = g_path_get_basename(fpath);
                label = basename;
            }

            gtk_list_box_append(listbox, sidebar_row_new(
                "folder-symbolic", label, fpath));
        }
    }

    /* ── Separator + system locations ────────────────────────────── */
    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *sep2_row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(sep2_row), sep2);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(sep2_row), FALSE);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(sep2_row), FALSE);
    gtk_list_box_append(listbox, sep2_row);

    /* Trash */
    g_autofree char *trash_path = g_build_filename(
        g_get_user_data_dir(), "Trash", "files", NULL);
    if (g_file_test(trash_path, G_FILE_TEST_IS_DIR))
        gtk_list_box_append(listbox, sidebar_row_new(
            "user-trash-symbolic", _("Trash"), trash_path));

    /* Root filesystem */
    gtk_list_box_append(listbox, sidebar_row_new(
        "drive-harddisk-symbolic", _("Computer"), "/"));
}

/* ── Sort popover (attached to view split-button dropdown) ────────── */

static GtkWidget *
create_sort_popover(ExplorerData *ed)
{
    GtkWidget *popover = gtk_popover_new();
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(vbox, 10);
    gtk_widget_set_margin_end(vbox, 10);
    gtk_widget_set_margin_top(vbox, 10);
    gtk_widget_set_margin_bottom(vbox, 10);

    GtkWidget *header = gtk_label_new(_("Sort"));
    gtk_label_set_xalign(GTK_LABEL(header), 0.0);
    gtk_widget_add_css_class(header, "heading");
    gtk_box_append(GTK_BOX(vbox), header);

    GtkWidget *name_r = gtk_check_button_new_with_label(_("Name"));
    GtkWidget *size_r = gtk_check_button_new_with_label(_("Size"));
    GtkWidget *date_r = gtk_check_button_new_with_label(_("Last Modified"));
    GtkWidget *type_r = gtk_check_button_new_with_label(_("Type"));

    gtk_check_button_set_group(GTK_CHECK_BUTTON(size_r), GTK_CHECK_BUTTON(name_r));
    gtk_check_button_set_group(GTK_CHECK_BUTTON(date_r), GTK_CHECK_BUTTON(name_r));
    gtk_check_button_set_group(GTK_CHECK_BUTTON(type_r), GTK_CHECK_BUTTON(name_r));

    switch (ed->sort_by) {
    case 1: gtk_check_button_set_active(GTK_CHECK_BUTTON(size_r), TRUE); break;
    case 2: gtk_check_button_set_active(GTK_CHECK_BUTTON(date_r), TRUE); break;
    case 3: gtk_check_button_set_active(GTK_CHECK_BUTTON(type_r), TRUE); break;
    default: gtk_check_button_set_active(GTK_CHECK_BUTTON(name_r), TRUE); break;
    }

    g_signal_connect(name_r, "toggled", G_CALLBACK(on_sort_name), ed);
    g_signal_connect(size_r, "toggled", G_CALLBACK(on_sort_size), ed);
    g_signal_connect(date_r, "toggled", G_CALLBACK(on_sort_date), ed);
    g_signal_connect(type_r, "toggled", G_CALLBACK(on_sort_type), ed);

    gtk_box_append(GTK_BOX(vbox), name_r);
    gtk_box_append(GTK_BOX(vbox), size_r);
    gtk_box_append(GTK_BOX(vbox), date_r);
    gtk_box_append(GTK_BOX(vbox), type_r);

    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    GtkWidget *rev = gtk_check_button_new_with_label(_("Reversed"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(rev), ed->sort_reversed);
    g_signal_connect(rev, "toggled", G_CALLBACK(on_sort_reversed), ed);
    gtk_box_append(GTK_BOX(vbox), rev);

    gtk_popover_set_child(GTK_POPOVER(popover), vbox);
    return popover;
}

/* ── Hamburger menu ──────────────────────────────────────────────── */

static GtkWidget *
create_menu_popover(ExplorerData *ed)
{
    GtkWidget *popover = gtk_popover_new();
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(vbox, 10);
    gtk_widget_set_margin_end(vbox, 10);
    gtk_widget_set_margin_top(vbox, 10);
    gtk_widget_set_margin_bottom(vbox, 10);

    /* Zoom in/out row */
    GtkWidget *zoom_header = gtk_label_new(_("Icon Size"));
    gtk_label_set_xalign(GTK_LABEL(zoom_header), 0.0);
    gtk_widget_add_css_class(zoom_header, "heading");
    gtk_box_append(GTK_BOX(vbox), zoom_header);

    GtkWidget *zoom_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(zoom_row, GTK_ALIGN_CENTER);

    GtkWidget *zoom_out = gtk_button_new_from_icon_name("zoom-out-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(zoom_out), FALSE);
    g_signal_connect(zoom_out, "clicked", G_CALLBACK(on_zoom_out), ed);

    GtkWidget *zoom_in = gtk_button_new_from_icon_name("zoom-in-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(zoom_in), FALSE);
    g_signal_connect(zoom_in, "clicked", G_CALLBACK(on_zoom_in), ed);

    gtk_box_append(GTK_BOX(zoom_row), zoom_out);
    gtk_box_append(GTK_BOX(zoom_row), zoom_in);
    gtk_box_append(GTK_BOX(vbox), zoom_row);

    gtk_box_append(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Show hidden files */
    GtkWidget *hidden = gtk_check_button_new_with_label(_("Show Hidden Files"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(hidden), ed->show_hidden);
    g_signal_connect(hidden, "toggled", G_CALLBACK(on_show_hidden_toggled), ed);
    gtk_box_append(GTK_BOX(vbox), hidden);

    gtk_popover_set_child(GTK_POPOVER(popover), vbox);
    return popover;
}

/* ── Public API ───────────────────────────────────────────────────── */

GtkWidget *
kterm_explorer_new(const char *path)
{
    ExplorerData *ed = g_new0(ExplorerData, 1);
    ed->current_path = g_strdup(path ? path : g_get_home_dir());
    ed->right_path   = g_strdup(ed->current_path);
    ed->grid_mode    = FALSE;
    ed->grid_zoom    = GRID_ZOOM_DEFAULT;
    ed->list_zoom    = LIST_ZOOM_DEFAULT;
    ed->sort_by      = 0;
    ed->sort_reversed = FALSE;
    ed->show_hidden  = FALSE;

    /* Ensure signal exists on GtkBox */
    if (g_signal_lookup("terminal-exited", GTK_TYPE_BOX) == 0) {
        g_signal_new("terminal-exited", GTK_TYPE_BOX, G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    }

    /* ── Outer container ─────────────────────────────────────────── */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_vexpand(box, TRUE);
    ed->box = box;

    /* Title bar: horizontal box with label + close button */
    GtkWidget *title_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(title_bar, "kterm-title-bar");

    GtkWidget *title_label = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
    gtk_widget_set_hexpand(title_label, TRUE);
    gtk_box_append(GTK_BOX(title_bar), title_label);

    GtkWidget *title_close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(title_close_btn), FALSE);
    gtk_widget_set_tooltip_text(title_close_btn, _("Close"));
    gtk_widget_set_valign(title_close_btn, GTK_ALIGN_CENTER);
    g_signal_connect(title_close_btn, "clicked", G_CALLBACK(on_close_explorer), ed);
    gtk_box_append(GTK_BOX(title_bar), title_close_btn);

    gtk_box_append(GTK_BOX(box), title_bar);
    ed->title_label = title_label;

    g_object_set_data(G_OBJECT(box), "kterm-title-label", title_label);
    g_object_set_data(G_OBJECT(box), "kterm-explorer", GINT_TO_POINTER(TRUE));

    /* ── Toolbar ─────────────────────────────────────────────────── */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_start(toolbar, 4);
    gtk_widget_set_margin_end(toolbar, 4);
    gtk_widget_set_margin_top(toolbar, 2);
    gtk_widget_set_margin_bottom(toolbar, 2);

    /* Sidebar toggle */
    GtkWidget *sidebar_btn = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(sidebar_btn), "sidebar-show-symbolic");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sidebar_btn), TRUE);
    gtk_button_set_has_frame(GTK_BUTTON(sidebar_btn), FALSE);
    gtk_widget_set_tooltip_text(sidebar_btn, _("Folders"));
    g_signal_connect(sidebar_btn, "toggled", G_CALLBACK(on_toggle_sidebar), ed);
    gtk_box_append(GTK_BOX(toolbar), sidebar_btn);

    /* Up / Home */
    GtkWidget *up_btn = gtk_button_new_from_icon_name("go-up-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(up_btn), FALSE);
    gtk_widget_set_tooltip_text(up_btn, _("Parent directory"));
    g_signal_connect(up_btn, "clicked", G_CALLBACK(on_go_up), ed);
    gtk_box_append(GTK_BOX(toolbar), up_btn);

    GtkWidget *home_btn = gtk_button_new_from_icon_name("go-home-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(home_btn), FALSE);
    gtk_widget_set_tooltip_text(home_btn, _("Home"));
    g_signal_connect(home_btn, "clicked", G_CALLBACK(on_go_home), ed);
    gtk_box_append(GTK_BOX(toolbar), home_btn);

    /* Path bar: stack with breadcrumbs + text entry */
    GtkWidget *path_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(path_stack),
                                  GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_hexpand(path_stack, TRUE);
    ed->path_stack = path_stack;

    /* Breadcrumbs */
    GtkWidget *path_crumbs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    ed->path_crumbs = path_crumbs;

    GtkGesture *crumb_bg_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(crumb_bg_click), 1);
    g_signal_connect(crumb_bg_click, "pressed",
                     G_CALLBACK(on_crumbs_empty_click), ed);
    gtk_widget_add_controller(path_crumbs,
                              GTK_EVENT_CONTROLLER(crumb_bg_click));

    GtkWidget *crumb_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(crumb_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(crumb_scroll),
                                  path_crumbs);
    gtk_stack_add_named(GTK_STACK(path_stack), crumb_scroll, "crumbs");

    /* Text entry (edit mode) */
    GtkWidget *path_entry = gtk_entry_new();
    gtk_entry_set_has_frame(GTK_ENTRY(path_entry), FALSE);
    g_signal_connect(path_entry, "activate", G_CALLBACK(on_path_activate), ed);
    ed->path_entry = path_entry;

    GtkEventController *key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed",
                     G_CALLBACK(on_path_entry_key), ed);
    gtk_widget_add_controller(path_entry, key_ctrl);

    GtkEventController *focus_ctrl = gtk_event_controller_focus_new();
    g_signal_connect(focus_ctrl, "leave",
                     G_CALLBACK(on_path_focus_out), ed);
    gtk_widget_add_controller(path_entry, focus_ctrl);

    gtk_stack_add_named(GTK_STACK(path_stack), path_entry, "entry");
    gtk_stack_set_visible_child_name(GTK_STACK(path_stack), "crumbs");

    gtk_box_append(GTK_BOX(toolbar), path_stack);

    /* Build initial breadcrumbs (after dir_list is set up — deferred) */

    /* Grid/List view toggle */
    GtkWidget *list_btn = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(list_btn), "view-list-symbolic");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(list_btn), TRUE);
    gtk_button_set_has_frame(GTK_BUTTON(list_btn), FALSE);

    GtkWidget *grid_btn = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(grid_btn), "view-grid-symbolic");
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(grid_btn),
                                GTK_TOGGLE_BUTTON(list_btn));
    gtk_button_set_has_frame(GTK_BUTTON(grid_btn), FALSE);
    g_signal_connect(grid_btn, "toggled", G_CALLBACK(on_view_toggle), ed);

    gtk_box_append(GTK_BOX(toolbar), list_btn);
    gtk_box_append(GTK_BOX(toolbar), grid_btn);

    /* Sort menu button */
    GtkWidget *sort_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(sort_btn),
                                  "view-sort-descending-symbolic");
    gtk_widget_add_css_class(sort_btn, "flat");
    gtk_widget_set_tooltip_text(sort_btn, _("Sort"));
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(sort_btn),
                                create_sort_popover(ed));
    gtk_box_append(GTK_BOX(toolbar), sort_btn);

    /* Hamburger menu button */
    GtkWidget *menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn),
                                  "open-menu-symbolic");
    gtk_widget_add_css_class(menu_btn, "flat");
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_btn),
                                create_menu_popover(ed));
    gtk_box_append(GTK_BOX(toolbar), menu_btn);

    gtk_box_append(GTK_BOX(box), toolbar);

    /* ── Left pane: Nautilus-style sidebar ────────────────────────── */

    GtkWidget *left_listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(left_listbox),
                                    GTK_SELECTION_SINGLE);
    build_sidebar(GTK_LIST_BOX(left_listbox));
    g_signal_connect(left_listbox, "row-activated",
                     G_CALLBACK(on_sidebar_row_activated), ed);
    ed->left_listbox = left_listbox;

    GtkWidget *left_sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(left_sw),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(left_sw), left_listbox);
    gtk_widget_set_size_request(left_sw, 180, -1);
    gtk_widget_set_vexpand(left_sw, TRUE);
    ed->left_scrolled = left_sw;

    /* ── Right pane model pipeline ───────────────────────────────── */

    g_autoptr(GFile) file = g_file_new_for_path(ed->current_path);
    GtkDirectoryList *right_dir_list = gtk_directory_list_new(DIR_ATTRS, file);
    ed->right_dir_list = right_dir_list;

    GtkCustomFilter *rf = gtk_custom_filter_new(filter_right, ed, NULL);
    ed->right_filter = rf;
    GtkFilterListModel *right_filtered =
        gtk_filter_list_model_new(G_LIST_MODEL(right_dir_list), GTK_FILTER(rf));

    GtkCustomSorter *rs = gtk_custom_sorter_new(file_sort_fn, ed, NULL);
    ed->right_sorter = rs;
    GtkSortListModel *right_sorted =
        gtk_sort_list_model_new(G_LIST_MODEL(right_filtered), GTK_SORTER(rs));

    /* Two selection models sharing the same sorted model */
    GtkSingleSelection *list_sel =
        gtk_single_selection_new(G_LIST_MODEL(g_object_ref(right_sorted)));
    GtkSingleSelection *grid_sel =
        gtk_single_selection_new(G_LIST_MODEL(right_sorted));

    /* ── Right pane: list view ───────────────────────────────────── */

    GtkListItemFactory *list_fac = gtk_signal_list_item_factory_new();
    g_signal_connect(list_fac, "setup", G_CALLBACK(setup_item_list), NULL);
    g_signal_connect(list_fac, "bind",  G_CALLBACK(bind_item_list), ed);

    GtkWidget *right_lv = gtk_list_view_new(
        GTK_SELECTION_MODEL(list_sel), list_fac);
    g_signal_connect(right_lv, "activate",
                     G_CALLBACK(on_list_item_activated), ed);
    ed->right_list_view = right_lv;

    GtkWidget *list_sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_sw),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(list_sw), right_lv);
    gtk_widget_set_hexpand(list_sw, TRUE);
    gtk_widget_set_vexpand(list_sw, TRUE);

    /* ── Right pane: grid view ───────────────────────────────────── */

    GtkListItemFactory *grid_fac = gtk_signal_list_item_factory_new();
    g_signal_connect(grid_fac, "setup", G_CALLBACK(setup_item_grid), NULL);
    g_signal_connect(grid_fac, "bind",  G_CALLBACK(bind_item_grid), ed);

    GtkWidget *right_gv = gtk_grid_view_new(
        GTK_SELECTION_MODEL(grid_sel), grid_fac);
    g_signal_connect(right_gv, "activate",
                     G_CALLBACK(on_grid_item_activated), ed);
    ed->grid_view = right_gv;

    GtkWidget *grid_sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(grid_sw),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(grid_sw), right_gv);
    gtk_widget_set_hexpand(grid_sw, TRUE);
    gtk_widget_set_vexpand(grid_sw, TRUE);

    /* ── Right pane: stack ───────────────────────────────────────── */

    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_add_named(GTK_STACK(stack), list_sw, "list");
    gtk_stack_add_named(GTK_STACK(stack), grid_sw, "grid");
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "list");
    gtk_widget_set_hexpand(stack, TRUE);
    gtk_widget_set_vexpand(stack, TRUE);
    ed->right_stack = stack;

    /* ── Ctrl+scroll zoom on right pane ───────────────────────────── */
    GtkEventController *scroll_ctrl = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    gtk_event_controller_set_propagation_phase(scroll_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(scroll_ctrl, "scroll", G_CALLBACK(on_scroll_zoom), ed);
    gtk_widget_add_controller(stack, scroll_ctrl);

    /* ── Paned ───────────────────────────────────────────────────── */

    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(paned), left_sw);
    gtk_paned_set_end_child(GTK_PANED(paned), stack);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 180);
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(box), paned);

    /* Initial title + breadcrumbs */
    g_autofree char *basename = g_path_get_basename(ed->current_path);
    gtk_label_set_text(GTK_LABEL(title_label), basename);
    rebuild_breadcrumbs(ed);

    g_object_set_data_full(G_OBJECT(box), "kterm-explorer-data",
                           ed, explorer_data_free);
    return box;
}

GtkWidget *
kterm_explorer_get_focusable(GtkWidget *explorer_box)
{
    ExplorerData *ed = g_object_get_data(G_OBJECT(explorer_box),
                                          "kterm-explorer-data");
    if (!ed) return NULL;
    return ed->grid_mode ? ed->grid_view : ed->right_list_view;
}

GtkWidget *
kterm_explorer_get_list_focusable(GtkWidget *explorer_box)
{
    ExplorerData *ed = g_object_get_data(G_OBJECT(explorer_box),
                                          "kterm-explorer-data");
    return ed ? ed->right_list_view : NULL;
}

GtkWidget *
kterm_explorer_get_grid_focusable(GtkWidget *explorer_box)
{
    ExplorerData *ed = g_object_get_data(G_OBJECT(explorer_box),
                                          "kterm-explorer-data");
    return ed ? ed->grid_view : NULL;
}

GtkWidget *
kterm_explorer_get_left_focusable(GtkWidget *explorer_box)
{
    ExplorerData *ed = g_object_get_data(G_OBJECT(explorer_box),
                                          "kterm-explorer-data");
    return ed ? ed->left_listbox : NULL;
}

GtkWidget *
kterm_explorer_get_title_label(GtkWidget *explorer_box)
{
    return g_object_get_data(G_OBJECT(explorer_box), "kterm-title-label");
}

const char *
kterm_explorer_get_path(GtkWidget *explorer_box)
{
    ExplorerData *ed = g_object_get_data(G_OBJECT(explorer_box),
                                          "kterm-explorer-data");
    return ed ? ed->current_path : NULL;
}

gboolean
kterm_explorer_is_explorer(GtkWidget *box)
{
    return g_object_get_data(G_OBJECT(box), "kterm-explorer") != NULL;
}
