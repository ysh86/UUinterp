#pragma once

#include <stdint.h>
#include <stdio.h>

static inline void addroot(char *path, size_t len, const char *src, const char *rootdir) {
    if (src[0] == '/') {
        snprintf(path, len, "%s%s", rootdir, src);
    } else {
        snprintf(path, len, "%s", src);
    }
}
