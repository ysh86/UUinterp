#include <stdio.h>
#include <assert.h>

#include "machine.h"
#include "util.h"

bool serializeArgvReal(machine_t *pm, int argc, char *argv[]) {
    assert(argv[argc] == NULL);

    size_t nc = 0;
    for (int i = 0; i < argc; i++) {
        const char *pa = argv[i];
        do {
            pm->args[nc++] = *pa;
            if (nc >= sizeof(pm->args) - 1) {
                return false;
            }
        } while (*pa++ != '\0');
    }
    if (nc & 1) {
        pm->args[nc++] = '\0';
    }

    pm->argc = argc;
    pm->argsbytes = nc;
    return true;
}

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

// 16-bit LE
static inline void write16(uint8_t *p, uint16_t data) {
    p[0] = data & 0xff;
    p[1] = data >> 8;
}

uint16_t pushArgs16(machine_t *pm, uint16_t stackAddr) {
    // argc, argv[0]...argv[na-1], -1, buf
    const uint16_t na = pm->argc;
    const uint16_t vsp = stackAddr - (2 + na * 2 + 2 + pm->argsbytes);
    uint8_t *rsp = mmuV2R(pm, vsp);
    uint8_t *pbuf = rsp + 2 + na * 2 + 2;

    // argc
    write16(rsp, na);
    rsp += 2;

    // argv & buf
    const uint8_t *pa = pm->args;
    for (int i = 0; i < na; i++) {
        uint16_t vaddr = mmuR2V(pm, pbuf);
        write16(rsp, vaddr);
        rsp += 2;
        do {
            *pbuf++ = *pa;
        } while (*pa++ != '\0');
    }

    uint16_t vaddr = mmuR2V(pm, pbuf);
    if (vaddr & 1) {
        *pbuf = '\0'; // alignment
    }

    // -1
    write16(rsp, 0xffff);

    return vsp;
}

// 32-bit BE
static inline void write32(uint8_t *p, uint32_t data) {
    p[0] = data >> 24;
    p[1] = (data >> 16) & 0xff;
    p[2] = (data >> 8) & 0xff;
    p[3] = data & 0xff;
}

static inline void writeAddr16(uint8_t *p, uint16_t vaddr) {
    p[0] = 0;
    p[1] = 0;
    p[2] = vaddr >> 8;
    p[3] = vaddr & 0xff;
}

// TODO: 32bit
// TODO: virtual memory page をまたぐと動かない
uint32_t pushArgs(machine_t *pm, uint32_t stackAddr) {
    // argc, argv[0]...argv[na-1], -1, buf
    const uint32_t na = pm->argc;
    const uint16_t vsp = stackAddr - (4 + na * 4 + 4 + pm->argsbytes);
    uint8_t *rsp = mmuV2R(pm, vsp);
    uint8_t *pbuf = rsp + 4 + na * 4 + 4;

    // argc
    write32(rsp, na);
    rsp += 4;

    // argv & buf
    const uint8_t *pa = pm->args;
    for (int i = 0; i < na; i++) {
        uint16_t vaddr = mmuR2V(pm, pbuf);
        writeAddr16(rsp, vaddr);
        rsp += 4;
        do {
            *pbuf++ = *pa;
        } while (*pa++ != '\0');
    }

    uint16_t vaddr = mmuR2V(pm, pbuf);
    if (vaddr & 1) {
        *pbuf = '\0'; // alignment
    }

    // -1
    write32(rsp, 0xffffffff);

    return vsp;
}
