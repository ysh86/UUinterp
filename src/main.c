#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "machine.h"
#include "../pdp11/src/cpu.h"
#include "syscall.h"

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
    machine.textStart = 0;
    machine.textEnd = machine.textStart + machine.aoutHeader[1];
    machine.dataStart = machine.textEnd;
    if (machine.aoutHeader[0] == 0x0108) {
        // 8KB alignment
        machine.dataStart = (machine.dataStart + 0x1fff) & ~0x1fff;
        memmove(&machine.virtualMemory[machine.dataStart], &machine.virtualMemory[machine.textEnd], machine.aoutHeader[2]);
    }
    machine.dataEnd = machine.dataStart + machine.aoutHeader[2];
    machine.bssStart = machine.dataEnd;
    machine.bssEnd = machine.bssStart + machine.aoutHeader[3];
    machine.brk = machine.bssEnd;

    assert(machine.aoutHeader[0] == 0x0107 || machine.aoutHeader[0] == 0x0108);
    assert(machine.aoutHeader[1] > 0);
    assert(machine.bssEnd <= 0xfffe);
    // TODO: validate other fields

    printf("/ aout header\n");
    printf("/\n");
    printf("/ magic:     0x%04x\n", machine.aoutHeader[0]);
    printf("/ text size: 0x%04x\n", machine.aoutHeader[1]);
    printf("/ data size: 0x%04x\n", machine.aoutHeader[2]);
    printf("/ bss  size: 0x%04x\n", machine.aoutHeader[3]);
    printf("/ symbol:    0x%04x\n", machine.aoutHeader[4]);
    printf("/ entry:     0x%04x\n", machine.aoutHeader[5]);
    printf("/ unused:    0x%04x\n", machine.aoutHeader[6]);
    printf("/ flag:      0x%04x\n", machine.aoutHeader[7]);
    printf("/\n");

    // bss
    memset(&machine.virtualMemory[machine.bssStart], 0, machine.aoutHeader[3]);

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
#if 0
    // debug dump
    for (int i = 0; i < argc; i++) {
        const char *pa = argv[i];
        printf("/ argv[%d]: %s\n", i, pa);
    }
    printf("\n");
    printf("/ stack: sp = %04x\n", cpu.sp);
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

        //disasm(&cpu);
    }
    goto reloaded;

    // never reach
    assert(0);
    return EXIT_FAILURE;
}
