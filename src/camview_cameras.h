#ifndef CAMVIEW_CAMERAS_H
#define CAMVIEW_CAMERAS_H

#include <stddef.h>

/* Runs rpicam-hello / libcamera-hello list; returns 0 on success (possibly 0 cameras). */
int camview_list_cameras(char ***names, size_t *count);

void camview_free_camera_list(char **names, size_t count);

/*
 * Resolve libcamera path for device index (1 = first CSI camera).
 * If override is non-NULL, copies it into *out_name (caller must g_free).
 * Otherwise uses list[device_id-1]. device_id must be >= 1.
 */
int camview_resolve_camera(int device_id, const char *override, char **out_name, char *errbuf,
                           size_t errlen);

#endif
