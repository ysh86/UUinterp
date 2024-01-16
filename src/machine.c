#include <stdio.h>
#include <assert.h>

#include <errno.h>

#define DEBUG_LOG 0

#include "machine.h"
#include "util.h"

bool serializeArgvReal(machine_t *pm, int argc, char *argv[]) {
    assert(argv[argc] == NULL);

    // args
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

    // envs
    int ne = 0;

    pm->argc = argc;
    pm->envc = ne;
    pm->argsbytes = nc;
    return true;
}

int serializeArgvVirt16(machine_t *pm, uint8_t *argv) {
    uint16_t na = 0;
    uint16_t nc = 0;

    uint16_t vaddr = read16(argv);
    argv += 2;
    while (vaddr != 0) {
        const char *pa = (const char *)&pm->virtualMemory[vaddr];

        vaddr = read16(argv);
        argv += 2;
        na++;

        do {
            pm->args[nc++] = *pa;
            if (nc >= sizeof(pm->args) - 1) {
                return -1;
            }
        } while (*pa++ != '\0');
    }
    if (nc & 1) {
        pm->args[nc++] = '\0';
    }

    pm->argc = na;
    pm->envc = 0;
    pm->argsbytes = nc;
    return 0;
}

// TODO: virtual memory page をまたぐと動かない
int serializeArgvVirt(machine_t *pm, uint32_t vaddr) {
    uint32_t na = 0;
    uint32_t ne = 0;
    uint32_t nc = 0;

    uint32_t vp = vaddr;
    uint32_t argc = read32(mmuV2R(pm, vp));
    vp += 4;

    // args
    uint32_t varg = vaddr + read32(mmuV2R(pm, vp));
    vp += 4;
    while (varg > vaddr) {
        const char *pa = (const char *)mmuV2R(pm, varg);

        varg = vaddr + read32(mmuV2R(pm, vp));
        vp += 4;
        na++;

#if DEBUG_LOG
        fprintf(stderr, "/ [DBG] varg=%08x: ", mmuR2V(pm, (uint8_t *)pa));
#endif
        do {
#if DEBUG_LOG
            if (*pa != '\0') fprintf(stderr, "%c", *pa);
#endif
            pm->args[nc++] = *pa;
            if (nc >= sizeof(pm->args) - 1) {
                return -1;
            }
        } while (*pa++ != '\0');
#if DEBUG_LOG
        fprintf(stderr, "\n");
#endif
    }
    if (nc & 1) {
        pm->args[nc++] = '\0';
    }

    // envs
    uint32_t venv = vaddr + read32(mmuV2R(pm, vp));
    vp += 4;
    while (venv > vaddr) {
        const char *pa = (const char *)mmuV2R(pm, venv);

        venv = vaddr + read32(mmuV2R(pm, vp));
        vp += 4;
        ne++;

#if DEBUG_LOG
        fprintf(stderr, "/ [DBG] venv=%08x: ", mmuR2V(pm, (uint8_t *)pa));
#endif
        do {
#if DEBUG_LOG
            if (*pa != '\0') fprintf(stderr, "%c", *pa);
#endif
            pm->args[nc++] = *pa;
            if (nc >= sizeof(pm->args) - 1) {
                return -1;
            }
        } while (*pa++ != '\0');
#if DEBUG_LOG
        fprintf(stderr, "\n");
#endif
    }
    if (nc & 1) {
        pm->args[nc++] = '\0';
    }

    assert(na == argc);
    pm->argc = na;
    pm->envc = ne;
    pm->argsbytes = nc;
    return 0;
}

int load(machine_t *pm, const char *src) {
    char name[PATH_MAX];
    addroot(name, sizeof(name), src, pm->rootdir);

    FILE *fp;
    fp = fopen(name, "rb");
    if (fp == NULL) {
        return errno;
    }

    size_t n;
    size_t size;
    size = sizeof(pm->aout.header);
    n = fread(pm->aout.header, 1, size, fp);
    if (n != size) {
        fclose(fp);
        return ENOEXEC;
    }

    if (!IS_MAGIC_BE(pm->aout.headerBE[0])) {
        // PDP-11 V6
        // TODO: endian
    } else {
        // m68k Minix
        n = fread(&pm->aout.headerBE[4], 1, size, fp);
        if (n != size) {
            fclose(fp);
            return ENOEXEC;
        }

        pm->aout.headerBE[1] = ntohl(pm->aout.headerBE[1]) & 0xff;
        pm->aout.headerBE[2] = ntohl(pm->aout.headerBE[2]);
        pm->aout.headerBE[3] = ntohl(pm->aout.headerBE[3]);
        pm->aout.headerBE[4] = ntohl(pm->aout.headerBE[4]);
        pm->aout.headerBE[5] = ntohl(pm->aout.headerBE[5]);
        pm->aout.headerBE[6] = ntohl(pm->aout.headerBE[6]);
        pm->aout.headerBE[7] = ntohl(pm->aout.headerBE[7]);
    }

    size = sizeof(pm->virtualMemory) - pm->textStart;
    n = fread(&pm->virtualMemory[pm->textStart], 1, size, fp);
    if (n <= 0) {
        fclose(fp);
        return ENOEXEC;
    }
    fclose(fp);
    fp = NULL;

    return 0;
}

uint16_t pushArgs16(machine_t *pm, uint16_t stackAddr) {
    // argc, argv[0]...argv[na-1], -1, buf
    assert(pm->envc == 0);
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

// TODO: virtual memory page をまたぐと動かない
uint32_t pushArgs(machine_t *pm, uint32_t stackAddr) {
    // argc, argv[0]...argv[na-1], NULL, envp[0]...envp[ne-1], NULL, buf
    const uint32_t na = pm->argc;
    const uint32_t ne = pm->envc;
    const uint32_t vsp = stackAddr - (4 + na * 4 + 4 + ne * 4 + 4 + pm->argsbytes);
    uint8_t *rsp = mmuV2R(pm, vsp);
    uint8_t *pbuf = rsp + 4 + na * 4 + 4 + ne * 4 + 4;

    // argc
    write32(rsp, na);
    rsp += 4;

    const uint8_t *pa = pm->args;
    uint32_t vaddr = mmuR2V(pm, pbuf);

    // argv & buf
    for (int i = 0; i < na; i++) {
        write32(rsp, vaddr);
        rsp += 4;
        do {
            *pbuf++ = *pa;
        } while (*pa++ != '\0');
        vaddr = mmuR2V(pm, pbuf);
    }
    if (vaddr & 1) {
        *pbuf++ = '\0'; // alignment
        vaddr = mmuR2V(pm, pbuf);
    }
    // NULL
    write32(rsp, (uintptr_t)NULL);
    rsp += 4;

    // envp & buf
    for (int i = 0; i < ne; i++) {
        write32(rsp, vaddr);
        rsp += 4;
        do {
            *pbuf++ = *pa;
        } while (*pa++ != '\0');
        vaddr = mmuR2V(pm, pbuf);
    }
    if (vaddr & 1) {
        *pbuf++ = '\0'; // alignment
        vaddr = mmuR2V(pm, pbuf);
    }
    // NULL
    write32(rsp, (uintptr_t)NULL);
    rsp += 4;

    return vsp;
}
