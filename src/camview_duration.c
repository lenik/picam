/*
 * Copyright (C) 2026 Lenik <picam@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "camview_duration.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void err(char *errbuf, size_t errlen, const char *msg) {
    if (errbuf && errlen) {
        size_t n = strlen(msg);
        if (n >= errlen) {
            n = errlen - 1;
        }
        memcpy(errbuf, msg, n);
        errbuf[n] = '\0';
    }
}

int camview_parse_duration_ns(const char *s, uint64_t *out_ns, char *errbuf, size_t errlen) {
    char *endn = NULL;
    double v;
    uint64_t mult;
    unsigned long long acc;
    const char *u;

    if (!s || !*s || !out_ns) {
        err(errbuf, errlen, "empty duration");
        return -1;
    }

    while (*s == ' ' || *s == '\t') {
        s++;
    }

    errno = 0;
    v = strtod(s, &endn);
    if (endn == s || errno == ERANGE || v < 0) {
        err(errbuf, errlen, "invalid duration number");
        return -1;
    }

    u = endn;
    while (*u == ' ' || *u == '\t') {
        u++;
    }

    if (*u == '\0') {
        mult = 1000000000ULL;
    } else if (strcasecmp(u, "ns") == 0) {
        mult = 1ULL;
    } else if (strcasecmp(u, "us") == 0 || strcmp(u, "µs") == 0) {
        mult = 1000ULL;
    } else if (strcasecmp(u, "ms") == 0) {
        mult = 1000000ULL;
    } else if (strcasecmp(u, "s") == 0 || strcasecmp(u, "sec") == 0 || strcasecmp(u, "secs") == 0 ||
               strcasecmp(u, "second") == 0 || strcasecmp(u, "seconds") == 0) {
        mult = 1000000000ULL;
    } else if (strcasecmp(u, "m") == 0 || strcasecmp(u, "min") == 0 || strcasecmp(u, "mins") == 0 ||
               strcasecmp(u, "minute") == 0 || strcasecmp(u, "minutes") == 0) {
        mult = 60ULL * 1000000000ULL;
    } else if (strcasecmp(u, "h") == 0 || strcasecmp(u, "hr") == 0 || strcasecmp(u, "hrs") == 0 ||
               strcasecmp(u, "hour") == 0 || strcasecmp(u, "hours") == 0) {
        mult = 3600ULL * 1000000000ULL;
    } else {
        err(errbuf, errlen, "unknown duration unit (ns, us, ms, s, min, hr, ...)");
        return -1;
    }

    acc = (unsigned long long)(v * (double)mult + 0.5);
    if (acc == 0 && v > 0) {
        acc = 1;
    }
    *out_ns = acc;
    return 0;
}
