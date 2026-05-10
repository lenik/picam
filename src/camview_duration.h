#ifndef CAMVIEW_DURATION_H
#define CAMVIEW_DURATION_H

#include <stdint.h>

/* Parses duration strings like 5s, 500ms, 2min, 1.5hr. Returns 0 on success. */
int camview_parse_duration_ns(const char *s, uint64_t *out_ns, char *errbuf, size_t errlen);

#endif
