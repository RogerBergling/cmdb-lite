#include "platform_dialog.h"

#include "cmdb/util.h"

#include <windows.h>
#include <commdlg.h>

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static wchar_t *utf8_to_wide(const char *value) {
  int length;
  wchar_t *wide;

  if (value == NULL) {
    return NULL;
  }
  length = MultiByteToWideChar(CP_UTF8, 0, value, -1, NULL, 0);
  if (length <= 0) {
    return NULL;
  }
  wide = (wchar_t *)cmdb_calloc((size_t)length, sizeof(wchar_t));
  if (MultiByteToWideChar(CP_UTF8, 0, value, -1, wide, length) != length) {
    free(wide);
    return NULL;
  }
  return wide;
}

static char *wide_to_utf8(const wchar_t *value) {
  int length;
  char *utf8;

  if (value == NULL) {
    return NULL;
  }
  length = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
  if (length <= 0) {
    return NULL;
  }
  utf8 = (char *)cmdb_calloc((size_t)length, sizeof(char));
  if (WideCharToMultiByte(CP_UTF8, 0, value, -1, utf8, length,
                          NULL, NULL) != length) {
    free(utf8);
    return NULL;
  }
  return utf8;
}

static void extension_from_name(const char *name, wchar_t *out,
                                size_t out_count) {
  const char *dot;
  if (out == NULL || out_count == 0) {
    return;
  }
  out[0] = L'\0';
  if (name == NULL) {
    return;
  }
  dot = strrchr(name, '.');
  if (dot == NULL || dot[1] == '\0') {
    return;
  }
  {
    wchar_t *wide = utf8_to_wide(dot + 1);
    if (wide != NULL) {
      wcsncpy(out, wide, out_count - 1);
      out[out_count - 1] = L'\0';
      free(wide);
    }
  }
}

void platform_configure_webview(void *webview) {
  (void)webview;
}

char *platform_choose_file(int save, const char *title,
                           const char *filter_name, const char *pattern,
                           const char *suggested_name, cmdb_error_t *err) {
  wchar_t file[32768];
  wchar_t filter[4096];
  wchar_t extension[32];
  wchar_t *wide_title = NULL;
  wchar_t *wide_filter_name = NULL;
  wchar_t *wide_pattern = NULL;
  wchar_t *wide_suggested = NULL;
  wchar_t *wide_all = NULL;
  wchar_t *wide_star = NULL;
  OPENFILENAMEW ofn;
  BOOL ok;
  char *result = NULL;

  memset(file, 0, sizeof(file));
  memset(filter, 0, sizeof(filter));
  memset(&ofn, 0, sizeof(ofn));

  wide_title = utf8_to_wide(title != NULL ? title
                                          : (save ? "Save file" : "Open file"));
  wide_filter_name = utf8_to_wide(filter_name != NULL ? filter_name : "Files");
  wide_pattern = utf8_to_wide(pattern != NULL ? pattern : "*");
  wide_all = utf8_to_wide("All files");
  wide_star = utf8_to_wide("*.*");
  wide_suggested = utf8_to_wide(suggested_name);

  if (wide_title == NULL || wide_filter_name == NULL || wide_pattern == NULL ||
      wide_all == NULL || wide_star == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot prepare file dialog");
    goto cleanup;
  }

  {
    size_t pos = 0;
    const wchar_t *parts[4];
    size_t part_index;
    parts[0] = wide_filter_name;
    parts[1] = wide_pattern;
    parts[2] = wide_all;
    parts[3] = wide_star;
    for (part_index = 0; part_index < 4; part_index++) {
      size_t part_len = wcslen(parts[part_index]);
      if (pos + part_len + 1 >= (sizeof(filter) / sizeof(filter[0]))) {
        cmdb_error_set(err, CMDB_ERR_LIMIT, "File dialog filter is too long");
        goto cleanup;
      }
      memcpy(&filter[pos], parts[part_index],
             (part_len + 1) * sizeof(wchar_t));
      pos += part_len + 1;
    }
    filter[pos] = L'\0';
  }

  if (save && wide_suggested != NULL) {
    (void)wcsncpy(file, wide_suggested,
                  (sizeof(file) / sizeof(file[0])) - 1);
    file[(sizeof(file) / sizeof(file[0])) - 1] = L'\0';
  }

  extension_from_name(suggested_name, extension, sizeof(extension) / sizeof(extension[0]));

  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = GetActiveWindow();
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = file;
  ofn.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
  ofn.lpstrTitle = wide_title;
  ofn.lpstrDefExt = extension[0] != L'\0' ? extension : NULL;
  ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR |
              (save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST)
                    : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));

  if (save) {
    ok = GetSaveFileNameW(&ofn);
  } else {
    ok = GetOpenFileNameW(&ofn);
  }

  if (!ok) {
    DWORD code = CommDlgExtendedError();
    if (code != 0) {
      cmdb_error_set(err, CMDB_ERR_IO, "File dialog failed");
      cmdb_error_set_detail(err, "CommDlgExtendedError=%lu",
                            (unsigned long)code);
    }
    goto cleanup;
  }

  result = wide_to_utf8(file);
  if (result == NULL) {
    cmdb_error_set(err, CMDB_ERR_INTERNAL, "Cannot convert selected path");
  }

cleanup:
  free(wide_title);
  free(wide_filter_name);
  free(wide_pattern);
  free(wide_suggested);
  free(wide_all);
  free(wide_star);
  return result;
}
