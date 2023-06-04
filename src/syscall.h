#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct machine_tag;
#ifndef _MACHINE_T_
#define _MACHINE_T_
typedef struct machine_tag machine_t;
#endif

bool serializeArgvReal(machine_t *pm, int argc, char *argv[]);
void mysyscall(machine_t *pm);
void syscallString(machine_t *pm, char *str, size_t size, uint8_t id);
