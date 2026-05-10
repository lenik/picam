/*
 * Copyright (C) 2026 Lenik <picam@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "camview_cameras.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

static void parse_list_output(const char *text, GPtrArray *arr) {
    const char *line = text;

    while (line && *line) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        char *buf = g_strndup(line, len);
        char *p = buf;

        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (isdigit((unsigned char)*p)) {
            char *lp = strchr(p, '(');
            if (lp) {
                lp++;
                char *rp = strchr(lp, ')');
                if (rp) {
                    *rp = '\0';
                    while (*lp == ' ' || *lp == '\t') {
                        lp++;
                    }
                    if (*lp != '\0') {
                        g_ptr_array_add(arr, g_strdup(lp));
                    }
                }
            }
        }

        g_free(buf);
        if (eol) {
            line = eol + 1;
        } else {
            break;
        }
    }
}

static int try_list_command(const char *argv0, char **out_text) {
    gchar *stdout_data = NULL;
    gchar *stderr_data = NULL;
    gint exit_status = 0;
    gchar *argv[] = {(gchar *)argv0, (gchar *)"--list-cameras", NULL};
    GError *err = NULL;

    if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &stdout_data, &stderr_data,
                      &exit_status, &err)) {
        g_clear_error(&err);
        g_free(stdout_data);
        g_free(stderr_data);
        return -1;
    }

    (void)exit_status;
    g_free(stderr_data);
    *out_text = stdout_data;
    return 0;
}

int camview_list_cameras(char ***names, size_t *count) {
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    char *text = NULL;

    if (!names || !count) {
        g_ptr_array_unref(arr);
        return -1;
    }

    *names = NULL;
    *count = 0;

    if (try_list_command("rpicam-hello", &text) != 0) {
        try_list_command("libcamera-hello", &text);
    }

    if (text) {
        parse_list_output(text, arr);
        g_free(text);
    }

    *count = arr->len;
    if (*count == 0) {
        g_ptr_array_unref(arr);
        return 0;
    }

    *names = g_new0(char *, *count + 1);
    for (gsize i = 0; i < arr->len; i++) {
        (*names)[i] = g_strdup(g_ptr_array_index(arr, i));
    }
    g_ptr_array_unref(arr);
    return 0;
}

void camview_free_camera_list(char **names, size_t count) {
    if (!names) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        g_free(names[i]);
    }
    g_free(names);
}

int camview_resolve_camera(int device_id, const char *override, char **out_name, char *errbuf,
                           size_t errlen) {
    char **list = NULL;
    size_t n = 0;

    if (!out_name) {
        return -1;
    }
    *out_name = NULL;

    if (override && *override) {
        *out_name = g_strdup(override);
        return 0;
    }

    if (device_id < 1) {
        if (errbuf && errlen) {
            snprintf(errbuf, errlen, "device id must be >= 1");
        }
        return -1;
    }

    if (camview_list_cameras(&list, &n) != 0) {
        if (errbuf && errlen) {
            snprintf(errbuf, errlen, "failed to list cameras");
        }
        return -1;
    }

    if (n == 0) {
        camview_free_camera_list(list, n);
        if (device_id == 1) {
            *out_name = NULL;
            return 0;
        }
        if (errbuf && errlen) {
            snprintf(errbuf, errlen,
                     "camera %d requested but no cameras listed (install rpicam-hello or use "
                     "--camera-name)",
                     device_id);
        }
        return -1;
    }

    if ((size_t)device_id > n) {
        if (errbuf && errlen) {
            snprintf(errbuf, errlen, "camera %d not found (%zu available)", device_id, n);
        }
        camview_free_camera_list(list, n);
        return -1;
    }

    *out_name = g_strdup(list[device_id - 1]);
    camview_free_camera_list(list, n);
    return 0;
}
