#include <stdio.h>

#include "machine.h"
#include "util.h"

bool load(machine_t *pm, const char *src) {
    char name[PATH_MAX];
    addroot(name, sizeof(name), src, pm->rootdir);
    printf("\n/ loading: %s (orig: %s)\n", name, src);

    FILE *fp;
    fp = fopen(name, "rb");
    if (fp == NULL) {
        perror("/ [ERR] machine::load()");
        return false;
    }

    size_t n;
    size_t size;
    size = sizeof(pm->aoutHeader);
    n = fread(pm->aoutHeader, 1, size, fp);
    if (n != size) {
        fclose(fp);
        return false;
    }
    // TODO: endian

    size = sizeof(pm->virtualMemory);
    n = fread(pm->virtualMemory, 1, size, fp);
    if (n <= 0) {
        fclose(fp);
        return false;
    }
    fclose(fp);
    fp = NULL;

    return true;
}
