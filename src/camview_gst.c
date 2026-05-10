/*
 * Copyright (C) 2026 Lenik <picam@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "camview_gst.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <glib.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

#include <gtk/gtk.h>

typedef struct {
    GMutex lock;
    GstSample *last;
} SnapBuffer;

typedef struct {
    GstElement *pipeline;
    GstElement *vsink;
    SnapBuffer snap;
    int jpeg_quality;
    const CamviewRunOpts *opts;
} PreviewCtx;

static void snap_buffer_clear(SnapBuffer *s) {
    g_mutex_lock(&s->lock);
    if (s->last) {
        gst_sample_unref(s->last);
        s->last = NULL;
    }
    g_mutex_unlock(&s->lock);
}

static GstFlowReturn on_new_sample(GstAppSink *sink, gpointer user_data) {
    SnapBuffer *s = user_data;
    GstSample *sample = gst_app_sink_pull_sample(sink);

    (void)sink;

    if (!sample) {
        return GST_FLOW_OK;
    }

    g_mutex_lock(&s->lock);
    if (s->last) {
        gst_sample_unref(s->last);
    }
    s->last = sample;
    g_mutex_unlock(&s->lock);
    return GST_FLOW_OK;
}

static void apply_libcamera_src(GstElement *src, const CamviewRunOpts *opts, int for_still) {
    (void)for_still;

    if (opts->camera_name && opts->camera_name[0]) {
        g_object_set(src, "camera-name", opts->camera_name, NULL);
    }

    if (opts->no_autofocus) {
        g_object_set(src, "af-mode", 0, NULL);
    } else {
        g_object_set(src, "af-mode", 2, NULL);
        g_object_set(src, "af-speed", 1, NULL);
    }
}

static int write_sample_to_file(GstSample *sample, const char *path) {
    GstBuffer *buf = gst_sample_get_buffer(sample);
    GstMapInfo map;

    if (!buf || !gst_buffer_map(buf, &map, GST_MAP_READ)) {
        return -1;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("camview");
        gst_buffer_unmap(buf, &map);
        return -1;
    }

    if (fwrite(map.data, 1, map.size, f) != map.size) {
        perror("camview");
        fclose(f);
        gst_buffer_unmap(buf, &map);
        return -1;
    }

    fclose(f);
    gst_buffer_unmap(buf, &map);
    return 0;
}

typedef struct {
    GMainLoop *loop;
    int errored;
} JpegEncWait;

static gboolean jpeg_bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data) {
    JpegEncWait *w = user_data;

    (void)bus;

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
        g_main_loop_quit(w->loop);
        break;
    case GST_MESSAGE_ERROR: {
        GError *err = NULL;
        gchar *dbg = NULL;
        gst_message_parse_error(msg, &err, &dbg);
        fprintf(stderr, "camview: jpeg encode error: %s\n", err ? err->message : "?");
        if (dbg && *dbg) {
            fprintf(stderr, "%s\n", dbg);
        }
        g_clear_error(&err);
        g_free(dbg);
        w->errored = 1;
        g_main_loop_quit(w->loop);
        break;
    }
    default:
        break;
    }
    return TRUE;
}

static int write_jpeg_from_rgb_sample(GstSample *sample, const char *path, int quality) {
    GstCaps *caps = gst_sample_get_caps(sample);
    GstStructure *st;
    int w = 0;
    int h = 0;
    GstBuffer *buf = gst_sample_get_buffer(sample);
    GstMapInfo map;
    GstElement *pipe = NULL;
    GstElement *asrc = NULL;
    GstElement *jenc = NULL;
    GstElement *fsink = NULL;
    GstBus *bus = NULL;
    GMainLoop *loop = NULL;
    JpegEncWait wait = {NULL, 0};
    int ret = -1;

    if (!caps || !buf) {
        return -1;
    }

    st = gst_caps_get_structure(caps, 0);
    if (!st || !gst_structure_get_int(st, "width", &w) || !gst_structure_get_int(st, "height", &h)) {
        return -1;
    }

    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
        return -1;
    }

    pipe = gst_pipeline_new("jpegpipe");
    asrc = gst_element_factory_make("appsrc", "asrc");
    jenc = gst_element_factory_make("jpegenc", "jenc");
    fsink = gst_element_factory_make("filesink", "fsink");

    if (!pipe || !asrc || !jenc || !fsink) {
        fprintf(stderr, "camview: missing jpeg pipeline elements\n");
        goto unmap;
    }

    g_object_set(jenc, "quality", quality, NULL);
    g_object_set(fsink, "location", path, NULL);

    {
        gchar *cs = g_strdup_printf(
            "video/x-raw,format=RGB,width=%d,height=%d,pixel-aspect-ratio=1/1,framerate=1/1", w, h);
        GstCaps *acaps = gst_caps_from_string(cs);
        g_object_set(asrc, "caps", acaps, "format", GST_FORMAT_TIME, "is-live", FALSE, NULL);
        gst_caps_unref(acaps);
        g_free(cs);
    }

    gst_bin_add_many(GST_BIN(pipe), asrc, jenc, fsink, NULL);
    if (!gst_element_link_many(asrc, jenc, fsink, NULL)) {
        fprintf(stderr, "camview: failed to link jpeg sub-pipeline\n");
        goto unmap;
    }

    loop = g_main_loop_new(NULL, FALSE);
    wait.loop = loop;
    bus = gst_element_get_bus(pipe);
    gst_bus_add_watch(bus, jpeg_bus_cb, &wait);
    gst_object_unref(bus);

    gst_element_set_state(pipe, GST_STATE_PLAYING);

    {
        GstBuffer *cpy = gst_buffer_copy(buf);

        if (gst_app_src_push_buffer(GST_APP_SRC(asrc), cpy) != GST_FLOW_OK) {
            fprintf(stderr, "camview: push-buffer failed\n");
            gst_buffer_unref(cpy);
            goto stop_pipe;
        }

        gst_app_src_end_of_stream(GST_APP_SRC(asrc));
    }

    g_main_loop_run(loop);

    ret = wait.errored ? -1 : 0;

stop_pipe:
    if (pipe) {
        gst_element_set_state(pipe, GST_STATE_NULL);
        gst_object_unref(pipe);
        pipe = NULL;
    }
    if (loop) {
        g_main_loop_unref(loop);
        loop = NULL;
    }

unmap:
    if (pipe) {
        gst_element_set_state(pipe, GST_STATE_NULL);
        gst_object_unref(pipe);
    }
    if (loop) {
        g_main_loop_unref(loop);
    }
    gst_buffer_unmap(buf, &map);
    return ret;
}

static const char *pick_h264_encoder(void) {
    if (gst_element_factory_find("v4l2h264enc")) {
        return "v4l2h264enc";
    }
    if (gst_element_factory_find("x264enc")) {
        return "x264enc";
    }
    if (gst_element_factory_find("openh264enc")) {
        return "openh264enc";
    }
    return NULL;
}

static const char *pick_muxer(const char *fmt) {
    if (!fmt) {
        return "mp4mux";
    }
    if (strcasecmp(fmt, "mov") == 0) {
        return "qtmux";
    }
    if (strcasecmp(fmt, "mkv") == 0) {
        return "matroskamux";
    }
    return "mp4mux";
}

int camview_gst_init(int *argc, char ***argv) {
    GError *err = NULL;

    if (!gst_init_check(argc, argv, &err)) {
        fprintf(stderr, "camview: %s\n", err ? err->message : "gst_init failed");
        g_clear_error(&err);
        return -1;
    }
    return 0;
}

void camview_gst_deinit(void) {
    /* Avoid gst_deinit(); GTK / plugins may still hold references. */
}

