#include "platform_dialog.h"

#include "cmdb/util.h"

#include "webview/webview.h"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <stdlib.h>
#include <string.h>

void platform_configure_webview(void *webview) {
  WebKitWebView *view;
  WebKitSettings *settings;

  if (webview == NULL) {
    return;
  }
  view = (WebKitWebView *)webview_get_native_handle(
    (webview_t)webview, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER);
  if (view == NULL) {
    return;
  }
  settings = webkit_web_view_get_settings(view);
  if (settings != NULL) {
    /* Relative local assets (index.html + app.js/style.css) must load. */
    webkit_settings_set_allow_file_access_from_file_urls(settings, TRUE);
    webkit_settings_set_allow_universal_access_from_file_urls(settings, FALSE);
  }
}

char *platform_choose_file(int save, const char *title,
                           const char *filter_name, const char *pattern,
                           const char *suggested_name, cmdb_error_t *err) {
  GtkWidget *dialog;
  GtkFileFilter *filter;
  GtkFileFilter *all_filter;
  char *filename = NULL;
  char *result = NULL;
  int response;

  if (!gtk_init_check(NULL, NULL)) {
    cmdb_error_set(err, CMDB_ERR_IO, "Cannot initialize GTK file dialog");
    return NULL;
  }

  dialog = gtk_file_chooser_dialog_new(
    title != NULL ? title : (save ? "Save file" : "Open file"),
    NULL,
    save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
    "_Cancel", GTK_RESPONSE_CANCEL,
    save ? "_Save" : "_Open", GTK_RESPONSE_ACCEPT,
    NULL);

  filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, filter_name != NULL ? filter_name : "Files");
  gtk_file_filter_add_pattern(filter, pattern != NULL ? pattern : "*");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

  all_filter = gtk_file_filter_new();
  gtk_file_filter_set_name(all_filter, "All files");
  gtk_file_filter_add_pattern(all_filter, "*");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_filter);

  if (save) {
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    if (suggested_name != NULL) {
      gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), suggested_name);
    }
  }

  response = gtk_dialog_run(GTK_DIALOG(dialog));
  if (response == GTK_RESPONSE_ACCEPT) {
    filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    if (filename != NULL) {
      if (!cmdb_utf8_valid(filename, strlen(filename))) {
        cmdb_error_set(err, CMDB_ERR_VALIDATION,
                       "Selected path is not valid UTF-8");
      } else {
        result = cmdb_strdup(filename);
      }
      g_free(filename);
    }
  }
  gtk_widget_destroy(dialog);
  return result;
}
