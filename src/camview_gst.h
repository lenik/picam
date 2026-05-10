#ifndef CAMVIEW_GST_H
#define CAMVIEW_GST_H

#include <stdint.h>

typedef struct CamviewRunOpts {
    int device_id;
    const char *camera_name;
    const char *output_path;
    const char *format;
    int width;
    int height;
    int framerate;
    int jpeg_quality;
    int af_settle_ms;
    int no_autofocus;
    uint64_t video_duration_ns;
    int headless;
} CamviewRunOpts;

int camview_gst_init(int *argc, char ***argv);

void camview_gst_deinit(void);

int camview_run_still(const CamviewRunOpts *opts);

int camview_run_video(const CamviewRunOpts *opts);

/* Interactive preview; returns when window closed. */
int camview_run_preview(const CamviewRunOpts *opts);

#endif