int camview_run_still(const CamviewRunOpts *opts) {
    GstElement *pipeline;
    GstElement *src;
    GstElement *conv;
    GstElement *scale;
    GstElement *cf;
    GstElement *enc;
    GstElement *sink;
    GstCaps *caps = NULL;
    GstSample *sample;
    int settle_us;
    int ret = -1;

    pipeline = gst_pipeline_new("still");
    src = gst_element_factory_make("libcamerasrc", "src");
    conv = gst_element_factory_make("videoconvert", "conv");
    sink = gst_element_factory_make("appsink", "sink");

    if (!pipeline || !src || !conv || !sink) {
        fprintf(stderr, "camview: failed to create still pipeline elements\n");
        goto bad;
    }

    scale = NULL;
    cf = NULL;
    if (opts->width > 0 && opts->height > 0) {
        scale = gst_element_factory_make("videoscale", "vs");
        cf = gst_element_factory_make("capsfilter", "cf");
        if (!scale || !cf) {
            fprintf(stderr, "camview: videoscale/capsfilter missing\n");
            goto bad;
        }
        caps = gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, opts->width, "height",
                                   G_TYPE_INT, opts->height, NULL);
        g_object_set(cf, "caps", caps, NULL);
        gst_caps_unref(caps);
        caps = NULL;
    }

    if (strcasecmp(opts->format, "png") == 0) {
        enc = gst_element_factory_make("pngenc", "enc");
    } else {
        enc = gst_element_factory_make("jpegenc", "enc");
        if (enc) {
            g_object_set(enc, "quality", opts->jpeg_quality, NULL);
        }
    }

    if (!enc) {
        fprintf(stderr, "camview: image encoder not available\n");
        goto bad;
    }

    g_object_set(sink, "max-buffers", 1, "drop", FALSE, "sync", FALSE, NULL);
    gst_app_sink_set_emit_signals(GST_APP_SINK(sink), FALSE);

    apply_libcamera_src(src, opts, 1);

    gst_bin_add_many(GST_BIN(pipeline), src, conv, NULL);
    if (scale && cf) {
        gst_bin_add_many(GST_BIN(pipeline), scale, cf, enc, sink, NULL);
        if (!gst_element_link_many(src, conv, scale, cf, enc, sink, NULL)) {
            fprintf(stderr, "camview: link failed (still)\n");
            goto bad;
        }
    } else {
        gst_bin_add_many(GST_BIN(pipeline), enc, sink, NULL);
        if (!gst_element_link_many(src, conv, enc, sink, NULL)) {
            fprintf(stderr, "camview: link failed (still)\n");
            goto bad;
        }
    }

    {
        GstStateChangeReturn scr = gst_element_set_state(pipeline, GST_STATE_PLAYING);

        if (scr == GST_STATE_CHANGE_ASYNC) {
            scr = gst_element_get_state(pipeline, NULL, NULL, 30 * GST_SECOND);
        }
        if (scr != GST_STATE_CHANGE_SUCCESS) {
            fprintf(stderr,
                    "camview: still pipeline did not reach PLAYING (camera busy, no device, or "
                    "timeout)\n");
            goto stop;
        }
    }

    settle_us = opts->no_autofocus ? 100000 : opts->af_settle_ms * 1000;
    if (settle_us < 0) {
        settle_us = 1500000;
    }
    g_usleep((gulong)settle_us);

    sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 30 * GST_SECOND);
    if (!sample) {
        fprintf(stderr, "camview: timeout waiting for frame (no camera, busy device, or pipeline error)\n");
        goto stop;
    }

    ret = write_sample_to_file(sample, opts->output_path);
    gst_sample_unref(sample);

