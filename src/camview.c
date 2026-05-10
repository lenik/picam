/*
 * Copyright (C) 2026 Lenik <picam@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "camview.h"

#include "camview_cameras.h"
#include "camview_duration.h"
#include "camview_gst.h"

#include "config.h"

#include <bas/locale/i18n.h>
#include <bas/log/deflog.h>
#include <bas/proc/env.h>

#include <glib.h>

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

define_logger();

enum {
    OPT_VERSION = 256,
    OPT_CAMERA_NAME,
    OPT_FRAMERATE,
    OPT_AF_SETTLE,
    OPT_NO_AF,
    OPT_HEADLESS,
    OPT_JPEG_QUALITY,
};

void usage(FILE *out) {
    fputs(_("Usage: camview [OPTION]...\n"
            "Raspberry Pi 5 CSI/MIPI camera preview and capture (GStreamer + libcamera).\n"),
          out);
    fputs("\n", out);
    fputs(_("Preview (graphical):\n"), out);
    fputs("  With no capture flags, opens a live preview. Keys: P save JPEG, Q quit.\n", out);
    fputs("\n", out);
    fputs(_("Capture (headless):\n"), out);
    fputs("  -s, --still              capture one still image and exit\n", out);
    fputs("  -d, --duration TIME      record H.264 video for TIME then exit (e.g. 10s, 500ms, 2min)\n",
          out);
    fputs("  -f, --format FMT         jpg|png (still) or mp4|mov|mkv (video); default by mode\n", out);
    fputs("  -o, --output PATH        output file (default: camview_YYYYMMDD_HHMMSS.ext)\n", out);
    fputs("\n", out);
    fputs(_("Camera:\n"), out);
    fputs("  -i, --device ID          CSI camera index starting at 1 (2 = second camera)\n", out);
    fputs("      --camera-name PATH   libcamera id (overrides -i; from rpicam-hello --list-cameras)\n",
          out);
    fputs("\n", out);
    fputs(_("Image / video tuning:\n"), out);
    fputs("  -r, --resolution WxH      scale output (e.g. 1920x1080); 0 = sensor default\n", out);
    fputs("      --framerate N        frames per second for video (default: 30)\n", out);
    fputs("      --jpeg-quality N     1-100 (default: 92)\n", out);
    fputs("      --af-settle-ms MS    wait before still capture for autofocus (default: 1500)\n", out);
    fputs("      --no-autofocus       disable continuous AF on libcamera\n", out);
    fputs("      --headless           never open a window (for capture modes)\n", out);
    fputs("\n", out);
    fputs("  -v, --verbose            ", out);
    fputs(_("repeat for more verbose loggings\n"), out);
    fputs("  -q, --quiet              ", out);
    fputs(_("show less logging messages\n"), out);
    fputs("  -h, --help               ", out);
    fputs(_("display this help and exit\n"), out);
    fputs("      --version            ", out);
    fputs(_("output version information and exit\n"), out);
    fputs("\n", out);
    fprintf(out, _("Report bugs to: <%s>\n"), PROJECT_EMAIL);
}

static char *default_output_path(const char *ext, int is_still) {
    GDateTime *now = g_date_time_new_now_local();
    char *path = g_strdup_printf("camview_%04d%02d%02d_%02d%02d%02d.%s", g_date_time_get_year(now),
                                   g_date_time_get_month(now), g_date_time_get_day_of_month(now),
                                   g_date_time_get_hour(now), g_date_time_get_minute(now),
                                   g_date_time_get_second(now), ext);
    (void)is_still;
    g_date_time_unref(now);
    return path;
}

static const char *normalize_still_format(const char *f) {
    if (!f || !*f) {
        return "jpg";
    }
    if (strcasecmp(f, "jpeg") == 0) {
        return "jpg";
    }
    if (strcasecmp(f, "jpg") == 0 || strcasecmp(f, "png") == 0) {
        return f;
    }
    return NULL;
}

static const char *normalize_video_format(const char *f) {
    if (!f || !*f) {
        return "mp4";
    }
    if (strcasecmp(f, "mpeg4") == 0) {
        return "mp4";
    }
    if (strcasecmp(f, "mp4") == 0 || strcasecmp(f, "mov") == 0 || strcasecmp(f, "mkv") == 0) {
        return f;
    }
    return NULL;
}

static const char *pick_ext(const char *fmt, int video) {
    if (video) {
        if (strcasecmp(fmt, "mov") == 0) {
            return "mov";
        }
        if (strcasecmp(fmt, "mkv") == 0) {
            return "mkv";
        }
        return "mp4";
    }
    if (strcasecmp(fmt, "png") == 0) {
        return "png";
    }
    return "jpg";
}

int main(int argc, char **argv) {
    const char *exe = self_exe();
    CamviewRunOpts opts;
    char *cam_name = NULL;
    char *output_alloc = NULL;
    char errbuf[256];
    int opt;
    int still = 0;
    int video = 0;
    char *duration_arg = NULL;
    int device_id = 0;
    const char *camera_name_opt = NULL;
    const char *format_opt = NULL;
    const char *output_opt = NULL;
    int width = 0;
    int height = 0;
    int framerate = 30;
    int jpeg_quality = 92;
    int af_settle_ms = 1500;
    int no_autofocus = 0;
    int headless_force = 0;
    uint64_t video_dur_ns = 0;

    init_i18n(LOCALEDIR);

    memset(&opts, 0, sizeof(opts));
    opts.framerate = 30;
    opts.jpeg_quality = 92;
    opts.af_settle_ms = 1500;

    static const struct option long_opts[] = {
        {"verbose", no_argument, NULL, 'v'},
        {"quiet", no_argument, NULL, 'q'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, OPT_VERSION},
        {"still", no_argument, NULL, 's'},
        {"duration", required_argument, NULL, 'd'},
        {"format", required_argument, NULL, 'f'},
        {"output", required_argument, NULL, 'o'},
        {"device", required_argument, NULL, 'i'},
        {"camera-name", required_argument, NULL, OPT_CAMERA_NAME},
        {"resolution", required_argument, NULL, 'r'},
        {"framerate", required_argument, NULL, OPT_FRAMERATE},
        {"jpeg-quality", required_argument, NULL, OPT_JPEG_QUALITY},
        {"af-settle-ms", required_argument, NULL, OPT_AF_SETTLE},
        {"no-autofocus", no_argument, NULL, OPT_NO_AF},
        {"headless", no_argument, NULL, OPT_HEADLESS},
        {NULL, 0, NULL, 0},
    };

    for (;;) {
        opt = getopt_long(argc, argv, "vqhsd:f:o:i:r:", long_opts, NULL);
        if (opt == -1) {
            break;
        }
        switch (opt) {
        case 'v':
            log_more();
            break;
        case 'q':
            log_less();
            break;
        case 'h':
            usage(stdout);
            return 0;
        case OPT_VERSION:
            printf("camview %s\n", PROJECT_VERSION);
            printf(_("Copyright (C) %d %s\n"), PROJECT_YEAR, PROJECT_AUTHOR);
            fputs(_("License AGPL-3.0-or-later: <https://www.gnu.org/licenses/agpl-3.0.html>\n"),
                  stdout);
            fputs(_("This is free software: you are free to change and redistribute it.\n"),
                  stdout);
            fputs(_("This project opposes AI exploitation and AI hegemony.\n"), stdout);
            fputs(_("This project rejects mindless MIT-style licensing and politically naive "
                    "BSD-style licensing.\n"),
                  stdout);
            fputs(_("There is NO WARRANTY, to the extent permitted by law.\n"), stdout);
            return 0;
        case 's':
            still = 1;
            break;
        case 'd':
            duration_arg = optarg;
            video = 1;
            break;
        case 'f':
            format_opt = optarg;
            break;
        case 'o':
            output_opt = optarg;
            break;
        case 'i':
            device_id = atoi(optarg);
            break;
        case OPT_CAMERA_NAME:
            camera_name_opt = optarg;
            break;
        case 'r':
            if (sscanf(optarg, "%dx%d", &width, &height) != 2 || width <= 0 || height <= 0) {
                fprintf(stderr, "%s: bad --resolution (use WxH, e.g. 1920x1080)\n", exe);
                return 1;
            }
            break;
        case OPT_FRAMERATE:
            framerate = atoi(optarg);
            if (framerate <= 0) {
                fprintf(stderr, "%s: bad --framerate\n", exe);
                return 1;
            }
            break;
        case OPT_JPEG_QUALITY:
            jpeg_quality = atoi(optarg);
            if (jpeg_quality < 1 || jpeg_quality > 100) {
                fprintf(stderr, "%s: --jpeg-quality must be 1-100\n", exe);
                return 1;
            }
            break;
        case OPT_AF_SETTLE:
            af_settle_ms = atoi(optarg);
            if (af_settle_ms < 0) {
                fprintf(stderr, "%s: bad --af-settle-ms\n", exe);
                return 1;
            }
            break;
        case OPT_NO_AF:
            no_autofocus = 1;
            break;
        case OPT_HEADLESS:
            headless_force = 1;
            break;
        default:
            usage(stderr);
            return 1;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "%s: unexpected argument: %s\n", exe, argv[optind]);
        usage(stderr);
        return 1;
    }

    if (still && video) {
        fprintf(stderr, "%s: use only one of --still and --duration\n", exe);
        return 1;
    }

    if (video && (!duration_arg || !*duration_arg)) {
        fprintf(stderr, "%s: --duration requires a value\n", exe);
        return 1;
    }

    if (video && camview_parse_duration_ns(duration_arg, &video_dur_ns, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "%s: %s\n", exe, errbuf);
        return 1;
    }

    if (video && video_dur_ns == 0) {
        fprintf(stderr, "%s: --duration must be greater than zero\n", exe);
        return 1;
    }

    if (still) {
        const char *nf = normalize_still_format(format_opt);
        if (format_opt && !nf) {
            fprintf(stderr, "%s: still --format must be jpg or png\n", exe);
            return 1;
        }
        format_opt = nf;
    } else if (video) {
        const char *nf = normalize_video_format(format_opt);
        if (format_opt && !nf) {
            fprintf(stderr, "%s: video --format must be mp4, mov, or mkv\n", exe);
            return 1;
        }
        format_opt = nf;
    }

    if (camview_gst_init(&argc, &argv) != 0) {
        return 1;
    }

    if (device_id < 0) {
        fprintf(stderr, "%s: --device must be >= 1\n", exe);
        return 1;
    }

    if (camera_name_opt && *camera_name_opt) {
        cam_name = g_strdup(camera_name_opt);
    } else if (device_id > 0) {
        if (camview_resolve_camera(device_id, NULL, &cam_name, errbuf, sizeof(errbuf)) != 0) {
            fprintf(stderr, "%s: %s\n", exe, errbuf);
            return 1;
        }
    } else {
        /* default first camera: try explicit name for index 1 when enumerable */
        if (camview_resolve_camera(1, NULL, &cam_name, errbuf, sizeof(errbuf)) != 0) {
            fprintf(stderr, "%s: %s\n", exe, errbuf);
            return 1;
        }
    }

    opts.camera_name = cam_name;
    opts.width = width;
    opts.height = height;
    opts.framerate = framerate;
    opts.jpeg_quality = jpeg_quality;
    opts.af_settle_ms = af_settle_ms;
    opts.no_autofocus = no_autofocus;
    opts.format = format_opt;
    opts.video_duration_ns = video_dur_ns;
    opts.headless = headless_force || still || video;

    if (still) {
        if (!output_opt) {
            output_alloc = default_output_path(pick_ext(format_opt, 0), 1);
            output_opt = output_alloc;
        }
        opts.output_path = output_opt;
        opts.format = format_opt;
        loginfo_fmt("%s: still -> %s", exe, output_opt);
        if (camview_run_still(&opts) != 0) {
            g_free(cam_name);
            g_free(output_alloc);
            return 1;
        }
    } else if (video) {
        if (!output_opt) {
            output_alloc = default_output_path(pick_ext(format_opt, 1), 0);
            output_opt = output_alloc;
        }
        opts.output_path = output_opt;
        opts.format = format_opt;
        loginfo_fmt("%s: video %s -> %s", exe, duration_arg, output_opt);
        if (camview_run_video(&opts) != 0) {
            g_free(cam_name);
            g_free(output_alloc);
            return 1;
        }
    } else {
        if (format_opt || output_opt || duration_arg) {
            fprintf(stderr,
                    "%s: in preview mode do not use --format/--output/--duration "
                    "(use P key to save JPEG)\n",
                    exe);
            g_free(cam_name);
            return 1;
        }
        loginfo_fmt("%s: preview", exe);
        if (camview_run_preview(&opts) != 0) {
            g_free(cam_name);
            return 1;
        }
    }

    g_free(cam_name);
    g_free(output_alloc);
    return 0;
}
