#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

#define DEBUG_LOG 0

#include "machine.h"
#ifdef UU_M68K_MINIX
#include "../m68k/src/cpu.h"
#include "syscall.h"
#else
#include "../pdp11/src/cpu.h"
#include "syscall.h"
#endif

int main(int argc, char *argv[]) {
    //////////////////////////
    // usage
    //////////////////////////
    if (argc < 3) {
        fprintf(stderr, "Usage: uuinterp rootdir aout args...\n");
        return EXIT_FAILURE;
    }

    machine_t machine;
    machine.dirfd = -1;
    machine.dirp = NULL;
    machine.textStart = SIZE_OF_VECTORS;

    //////////////////////////
    // env
    //////////////////////////
    // skip vm cmd
    argv++;
    argc--;
    // cur dir
    {
        char *p = getcwd(machine.curdir, sizeof(machine.curdir));
        if (p == NULL) {
            fprintf(stderr, "%s\n", strerror(errno));
            return EXIT_FAILURE;
        }
    }
    // root dir
    {
        int ret = chdir(*argv);
        if (ret != 0) {
            fprintf(stderr, "%s: %s\n", strerror(errno), *argv);
            return EXIT_FAILURE;
        }
        char *p = getcwd(machine.rootdir, sizeof(machine.rootdir));
        if (p == NULL) {
            fprintf(stderr, "%s\n", strerror(errno));
            return EXIT_FAILURE;
        }
    }
    // return to cur dir
    {
        int ret = chdir(machine.curdir);
        if (ret != 0) {
            fprintf(stderr, "%s: %s\n", strerror(errno), machine.curdir);
            return EXIT_FAILURE;
        }
    }
    argv++;
    argc--;
    // aout
    if (!serializeArgvReal(&machine, argc, argv)) {
        fprintf(stderr, "/ [ERR] Too big argv\n");
        return EXIT_FAILURE;
    }
    int ret;
    if ((ret = load(&machine, (const char *)machine.args))) {
        fprintf(stderr, "/ [ERR] Can't load file \"%s\": %s\n", (const char *)machine.args, strerror(ret));
        return EXIT_FAILURE;
    }

    //////////////////////////
    // memory
    //////////////////////////
    uint32_t sp;
    reloaded:
    if (!IS_MAGIC_BE(machine.aout.headerBE[0])) {
        // PDP-11 V6
        machine.sizeOfVM = (sizeof(machine.virtualMemory) < 0x10000) ? sizeof(machine.virtualMemory) : 0x10000;

        assert(machine.textStart == 0); // vectors not implemented
        machine.textEnd = machine.textStart + machine.aout.header[1];
        machine.dataStart = machine.textEnd;
        if (machine.aout.header[0] == 0x0108) {
            // 8KB alignment
            machine.dataStart = (machine.dataStart + 0x1fff) & ~0x1fff;
            memmove(&machine.virtualMemory[machine.dataStart], &machine.virtualMemory[machine.textEnd], machine.sizeOfVM-machine.dataStart);
        }
        machine.dataEnd = machine.dataStart + machine.aout.header[2];
        machine.bssStart = machine.dataEnd;
        machine.bssEnd = machine.bssStart + machine.aout.header[3];
        machine.brk = machine.bssEnd;

        assert(machine.aout.header[0] == 0x0107 || machine.aout.header[0] == 0x0108);
        assert(machine.aout.header[1] > 0);
        assert(machine.bssEnd <= machine.sizeOfVM - 2);
        // TODO: validate other fields

#if DEBUG_LOG
        fprintf(stderr, "/ pid %d: ready\n", getpid());
        fprintf(stderr, "/ load: %s (root: %s)\n", (const char *)machine.args, machine.rootdir);
        fprintf(stderr, "/ aout header (PDP-11 V6)\n");
        fprintf(stderr, "/\n");
        fprintf(stderr, "/ magic:     0x%04x\n", machine.aout.header[0]);
        fprintf(stderr, "/ text size: 0x%04x\n", machine.aout.header[1]);
        fprintf(stderr, "/ data size: 0x%04x\n", machine.aout.header[2]);
        fprintf(stderr, "/ bss  size: 0x%04x\n", machine.aout.header[3]);
        fprintf(stderr, "/ symbol:    0x%04x\n", machine.aout.header[4]);
        fprintf(stderr, "/ entry:     0x%04x\n", machine.aout.header[5]);
        fprintf(stderr, "/ unused:    0x%04x\n", machine.aout.header[6]);
        fprintf(stderr, "/ flag:      0x%04x\n", machine.aout.header[7]);
        fprintf(stderr, "\n");
#endif

        // bss
        memset(&machine.virtualMemory[machine.bssStart], 0, machine.aout.header[3]);

        // stack
        sp = pushArgs16(&machine, 0);
    } else {
        // m68k Minix
        machine.sizeOfVM = sizeof(machine.virtualMemory);

        if (machine.textStart != 0) {
            // clear vectors
            memset(&machine.virtualMemory[0], 0, machine.textStart);
        }
        machine.textEnd = machine.textStart + machine.aout.headerBE[2];
        if (IS_SEPARATE(machine.aout.headerBE[0])) {
            machine.dataStart = machine.textEnd;
            machine.dataEnd = machine.dataStart + machine.aout.headerBE[3];
        } else {
            // treat text as data
            machine.dataStart = machine.textStart;
            machine.dataEnd = machine.textEnd + machine.aout.headerBE[3];
        }
        machine.bssStart = machine.dataEnd;
        machine.bssEnd = machine.bssStart + machine.aout.headerBE[4];
        machine.brk = machine.bssEnd;

        assert(machine.aout.headerBE[1] == 32);
        assert(machine.aout.headerBE[2] > 0);
        assert(machine.bssEnd <= machine.sizeOfVM - 2);
        assert((machine.brk & 1) == 0);
        // TODO: validate other fields

#if DEBUG_LOG
        fprintf(stderr, "/ pid %d: ready\n", getpid());
        fprintf(stderr, "/ load: %s (root: %s)\n", (const char *)machine.args, machine.rootdir);
        fprintf(stderr, "/ aout header (m68k Minix)\n");
        fprintf(stderr, "/\n");
        fprintf(stderr, "/ magic:     0x%08x\n", ntohl(machine.aout.headerBE[0]));
        fprintf(stderr, "/ header len:0x%08x\n", machine.aout.headerBE[1]);
        fprintf(stderr, "/ text size: 0x%08x\n", machine.aout.headerBE[2]);
        fprintf(stderr, "/ data size: 0x%08x\n", machine.aout.headerBE[3]);
        fprintf(stderr, "/ bss  size: 0x%08x\n", machine.aout.headerBE[4]);
        fprintf(stderr, "/ entry:     0x%08x\n", machine.aout.headerBE[5]);
        fprintf(stderr, "/ total:     0x%08x\n", machine.aout.headerBE[6]);
        fprintf(stderr, "/ symbol:    0x%08x\n", machine.aout.headerBE[7]);
        fprintf(stderr, "/\n");
        fprintf(stderr, "/ text: 0x%08x-0x%08x\n", machine.textStart, machine.textEnd);
        fprintf(stderr, "/ data: 0x%08x-0x%08x\n", machine.dataStart, machine.dataEnd);
        fprintf(stderr, "/ bss : 0x%08x-0x%08x\n", machine.bssStart, machine.bssEnd);
        fprintf(stderr, "/ brk : 0x%08x-\n",       machine.brk);
        fprintf(stderr, "\n");
#endif

        // move relocate table
        memmove(&machine.virtualMemory[machine.brk], &machine.virtualMemory[machine.bssStart], machine.sizeOfVM-machine.brk);
        // bss
        memset(&machine.virtualMemory[machine.bssStart], 0, machine.aout.headerBE[4]);

        // relocate
        const int32_t entry = machine.aout.headerBE[5];
        const int32_t offset = machine.textStart;
        uint8_t *paddrs = &machine.virtualMemory[machine.brk];
        int32_t addr = ntohl(*(uint32_t *)paddrs);
        paddrs += 4;
        if (offset != entry && addr != 0) {
            addr += offset;

            while (1) {
                assert(addr <= machine.sizeOfVM - 4);
                assert(addr < machine.dataEnd);

                int32_t opland = ntohl(*(int32_t *)&machine.virtualMemory[addr]);
                *(int32_t *)&machine.virtualMemory[addr] = htonl(opland + offset);

                uint8_t B;
                while((B = *paddrs++) == 1) {
                    addr += 254;
                }
                if (B == 0) {
                    break;
                }
                assert((B & 1) == 0);
                addr += B;
            }
        }

        // stack
        sp = pushArgs(&machine, machine.sizeOfVM);
    }

    //////////////////////////
    // cpu
    //////////////////////////
    cpu_t cpu;
    machine.cpu = &cpu;

    //////////////////////////
    // run
    //////////////////////////
    init(
        &cpu,
        &machine,
        (mmu_v2r_t)mmuV2R,
        (mmu_r2v_t)mmuR2V,
        (syscall_t)mysyscall16,
        (syscall_string_t)syscallString16,
        sp, machine.textStart);
#if DEBUG_LOG
    {
        // core dump
        char dumpPath[PATH_MAX];
        sprintf(dumpPath, "core%06d.bin", getpid());
        coreDump(&machine, dumpPath);

        // args
        const char *pa = (const char *)&machine.args[0];
        fprintf(stderr, "/ argc: %d\n", machine.argc);
        for (int i = 0; i < machine.argc; i++) {
            fprintf(stderr, "/ argv[%d]: %s\n", i, pa);
            pa += strlen(pa) + 1;
        }
        fprintf(stderr, "/ \n");
        while (*pa == '\0') ++pa;
        fprintf(stderr, "/ envc: %d\n", machine.envc);
        for (int i = 0; i < machine.envc; i++) {
            fprintf(stderr, "/ envp[%d]: %s\n", i, pa);
            pa += strlen(pa) + 1;
        }
        fprintf(stderr, "/ \n");

#if 0
        const uint32_t pc = getPC(&cpu);
        fprintf(stderr, "/ pc: %08x\n", pc);
        for (int j = 0; j < 256; j += 16) {
            fprintf(stderr, "/ %04x:", j);
            for (int i = 0; i < 16; i++) {
                if (i == 8) fprintf(stderr, " ");
                fprintf(stderr, " %02x", machine.virtualMemory[j + i]);
            }
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "/ \n");
        for (int j = pc; j < pc+256; j += 16) {
            fprintf(stderr, "/ %04x:", j);
            for (int i = 0; i < 16; i++) {
                if (i == 8) fprintf(stderr, " ");
                fprintf(stderr, " %02x", machine.virtualMemory[j + i]);
            }
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "/ \n");
#endif
        const uint32_t sp = getSP(&cpu);
        fprintf(stderr, "/ stack: sp = %08x\n", sp);
        int maxj = machine.sizeOfVM;
        for (int j = maxj - 256; j < maxj; j += 16) {
            fprintf(stderr, "/ %04x:", j);
            for (int i = 0; i < 16; i++) {
                if (i == 8) fprintf(stderr, " ");
                fprintf(stderr, " %02x", machine.virtualMemory[j + i]);
            }
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "\n");
    }
#endif

    const uint32_t eom = machine.sizeOfVM - 1;
    while (1) {
        const uint32_t pc = getPC(&cpu);
        // TODO: debug
        //assert(pc < machine.textEnd);
        if (pc >= eom) {
#if DEBUG_LOG
            fprintf(stderr, "/ pid %d: pc:%08x >= eom:%08x\n", getpid(), pc, eom);
#endif
            break;
        }

        fetch(&cpu);
        decode(&cpu);
#if 0
        fprintf(stderr, "/ pid %d: ", getpid());
        disasm(&cpu);
#endif

        exec(&cpu);
    }
    goto reloaded;

    // never reach
    assert(0);
    return EXIT_FAILURE;
}