stop:
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_element_get_state(pipeline, NULL, NULL, 5 * GST_SECOND);

bad:
    if (pipeline) {
        gst_object_unref(pipeline);
    }
    return ret;
}

typedef struct {
    GMainLoop *loop;
    int failed;
} VideoBusCtx;

static gboolean video_bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data) {
    VideoBusCtx *ctx = user_data;

    (void)bus;

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError *err = NULL;
        gchar *dbg = NULL;
        gst_message_parse_error(msg, &err, &dbg);
        fprintf(stderr, "camview: %s\n", err ? err->message : "error");
        g_free(dbg);
        g_clear_error(&err);
        ctx->failed = 1;
        g_main_loop_quit(ctx->loop);
        break;
    }
    case GST_MESSAGE_EOS:
        g_main_loop_quit(ctx->loop);
        break;
    default:
        break;
    }
    return TRUE;
}

static gboolean video_eos_timer_cb(gpointer data) {
    gst_element_send_event(GST_ELEMENT(data), gst_event_new_eos());
    return G_SOURCE_REMOVE;
}

int camview_run_video(const CamviewRunOpts *opts) {
    GstElement *pipeline = NULL;
    GstElement *src;
    GstElement *conv;
    GstElement *scale = NULL;
    GstElement *cf_rate;
    GstElement *enc;
    GstElement *parse;
    GstElement *mux;
    GstElement *fsink;
    GstCaps *caps;
    const char *enc_name;
    const char *mux_name;
    GMainLoop *loop = NULL;
    GstBus *bus;
    VideoBusCtx bus_ctx = {NULL, 0};
    guint ms;

    enc_name = pick_h264_encoder();
    if (!enc_name) {
        fprintf(stderr, "camview: no H.264 encoder (v4l2h264enc, x264enc, openh264enc)\n");
        return -1;
    }

    mux_name = pick_muxer(opts->format);

    pipeline = gst_pipeline_new("rec");
    src = gst_element_factory_make("libcamerasrc", "src");
    conv = gst_element_factory_make("videoconvert", "conv");
    cf_rate = gst_element_factory_make("capsfilter", "cfr");
    parse = gst_element_factory_make("h264parse", "parse");
    mux = gst_element_factory_make(mux_name, "mux");
    fsink = gst_element_factory_make("filesink", "fsink");
    enc = gst_element_factory_make(enc_name, "enc");

    if (!pipeline || !src || !conv || !cf_rate || !enc || !parse || !mux || !fsink) {
        fprintf(stderr, "camview: failed to create video pipeline elements\n");
        if (pipeline) {
            gst_object_unref(pipeline);
        }
        return -1;
    }

    g_object_set(fsink, "location", opts->output_path, NULL);

    if (strcmp(enc_name, "x264enc") == 0) {
        g_object_set(enc, "tune", 4, "speed-preset", 1, NULL);
    }

    if (opts->width > 0 && opts->height > 0) {
        caps = gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, opts->width, "height",
                                   G_TYPE_INT, opts->height, "framerate", GST_TYPE_FRACTION,
                                   opts->framerate, 1, NULL);
        scale = gst_element_factory_make("videoscale", "vs");
        if (!scale) {
            fprintf(stderr, "camview: videoscale missing\n");
            gst_object_unref(pipeline);
            return -1;
        }
    } else {
        caps = gst_caps_new_simple("video/x-raw", "framerate", GST_TYPE_FRACTION, opts->framerate, 1,
                                   NULL);
    }

    g_object_set(cf_rate, "caps", caps, NULL);
    gst_caps_unref(caps);

    apply_libcamera_src(src, opts, 0);

    gst_bin_add_many(GST_BIN(pipeline), src, conv, cf_rate, enc, parse, mux, fsink, NULL);
    if (scale) {
        gst_bin_add(GST_BIN(pipeline), scale);
    }

    if (scale) {
        if (!gst_element_link_many(src, conv, scale, cf_rate, NULL)) {
            fprintf(stderr, "camview: link failed (video / scale)\n");
            gst_object_unref(pipeline);
            return -1;
        }
    } else {
        if (!gst_element_link_many(src, conv, cf_rate, NULL)) {
            fprintf(stderr, "camview: link failed (video)\n");
            gst_object_unref(pipeline);
            return -1;
        }
    }

    if (!gst_element_link_many(cf_rate, enc, parse, mux, fsink, NULL)) {
        fprintf(stderr, "camview: link failed (video / encode)\n");
        gst_object_unref(pipeline);
        return -1;
    }

    loop = g_main_loop_new(NULL, FALSE);
    bus_ctx.loop = loop;
    bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, video_bus_cb, &bus_ctx);
    gst_object_unref(bus);

    ms = (guint)(opts->video_duration_ns / 1000000ULL);
    if (ms == 0) {
        ms = 1;
    }
    g_timeout_add(ms, video_eos_timer_cb, pipeline);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_main_loop_run(loop);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    g_main_loop_unref(loop);
    gst_object_unref(pipeline);
    return bus_ctx.failed ? -1 : 0;
}

