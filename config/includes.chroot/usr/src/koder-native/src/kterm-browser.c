#include "kterm-browser.h"
#include <webkit/webkit.h>
#include <libintl.h>

#define _(x) gettext(x)

/* ── Browser state ────────────────────────────────────────────────── */

typedef struct {
    GtkWidget *box;
    GtkWidget *title_label;
    GtkWidget *web_view;
    GtkWidget *url_entry;
    GtkWidget *back_btn;
    GtkWidget *forward_btn;
    GtkWidget *reload_btn;
} BrowserData;

static void
browser_data_free(gpointer data)
{
    g_free(data);
}

/* ── Callbacks ────────────────────────────────────────────────────── */

static void
on_close_browser(GtkButton *btn, gpointer data)
{
    (void)btn;
    g_signal_emit_by_name(((BrowserData *)data)->box, "terminal-exited");
}

static void
on_url_activate(GtkEntry *entry, gpointer data)
{
    (void)entry;
    BrowserData *bd = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(bd->url_entry));
    if (!text || text[0] == '\0')
        return;

    /* If no scheme, prepend https:// */
    if (!g_str_has_prefix(text, "http://") &&
        !g_str_has_prefix(text, "https://") &&
        !g_str_has_prefix(text, "file://") &&
        !g_str_has_prefix(text, "about:")) {
        g_autofree char *full_url = g_strdup_printf("https://%s", text);
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(bd->web_view), full_url);
    } else {
        webkit_web_view_load_uri(WEBKIT_WEB_VIEW(bd->web_view), text);
    }
}

static void
on_title_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    BrowserData *bd = data;
    const char *title = webkit_web_view_get_title(web_view);
    if (title && *title)
        gtk_label_set_text(GTK_LABEL(bd->title_label), title);
    else
        gtk_label_set_text(GTK_LABEL(bd->title_label), _("Browser"));
}

static void
on_uri_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    BrowserData *bd = data;
    const char *uri = webkit_web_view_get_uri(web_view);
    if (uri)
        gtk_editable_set_text(GTK_EDITABLE(bd->url_entry), uri);
}

static void
on_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, gpointer data)
{
    (void)event;
    BrowserData *bd = data;
    gtk_widget_set_sensitive(bd->back_btn,
        webkit_web_view_can_go_back(web_view));
    gtk_widget_set_sensitive(bd->forward_btn,
        webkit_web_view_can_go_forward(web_view));
}

static void
on_back_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    BrowserData *bd = data;
    webkit_web_view_go_back(WEBKIT_WEB_VIEW(bd->web_view));
}

static void
on_forward_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    BrowserData *bd = data;
    webkit_web_view_go_forward(WEBKIT_WEB_VIEW(bd->web_view));
}

static void
on_reload_clicked(GtkButton *btn, gpointer data)
{
    (void)btn;
    BrowserData *bd = data;
    webkit_web_view_reload(WEBKIT_WEB_VIEW(bd->web_view));
}

/* ── Public API ───────────────────────────────────────────────────── */

