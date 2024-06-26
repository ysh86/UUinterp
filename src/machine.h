#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <assert.h>

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
    int envc;
    uint8_t args[512+4096];
    size_t argsbytes;

    // aout
    union {
        uint16_t header[8];
        uint32_t headerBE[8];
    } aout;

    // memory
    uint8_t virtualMemory[1024 * 1024];
    size_t sizeOfVM;
    uint32_t textStart;
    uint32_t textEnd;
    uint32_t dataStart;
    uint32_t dataEnd;
    uint32_t bssStart;
    uint32_t bssEnd;
    uint32_t brk;

    // cpu
    cpu_t *cpu;
};
#ifndef _MACHINE_T_
#define _MACHINE_T_
typedef struct machine_tag machine_t;
#endif

bool serializeArgvReal(machine_t *pm, int argc, char *argv[]);
int serializeArgvVirt16(machine_t *pm, uint8_t *argv);
int serializeArgvVirt(machine_t *pm, uint32_t vaddr);

int load(machine_t *pm, const char *src);

uint16_t pushArgs16(machine_t *pm, uint16_t stackAddr);
uint32_t pushArgs(machine_t *pm, uint32_t stackAddr);


// MMU
static inline uint8_t *mmuV2R(machine_t *pm, uint32_t vaddr) {
    assert(vaddr <= sizeof(pm->virtualMemory));
    return &pm->virtualMemory[vaddr];
}
static inline uint32_t mmuR2V(machine_t *pm, uint8_t *raddr) {
    ptrdiff_t vaddr = raddr - pm->virtualMemory;
    assert(vaddr <= sizeof(pm->virtualMemory));
    return vaddr;
}

// 16-bit LE
static inline uint16_t read16(const uint8_t *p) {
    return p[0] | (p[1] << 8);
}
static inline void write16(uint8_t *p, uint16_t data) {
    p[0] = data & 0xff;
    p[1] = data >> 8;
}

// TODO: virtual memory page をまたぐと動かない

// 32-bit BE
static inline uint32_t read32(uint8_t *p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}
static inline void write32(uint8_t *p, uint32_t data) {
    p[0] = data >> 24;
    p[1] = (data >> 16) & 0xff;
    p[2] = (data >> 8) & 0xff;
    p[3] = data & 0xff;
}


// for debug
void coreDump(machine_t *pm, const char *path);
