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
    size = sizeof(pm->aout.header);
    n = fread(pm->aout.header, 1, size, fp);
    if (n != size) {
        fclose(fp);
        return false;
    }

    if (!IS_MAGIC_BE(pm->aout.headerBE[0])) {
        // PDP-11 V6
        // TODO: endian
    } else {
        // m68k Minix
        n = fread(&pm->aout.headerBE[4], 1, size, fp);
        if (n != size) {
            fclose(fp);
            return false;
        }

        pm->aout.headerBE[1] = ntohl(pm->aout.headerBE[1]) & 0xff;
        pm->aout.headerBE[2] = ntohl(pm->aout.headerBE[2]);
        pm->aout.headerBE[3] = ntohl(pm->aout.headerBE[3]);
        pm->aout.headerBE[4] = ntohl(pm->aout.headerBE[4]);
        pm->aout.headerBE[5] = ntohl(pm->aout.headerBE[5]);
        pm->aout.headerBE[6] = ntohl(pm->aout.headerBE[6]);
        pm->aout.headerBE[7] = ntohl(pm->aout.headerBE[7]);
    }

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