typedef struct {
    GtkApplication *app;
} PreviewBusCtx;

static gboolean preview_bus_cb(GstBus *bus, GstMessage *msg, gpointer user_data) {
    PreviewBusCtx *bc = user_data;

    (void)bus;

    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
        GError *err = NULL;
        gchar *dbg = NULL;
        gst_message_parse_error(msg, &err, &dbg);
        fprintf(stderr, "camview: preview: %s\n", err ? err->message : "error");
        g_free(dbg);
        g_clear_error(&err);
        g_application_quit(G_APPLICATION(bc->app));
    }
    return TRUE;
}

static void preview_save_jpeg(PreviewCtx *ctx, const char *path) {
    GstSample *s;

    g_mutex_lock(&ctx->snap.lock);
    s = ctx->snap.last ? gst_sample_ref(ctx->snap.last) : NULL;
    g_mutex_unlock(&ctx->snap.lock);

    if (!s) {
        fprintf(stderr, "camview: no preview frame yet; wait a moment.\n");
        return;
    }

    if (write_jpeg_from_rgb_sample(s, path, ctx->jpeg_quality) != 0) {
        fprintf(stderr, "camview: failed to write %s\n", path);
    } else {
        fprintf(stderr, "camview: saved %s\n", path);
    }

    gst_sample_unref(s);
}

