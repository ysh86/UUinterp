#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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
        fprintf(stderr, "Usage: uuinterp rootdir aout\n");
        return EXIT_FAILURE;
    }

    machine_t machine;
    machine.dirfd = -1;
    machine.dirp = NULL;

    //////////////////////////
    // env
    //////////////////////////
    // skip vm cmd
    argv++;
    argc--;
    // root dir
    snprintf(machine.rootdir, sizeof(machine.rootdir), "%s", *argv);
    argv++;
    argc--;
    // cur dir
    snprintf(machine.curdir, sizeof(machine.curdir), "./");
    // aout
    if (!serializeArgvReal(&machine, argc, argv)) {
        fprintf(stderr, "/ [ERR] Too big argv\n");
        return EXIT_FAILURE;
    }
    if (!load(&machine, (const char *)machine.args)) {
        fprintf(stderr, "/ [ERR] Can't load file\n");
        return EXIT_FAILURE;
    }

    //////////////////////////
    // memory
    //////////////////////////
    reloaded:
    if (!IS_MAGIC_BE(machine.aout.headerBE[0])) {
        // PDP-11 V6
        machine.textStart = 0;
        machine.textEnd = machine.textStart + machine.aout.header[1];
        machine.dataStart = machine.textEnd;
        if (machine.aout.header[0] == 0x0108) {
            // 8KB alignment
            machine.dataStart = (machine.dataStart + 0x1fff) & ~0x1fff;
            memmove(&machine.virtualMemory[machine.dataStart], &machine.virtualMemory[machine.textEnd], machine.aout.header[2]);
        }
        machine.dataEnd = machine.dataStart + machine.aout.header[2];
        machine.bssStart = machine.dataEnd;
        machine.bssEnd = machine.bssStart + machine.aout.header[3];
        machine.brk = machine.bssEnd;

        assert(machine.aout.header[0] == 0x0107 || machine.aout.header[0] == 0x0108);
        assert(machine.aout.header[1] > 0);
        assert(machine.bssEnd <= 0xfffe);
        // TODO: validate other fields

        printf("/ aout header (PDP-11 V6)\n");
        printf("/\n");
        printf("/ magic:     0x%04x\n", machine.aout.header[0]);
        printf("/ text size: 0x%04x\n", machine.aout.header[1]);
        printf("/ data size: 0x%04x\n", machine.aout.header[2]);
        printf("/ bss  size: 0x%04x\n", machine.aout.header[3]);
        printf("/ symbol:    0x%04x\n", machine.aout.header[4]);
        printf("/ entry:     0x%04x\n", machine.aout.header[5]);
        printf("/ unused:    0x%04x\n", machine.aout.header[6]);
        printf("/ flag:      0x%04x\n", machine.aout.header[7]);
        printf("/\n");

        // bss
        memset(&machine.virtualMemory[machine.bssStart], 0, machine.aout.header[3]);
    } else {
        // m68k Minix
        machine.textStart = SIZE_OF_VECTORS;
        if (machine.textStart != 0) {
            memmove(&machine.virtualMemory[machine.textStart], &machine.virtualMemory[0], sizeof(machine.virtualMemory)-machine.textStart);
            memset(&machine.virtualMemory[0], 0, machine.textStart); // clear vectors
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
        assert(machine.bssEnd <= 0xfffe);
        assert((machine.brk & 1) == 0);
        // TODO: validate other fields

        printf("/ aout header (m68k Minix)\n");
        printf("/\n");
        printf("/ magic:     0x%08x\n", ntohl(machine.aout.headerBE[0]));
        printf("/ header len:0x%08x\n", machine.aout.headerBE[1]);
        printf("/ text size: 0x%08x\n", machine.aout.headerBE[2]);
        printf("/ data size: 0x%08x\n", machine.aout.headerBE[3]);
        printf("/ bss  size: 0x%08x\n", machine.aout.headerBE[4]);
        printf("/ entry:     0x%08x\n", machine.aout.headerBE[5]);
        printf("/ total:     0x%08x\n", machine.aout.headerBE[6]);
        printf("/ symbol:    0x%08x\n", machine.aout.headerBE[7]);
        printf("/\n");
        printf("/ text: 0x%08x-0x%08x\n", machine.textStart, machine.textEnd);
        printf("/ data: 0x%08x-0x%08x\n", machine.dataStart, machine.dataEnd);
        printf("/ bss : 0x%08x-0x%08x\n", machine.bssStart, machine.bssEnd);
        printf("/ brk : 0x%08x-\n",       machine.brk);
        printf("/\n");

        // bss
        memmove(&machine.virtualMemory[machine.brk], &machine.virtualMemory[machine.bssStart], sizeof(machine.virtualMemory)-machine.brk);
        memset(&machine.virtualMemory[machine.bssStart], 0, machine.aout.headerBE[4]);

        // relocate
        const int32_t offset = machine.textStart;
        uint8_t *paddrs = &machine.virtualMemory[machine.brk];
        int32_t addr = ntohl(*(uint32_t *)paddrs);
        paddrs += 4;
        if (offset != 0 && addr != 0) {
            addr += offset;

            while (1) {
                assert(addr <= 0x0000fffe);
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
        (syscall_t)mysyscall,
        (syscall_string_t)syscallString,
        0, machine.textStart);

    pushArgs(&cpu, machine.argc, machine.args, machine.argsbytes);
#if 1
    // debug dump
    FILE *fp = fopen("dump.bin", "wb");
    fwrite(machine.virtualMemory, 1, sizeof(machine.virtualMemory), fp);
    fclose(fp);

    printf("/ argc: %d\n", argc);
    for (int i = 0; i < argc; i++) {
        const char *pa = argv[i];
        printf("/ argv[%d]: %s\n", i, pa);
    }
    printf("\n");
    printf("/ pc: %08x\n", cpu.pc);
    for (int j = 0; j < 256; j += 16) {
        printf("/ %04x:", j);
        for (int i = 0; i < 16; i++) {
            printf(" %02x", machine.virtualMemory[j + i]);
        }
        printf("\n");
    }
    printf("\n");
    for (int j = cpu.pc; j < cpu.pc+256; j += 16) {
        printf("/ %04x:", j);
        for (int i = 0; i < 16; i++) {
            printf(" %02x", machine.virtualMemory[j + i]);
        }
        printf("\n");
    }
    printf("\n");
    printf("/ stack: sp = %08x\n", cpu.sp);
    int maxj = sizeof(machine.virtualMemory);
    for (int j = maxj - 256; j < maxj; j += 16) {
        printf("/ %04x:", j);
        for (int i = 0; i < 16; i++) {
            printf(" %02x", machine.virtualMemory[j + i]);
        }
        printf("\n");
    }
    printf("\n");
#endif

    while (1) {
        // TODO: debug
        //assert(cpu.pc < machine.textEnd);
        if (cpu.pc >= 0xffff) {
            break;
        }

        fetch(&cpu);
        decode(&cpu);
        exec(&cpu);

#if 1
        disasm(&cpu);
#endif
    }
    goto reloaded;

    // never reach
    assert(0);
    return EXIT_FAILURE;
}
