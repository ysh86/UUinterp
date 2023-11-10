#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <arpa/inet.h>

// for PATH_MAX
#ifdef __linux__
#include <linux/limits.h>
#endif
#ifdef __APPLE__
#include <sys/syslimits.h>
#endif

struct cpu_tag;
#ifndef _CPU_T_
#define _CPU_T_
typedef struct cpu_tag cpu_t;
#endif

// aout
#define MAGIC_BE 0x04000301
#define IS_MAGIC_BE(X) ((ntohl(X) & 0xff0fffff) == MAGIC_BE)
#define IS_SEPARATE(X) (     ((X) & 0x00200000) ? 0x20 : 0)

struct machine_tag {
    // emulate syscall opendir, closedir and readdir
    int dirfd;
    DIR *dirp;

    // env
    char rootdir[PATH_MAX];
    char curdir[PATH_MAX];
    int argc;
    uint8_t args[512];
    size_t argsbytes;

    // aout
    union {
        uint16_t header[8];
        uint32_t headerBE[8];
    } aout;

    // memory
    uint8_t virtualMemory[64 * 1024];
    uint16_t textStart;
    uint16_t textEnd;
    uint16_t dataStart;
    uint16_t dataEnd;
    uint16_t bssStart;
    uint16_t bssEnd;
    uint16_t brk;

    // cpu
    cpu_t *cpu;
};
#ifndef _MACHINE_T_
#define _MACHINE_T_
typedef struct machine_tag machine_t;
#endif

bool serializeArgvReal(machine_t *pm, int argc, char *argv[]);
bool load(machine_t *pm, const char *src);

uint16_t pushArgs16(machine_t *pm, uint16_t stackAddr);
uint32_t pushArgs(machine_t *pm, uint32_t stackAddr);

static inline uint8_t *mmuV2R(machine_t *pm, uint16_t vaddr) {
    //return (vaddr != 0) ? &pm->virtualMemory[vaddr] : NULL;
    return &pm->virtualMemory[vaddr];
}
static inline uint16_t mmuR2V(machine_t *pm, uint8_t *raddr) {
    //return (raddr != NULL) ? (raddr - pm->virtualMemory) & 0xffff : 0;
    return (raddr - pm->virtualMemory) & 0xffff;
}