static gboolean preview_key_cb(GtkEventControllerKey *ctl, guint keyval, guint keycode,
                               GdkModifierType state, gpointer user_data) {
    PreviewCtx *ctx = user_data;
    GDateTime *now;
    gchar *path;
    const char *dir;

    (void)ctl;
    (void)keycode;
    (void)state;

    if (keyval == GDK_KEY_q || keyval == GDK_KEY_Q) {
        g_application_quit(g_application_get_default());
        return TRUE;
    }

    if (keyval == GDK_KEY_p || keyval == GDK_KEY_P) {
        dir = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
        if (!dir) {
            dir = ".";
        }
        now = g_date_time_new_now_local();
        path = g_strdup_printf("%s/camview_%04d%02d%02d_%02d%02d%02d.jpg", dir,
                               g_date_time_get_year(now), g_date_time_get_month(now),
                               g_date_time_get_day_of_month(now), g_date_time_get_hour(now),
                               g_date_time_get_minute(now), g_date_time_get_second(now));
        g_date_time_unref(now);
        preview_save_jpeg(ctx, path);
        g_free(path);
        return TRUE;
    }

    return FALSE;
}

static gboolean preview_close_request_cb(GtkWindow *win, gpointer user_data) {
    PreviewCtx *ctx = user_data;

    (void)win;

    if (ctx->pipeline) {
        gst_element_set_state(ctx->pipeline, GST_STATE_NULL);
    }
    snap_buffer_clear(&ctx->snap);
    g_application_quit(g_application_get_default());
    return TRUE;
}