GtkWidget *
kterm_browser_new(const char *url)
{
    BrowserData *bd = g_new0(BrowserData, 1);

    /* Ensure signal exists on GtkBox */
    if (g_signal_lookup("terminal-exited", GTK_TYPE_BOX) == 0) {
        g_signal_new("terminal-exited", GTK_TYPE_BOX, G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    }

    /* ── Outer container ─────────────────────────────────────────── */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_vexpand(box, TRUE);
    bd->box = box;

    /* Title bar: horizontal box with label + close button */
    GtkWidget *title_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(title_bar, "kterm-title-bar");

    GtkWidget *title_label = gtk_label_new(_("Browser"));
    gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
    gtk_widget_set_hexpand(title_label, TRUE);
    gtk_box_append(GTK_BOX(title_bar), title_label);

    GtkWidget *title_close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(title_close_btn), FALSE);
    gtk_widget_set_tooltip_text(title_close_btn, _("Close"));
    gtk_widget_set_valign(title_close_btn, GTK_ALIGN_CENTER);
    g_signal_connect(title_close_btn, "clicked", G_CALLBACK(on_close_browser), bd);
    gtk_box_append(GTK_BOX(title_bar), title_close_btn);

    gtk_box_append(GTK_BOX(box), title_bar);
    bd->title_label = title_label;

    g_object_set_data(G_OBJECT(box), "kterm-title-label", title_label);
    g_object_set_data(G_OBJECT(box), "kterm-browser", GINT_TO_POINTER(TRUE));

    /* ── Toolbar ─────────────────────────────────────────────────── */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_start(toolbar, 4);
    gtk_widget_set_margin_end(toolbar, 4);
    gtk_widget_set_margin_top(toolbar, 2);
    gtk_widget_set_margin_bottom(toolbar, 2);

    /* Back button */
    GtkWidget *back_btn = gtk_button_new_from_icon_name("go-previous-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(back_btn), FALSE);
    gtk_widget_set_tooltip_text(back_btn, _("Back"));
    gtk_widget_set_sensitive(back_btn, FALSE);
    g_signal_connect(back_btn, "clicked", G_CALLBACK(on_back_clicked), bd);
    gtk_box_append(GTK_BOX(toolbar), back_btn);
    bd->back_btn = back_btn;

    /* Forward button */
    GtkWidget *forward_btn = gtk_button_new_from_icon_name("go-next-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(forward_btn), FALSE);
    gtk_widget_set_tooltip_text(forward_btn, _("Forward"));
    gtk_widget_set_sensitive(forward_btn, FALSE);
    g_signal_connect(forward_btn, "clicked", G_CALLBACK(on_forward_clicked), bd);
    gtk_box_append(GTK_BOX(toolbar), forward_btn);
    bd->forward_btn = forward_btn;

    /* Reload button */
    GtkWidget *reload_btn = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(reload_btn), FALSE);
    gtk_widget_set_tooltip_text(reload_btn, _("Reload"));
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_reload_clicked), bd);
    gtk_box_append(GTK_BOX(toolbar), reload_btn);
    bd->reload_btn = reload_btn;

    /* URL entry */
    GtkWidget *url_entry = gtk_entry_new();
    gtk_widget_set_hexpand(url_entry, TRUE);
    g_signal_connect(url_entry, "activate", G_CALLBACK(on_url_activate), bd);
    gtk_box_append(GTK_BOX(toolbar), url_entry);
    bd->url_entry = url_entry;

    gtk_box_append(GTK_BOX(box), toolbar);

    /* ── WebView ─────────────────────────────────────────────────── */
    GtkWidget *web_view = webkit_web_view_new();
    gtk_widget_set_hexpand(web_view, TRUE);
    gtk_widget_set_vexpand(web_view, TRUE);
    gtk_box_append(GTK_BOX(box), web_view);
    bd->web_view = web_view;

    /* Connect signals */
    g_signal_connect(web_view, "notify::title",
                     G_CALLBACK(on_title_changed), bd);
    g_signal_connect(web_view, "notify::uri",
                     G_CALLBACK(on_uri_changed), bd);
    g_signal_connect(web_view, "load-changed",
                     G_CALLBACK(on_load_changed), bd);

    /* Load initial URL */
    const char *initial_url = url ? url : "https://www.google.com";
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(web_view), initial_url);
    gtk_editable_set_text(GTK_EDITABLE(url_entry), initial_url);

    g_object_set_data_full(G_OBJECT(box), "kterm-browser-data",
                           bd, browser_data_free);
    return box;
}

GtkWidget *
kterm_browser_get_focusable(GtkWidget *browser_box)
{
    BrowserData *bd = g_object_get_data(G_OBJECT(browser_box),
                                         "kterm-browser-data");
    return bd ? bd->web_view : NULL;
}

GtkWidget *
kterm_browser_get_title_label(GtkWidget *browser_box)
{
    return g_object_get_data(G_OBJECT(browser_box), "kterm-title-label");
}

const char *
kterm_browser_get_url(GtkWidget *browser_box)
{
    BrowserData *bd = g_object_get_data(G_OBJECT(browser_box),
                                         "kterm-browser-data");
    if (!bd) return NULL;
    return webkit_web_view_get_uri(WEBKIT_WEB_VIEW(bd->web_view));
}

gboolean
kterm_browser_is_browser(GtkWidget *box)
{
    return g_object_get_data(G_OBJECT(box), "kterm-browser") != NULL;
}