static void preview_activate(GtkApplication *app, gpointer user_data) {
    PreviewCtx *ctx = user_data;
    const CamviewRunOpts *opts = ctx->opts;
    GtkWidget *win;
    GtkWidget *pic;
    GtkEventController *keyctl;
    GError *err = NULL;
    gchar *desc;
    GstElement *src;
    GstElement *snap;
    GstBus *bus;
    PreviewBusCtx bc;
    GdkPaintable *paintable;

    desc = g_strdup(
        "libcamerasrc name=src ! queue ! videoconvert ! tee name=t "
        "t. ! queue ! gtk4paintablesink name=vsink "
        "t. ! queue max-size-buffers=2 leaky=downstream ! videoconvert ! "
        "video/x-raw,format=RGB ! "
        "appsink name=snap max-buffers=1 drop=true sync=false emit-signals=true");

    ctx->pipeline = gst_parse_launch(desc, &err);
    g_free(desc);

    if (!ctx->pipeline || err) {
        fprintf(stderr, "camview: %s\n", err ? err->message : "parse preview pipeline");
        g_clear_error(&err);
        g_application_quit(G_APPLICATION(app));
        return;
    }

    src = gst_bin_get_by_name(GST_BIN(ctx->pipeline), "src");
    ctx->vsink = gst_bin_get_by_name(GST_BIN(ctx->pipeline), "vsink");
    snap = gst_bin_get_by_name(GST_BIN(ctx->pipeline), "snap");

    if (!src || !ctx->vsink || !snap) {
        fprintf(stderr, "camview: preview pipeline missing elements\n");
        if (src) {
            gst_object_unref(src);
        }
        if (ctx->vsink) {
            gst_object_unref(ctx->vsink);
        }
        if (snap) {
            gst_object_unref(snap);
        }
        gst_object_unref(ctx->pipeline);
        ctx->pipeline = NULL;
        g_application_quit(G_APPLICATION(app));
        return;
    }

    apply_libcamera_src(src, opts, 0);

    gst_object_unref(src);

    ctx->snap.last = NULL;
    gst_app_sink_set_emit_signals(GST_APP_SINK(snap), TRUE);
    g_signal_connect(snap, "new-sample", G_CALLBACK(on_new_sample), &ctx->snap);
    gst_object_unref(snap);

    win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "camview");
    gtk_window_set_default_size(GTK_WINDOW(win), 960, 540);

    pic = gtk_picture_new();
    gtk_window_set_child(GTK_WINDOW(win), pic);

    keyctl = gtk_event_controller_key_new();
    g_signal_connect(keyctl, "key-pressed", G_CALLBACK(preview_key_cb), ctx);
    gtk_widget_add_controller(win, keyctl);

    g_signal_connect(win, "close-request", G_CALLBACK(preview_close_request_cb), ctx);

    paintable = NULL;
    g_object_get(ctx->vsink, "paintable", &paintable, NULL);
    if (paintable) {
        gtk_picture_set_paintable(GTK_PICTURE(pic), paintable);
        g_object_unref(paintable);
    }

    gtk_window_present(GTK_WINDOW(win));

    bc.app = app;
    bus = gst_element_get_bus(ctx->pipeline);
    gst_bus_add_watch(bus, preview_bus_cb, &bc);
    gst_object_unref(bus);

    gst_element_set_state(ctx->pipeline, GST_STATE_PLAYING);
}

int camview_run_preview(const CamviewRunOpts *opts) {
    GtkApplication *app;
    PreviewCtx ctx;
    int status;

    if (opts->headless) {
        fprintf(stderr, "camview: preview requires a display (headless mode is on)\n");
        return 1;
    }

    memset(&ctx, 0, sizeof(ctx));
    g_mutex_init(&ctx.snap.lock);
    ctx.jpeg_quality = opts->jpeg_quality;
    ctx.opts = opts;

    app = gtk_application_new("net.bodz.picam.camview", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect_data(app, "activate", G_CALLBACK(preview_activate), &ctx, NULL, 0);
    status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);

    if (ctx.vsink) {
        gst_object_unref(ctx.vsink);
    }
    if (ctx.pipeline) {
        gst_object_unref(ctx.pipeline);
    }
    g_mutex_clear(&ctx.snap.lock);

    return status;
}