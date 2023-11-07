#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/times.h>

#include "syscall.h"
#include "machine.h"
#include "../pdp11/src/cpu.h"
#include "../pdp11/src/util.h"
#include "util.h"

static int serializeArgvVirt(machine_t *pm, uint8_t *argv) {
    uint16_t na = 0;
    uint16_t nc = 0;

    uint16_t vaddr = read16(false, argv);
    argv += 2;
    while (vaddr != 0) {
        const char *pa = (const char *)&pm->virtualMemory[vaddr];
        // debug
        //fprintf(stderr, "/ [DBG]   argv[%d]: %s\n", na, pa);
        vaddr = read16(false, argv);
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
    pm->argsbytes = nc;
    return 0;
}

static void convstat(uint8_t *pi, const struct stat* ps) {
    struct inode {
        char  minor;         /* +0: minor device of i-node */
        char  major;         /* +1: major device */
        int   inumber;       /* +2 */
        int   flags;         /* +4: see below */
        char  nlinks;        /* +6: number of links to file */
        char  uid;           /* +7: user ID of owner */
        char  gid;           /* +8: group ID of owner */
        char  size0;         /* +9: high byte of 24-bit size */
        int   size1;         /* +10: low word of 24-bit size */
        int   addr[8];       /* +12: block numbers or device number */
        int   actime[2];     /* +28: time of last access */
        int   modtime[2];    /* +32: time of last modification */
    };
    /* flags
    100000   i-node is allocated
    060000   2-bit file type:
        000000   plain file
        040000   directory
        020000   character-type special file
        060000   block-type special file.
    010000   large file
    004000   set user-ID on execution
    002000   set group-ID on execution
    001000   save text image after execution
    000400   read (owner)
    000200   write (owner)
    000100   execute (owner)
    000070   read, write, execute (group)
    000007   read, write, execute (others)
    */
    pi[0] = ps->st_dev & 0xff; // pseudo
    pi[1] = (ps->st_dev >> 8) & 0xff; // pseudo
    pi[2] = ps->st_ino & 0xff;
    pi[3] = (ps->st_ino >> 8) & 0xff;
    pi[4] = ps->st_mode & 0xff;
    pi[5] = (ps->st_mode >> 8) & 0xff;
    pi[6] = ps->st_nlink & 0xff;
    pi[7] = ps->st_uid & 0xff;
    pi[8] = ps->st_gid & 0xff;
    pi[9] = (ps->st_size >> 16) & 0xff;
    pi[10] = ps->st_size & 0xff;
    pi[11] = (ps->st_size >> 8) & 0xff;
    // addr
    //pi[12];
    // actime
    //pi[28];
    //pi[29];
    //pi[30];
    //pi[31];
    // modtime
    //pi[32];
    //pi[33];
    //pi[34];
    //pi[35];
}

void mysyscall(machine_t *pm) {
    uint16_t addr;

    uint16_t word0 = 0;
    uint16_t word1 = 0;
    char path0[PATH_MAX];
    char path1[PATH_MAX];
    ssize_t sret;
    int ret;

    //fprintf(stderr, "/ [DBG] sys %d, %04x: %04x\n", pm->cpu->syscallID, pm->cpu->addr, pm->cpu->bin);
    switch (pm->cpu->syscallID) {
    case 0:
        // indir
        addr = fetch(pm->cpu);

        uint16_t oldpc = pm->cpu->pc;
        {
            pm->cpu->pc = addr;
            pm->cpu->bin = fetch(pm->cpu);
            pm->cpu->syscallID = pm->cpu->bin & 0x3f;
            assert(pm->cpu->bin - pm->cpu->syscallID == 0104400);
            mysyscall(pm);
        }
        // syscall exec(11) overwrites pc!
        if (pm->cpu->syscallID == 11) {
            if (pm->cpu->pc != 0xffff) {
                pm->cpu->pc = oldpc;
                assert(isC(pm->cpu));
            }
        } else {
            pm->cpu->pc = oldpc;
        }
        // TODO: In syscall fork(2) parent overwrites pc!
        assert(pm->cpu->syscallID != 2);
        break;
    case 1:
        // exit
        _exit((int16_t)pm->cpu->r0);
        break;
    case 2:
        // fork
        ret = fork();
        if (ret < 0) {
            pm->cpu->r0 = errno & 0xffff;
            setC(pm->cpu); // error bit
        } else {
            if (ret == 0) {
                // child
                // nothing to do
            } else {
                // parent
                pm->cpu->pc += 2;
            }
            //fprintf(stderr, "/ [DBG] fork pid: %d (pc: %04x)\n", ret, pm->cpu->pc);
            pm->cpu->r0 = ret & 0xffff;
            clearC(pm->cpu);
        }
        break;
    case 3:
        // read
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        if (pm->dirfd != -1 && pm->dirp != NULL && (int16_t)pm->cpu->r0 == pm->dirfd) {
            // dir
            struct dirent *ent;
            ent = readdir(pm->dirp);
            if (ent == NULL) {
                if (errno == 0) {
                    // EOF
                    sret = 0;
                } else {
                    // error
                    sret = -1;
                }
            } else {
                assert(word1 == 16);
                uint8_t *p = &pm->virtualMemory[word0];
                // ino
                p[0] = ent->d_ino & 0xff;
                p[1] = (ent->d_ino >> 8) & 0xff;
                // name
                // TODO: Which is better?
#if 1
                strncpy((char *)&p[2], ent->d_name, 16 - 2);
#else
                strncpy((char *)&p[2], ent->d_name, 16 - 2 - 1);
                p[15] = '\0';
#endif
                sret = word1;
            }
        } else {
            // file
            sret = read((int16_t)pm->cpu->r0, &pm->virtualMemory[word0], word1);
        }
        if (sret < 0) {
            pm->cpu->r0 = errno & 0xffff;
            setC(pm->cpu); // error bit
        } else {
            pm->cpu->r0 = sret & 0xffff;
            clearC(pm->cpu);
        }
        break;
    case 4:
        // write
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        sret = write((int16_t)pm->cpu->r0, &pm->virtualMemory[word0], word1);
        if (sret < 0) {
            pm->cpu->r0 = errno & 0xffff;
            setC(pm->cpu); // error bit
        } else {
            pm->cpu->r0 = sret & 0xffff;
            clearC(pm->cpu);
        }
        break;
    case 5:
        // open
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        addroot(path0, sizeof(path0), (const char *)&pm->virtualMemory[word0], pm->rootdir);
        // debug
        //fprintf(stderr, "/ [DBG] sys open; %04x; %06o\n", word0, word1);
        //fprintf(stderr, "/   %s, %s\n", (const char *)&pm->virtualMemory[word0], pm->rootdir);
        //fprintf(stderr, "/   %s\n", path0);
        ret = open(path0, word1);
        if (ret < 0) {
            pm->cpu->r0 = errno & 0xffff;
            setC(pm->cpu); // error bit
        } else {
            pm->cpu->r0 = ret & 0xffff;
            clearC(pm->cpu);

            // check file or dir
            int fd = ret;
            struct stat s;
            ret = fstat(fd, &s);
            if (ret == 0 && S_ISDIR(s.st_mode)) {
                // dir
                DIR *dirp = fdopendir(fd);
                if (dirp == NULL) {
                    pm->cpu->r0 = errno & 0xffff;
                    setC(pm->cpu); // error bit
                    close(fd);
                } else {
                    // TODO: support only one dir per process, currently
                    assert(pm->dirfd == -1);
                    assert(pm->dirp == NULL);
                    pm->dirfd = fd;
                    pm->dirp = dirp;
                }
            }
        }
        break;
    case 6:
        // close
        if (pm->dirfd != -1 && pm->dirp != NULL && (int16_t)pm->cpu->r0 == pm->dirfd) {
            // dir
            ret = closedir(pm->dirp);
            pm->dirfd = -1;
            pm->dirp = NULL;
        } else {
            // file
            ret = close((int16_t)pm->cpu->r0);
        }
        if (ret < 0) {
            pm->cpu->r0 = errno & 0xffff;
            setC(pm->cpu); // error bit
        } else {
            pm->cpu->r0 = ret & 0xffff;
            clearC(pm->cpu);
        }
        break;
    case 7:
        // wait
        {
            int status;
            ret = wait(&status);
            if (ret < 0) {
                pm->cpu->r0 = errno & 0xffff;
                setC(pm->cpu); // error bit
            } else {
                pm->cpu->r0 = ret & 0xffff;
                pm->cpu->r1 = status & 0xffff;
                clearC(pm->cpu);
            }
        }
        break;
    case 8:
        // creat
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        addroot(path0, sizeof(path0), (const char *)&pm->virtualMemory[word0], pm->rootdir);
        // debug
        //fprintf(stderr, "/ [DBG] sys creat; %04x; %06o\n", word0, word1);
        //fprintf(stderr, "/   %s, %s\n", (const char *)&pm->virtualMemory[word0], pm->rootdir);
        //fprintf(stderr, "/   %s\n", path0);
        ret = creat(path0, word1);
        if (ret < 0) {
            pm->cpu->r0 = errno & 0xffff;
            setC(pm->cpu); // error bit
        } else {
            pm->cpu->r0 = ret & 0xffff;
            clearC(pm->cpu);
        }
        break;
    case 9:
        // link
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        addroot(path0, sizeof(path0), (const char *)&pm->virtualMemory[word0], pm->rootdir);
        addroot(path1, sizeof(path1), (const char *)&pm->virtualMemory[word1], pm->rootdir);
        ret = link(path0, path1);
        if (ret < 0) {
            pm->cpu->r0 = errno & 0xffff;
            setC(pm->cpu); // error bit
        } else {
            pm->cpu->r0 = ret & 0xffff;
            clearC(pm->cpu);
        }
        break;
    case 10:
        // unlink
        word0 = fetch(pm->cpu);
        addroot(path0, sizeof(path0), (const char *)&pm->virtualMemory[word0], pm->rootdir);
        ret = unlink(path0);
        if (ret < 0) {
            pm->cpu->r0 = errno & 0xffff;
            setC(pm->cpu); // error bit
        } else {
            pm->cpu->r0 = ret & 0xffff;
            clearC(pm->cpu);
        }
        break;
    case 11:
        // exec
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        // debug
        //fprintf(stderr, "/ [DBG] sys exec; %04x; %04x\n", word0, word1);
        //fprintf(stderr, "/ [DBG]   %s\n", (const char *)&pm->virtualMemory[word0]);

        // calc size of args & copy args
        ret = serializeArgvVirt(pm, &pm->virtualMemory[word1]);
        if (ret < 0) {
            //fprintf(stderr, "/ [ERR] Too big argv\n");
            pm->argc = 0;
            pm->argsbytes = 0;
            pm->cpu->r0 = 0xffff;
            setC(pm->cpu); // error bit
        } else {
            if (!load(pm, (const char *)&pm->virtualMemory[word0])) {
                pm->cpu->r0 = 0xffff;
                setC(pm->cpu); // error bit
            } else {
                pm->cpu->r0 = 0;
                pm->cpu->pc = 0xffff; // goto the end of the memory, then run the new text
                clearC(pm->cpu);
            }
        }
        break;
    case 12:
        // chdir
        word0 = fetch(pm->cpu);
        addroot(path0, sizeof(path0), (const char *)&pm->virtualMemory[word0], pm->rootdir);
        ret = chdir(path0);
        if (ret < 0) {
            pm->cpu->r0 = errno & 0xffff;
            setC(pm->cpu); // error bit
        } else {
            pm->cpu->r0 = ret & 0xffff;
            clearC(pm->cpu);
        }
        break;
    case 13:
        // time
        {
            time_t t = time(NULL);
            pm->cpu->r0 = (t >> 16) & 0xffff;
            pm->cpu->r1 = t & 0xffff;
        }
        break;
    case 15:
        // chmod
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        addroot(path0, sizeof(path0), (const char *)&pm->virtualMemory[word0], pm->rootdir);
        ret = chmod(path0, word1);
        if (ret < 0) {
            pm->cpu->r0 = errno & 0xffff;
            setC(pm->cpu); // error bit
        } else {
            pm->cpu->r0 = ret & 0xffff;
            clearC(pm->cpu);
        }
        break;
    case 17:
        // break
        word0 = fetch(pm->cpu);
        addr = (word0 + 63) & ~63;
        if (addr < pm->bssEnd || pm->cpu->sp < addr) {
            pm->cpu->r0 = 0xffff;
            setC(pm->cpu); // error bit
        } else {
            pm->cpu->r0 = pm->brk;
            pm->brk = addr;
            clearC(pm->cpu);
        }
        break;
    case 18:
        // stat
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        {
            addroot(path0, sizeof(path0), (const char *)&pm->virtualMemory[word0], pm->rootdir);

            struct stat s;
            ret = stat(path0, &s);
            if (ret < 0) {
                pm->cpu->r0 = errno & 0xffff;
                setC(pm->cpu); // error bit
            } else {
                pm->cpu->r0 = ret & 0xffff;
                clearC(pm->cpu);
                uint8_t *pi = &pm->virtualMemory[word1];
                convstat(pi, &s);
                // debug
                //fprintf(stderr, "/ [DBG] stat src: %06o\n", s.st_mode);
                //fprintf(stderr, "/ [DBG] stat dst: %06o\n", *(uint16_t *)(pi + 4));
            }
        }
        break;
    case 19:
        // seek
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        off_t offset;
        if (word1 == 0 || word1 == 3) {
            offset = word0;
        } else {
            offset = (int16_t)word0;
        }
        if (word1 == 3 || word1 == 4 || word1 == 5) {
            offset *= 512;
            word1 -= 3;
        }
        offset = lseek((int16_t)pm->cpu->r0, offset, word1);
        if (offset < 0) {
            pm->cpu->r0 = errno & 0xffff;
            setC(pm->cpu); // error bit
        } else {
            pm->cpu->r0 = offset & 0xffff;
            clearC(pm->cpu);
        }
        break;
    case 20:
        // getpid
        pm->cpu->r0 = getpid() & 0xffff;
        break;
    case 23:
        // setuid
        fprintf(stderr, "/ [WRN] ignore setuid: sys %d, %04x: %04x\n", pm->cpu->syscallID, pm->cpu->addr, pm->cpu->bin);
        {
            pm->cpu->r0 = 0;
            clearC(pm->cpu);
        }
        break;
    case 24:
        // getuid
        pm->cpu->r0 = ((geteuid() & 0xff) << 8) | (getuid() & 0xff);
        break;
    case 28:
        // fstat
        word0 = fetch(pm->cpu);
        {
            struct stat s;
            ret = fstat((int16_t)pm->cpu->r0, &s);
            if (ret < 0) {
                pm->cpu->r0 = errno & 0xffff;
                setC(pm->cpu); // error bit
            } else {
                pm->cpu->r0 = ret & 0xffff;
                clearC(pm->cpu);
                uint8_t *pi = &pm->virtualMemory[word0];
                convstat(pi, &s);
                // debug
                //fprintf(stderr, "/ [DBG] fstat src: %06o\n", s.st_mode);
                //fprintf(stderr, "/ [DBG] fstat dst: %06o\n", *(uint16_t *)(pi + 4));
            }
        }
        break;
    case 41:
        // dup
        ret = dup((int16_t)pm->cpu->r0);
        if (ret < 0) {
            pm->cpu->r0 = errno & 0xffff;
            setC(pm->cpu); // error bit
        } else {
            pm->cpu->r0 = ret & 0xffff;
            clearC(pm->cpu);
        }
        break;
    case 42:
        // pipe
        {
            int pipefd[2];
            ret = pipe(pipefd);
            if (ret < 0) {
                pm->cpu->r0 = errno & 0xffff;
                setC(pm->cpu); // error bit
            } else {
                pm->cpu->r0 = pipefd[0] & 0xffff;
                pm->cpu->r1 = pipefd[1] & 0xffff;
                clearC(pm->cpu);
            }
        }
        break;
    case 43:
        // times
        word0 = fetch(pm->cpu);
        /* in 1/60 seconds
        struct tbuffer {
            int16_t proc_user_time;
            int16_t proc_system_time;
            int16_t child_user_time[2];
            int16_t child_system_time[2];
        };
        */
        {
            long ticks_per_sec = sysconf(_SC_CLK_TCK);
            struct tms sbuf;
            clock_t clk = times(&sbuf);
            assert(clk >= 0);

            // to 1/60 sec
            sbuf.tms_utime = sbuf.tms_utime * 60 / ticks_per_sec;
            sbuf.tms_stime = sbuf.tms_stime * 60 / ticks_per_sec;
            sbuf.tms_cutime = sbuf.tms_cutime * 60 / ticks_per_sec;
            sbuf.tms_cstime = sbuf.tms_cstime * 60 / ticks_per_sec;

            uint16_t *dbuf = (uint16_t *)&pm->virtualMemory[word0];
            dbuf[0] = sbuf.tms_utime & 0xffff;
            dbuf[1] = sbuf.tms_stime & 0xffff;
            dbuf[2] = (sbuf.tms_cutime >> 16) & 0xffff;
            dbuf[3] = sbuf.tms_cutime & 0xffff;
            dbuf[4] = (sbuf.tms_cstime >> 16) & 0xffff;
            dbuf[5] = sbuf.tms_cstime & 0xffff;
        }
        break;
    case 46:
        // setgid
        fprintf(stderr, "/ [WRN] ignore setgid: sys %d, %04x: %04x\n", pm->cpu->syscallID, pm->cpu->addr, pm->cpu->bin);
        {
            pm->cpu->r0 = 0;
            clearC(pm->cpu);
        }
        break;
    case 47:
        // getgid
        pm->cpu->r0 = ((getegid() & 0xff) << 8) | (getgid() & 0xff);
        break;
    case 48:
        // signal
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        fprintf(stderr, "/ [WRN] ignore signal: sys %d; %d; 0x%04x, %04x: %04x\n", pm->cpu->syscallID, word0, word1, pm->cpu->addr, pm->cpu->bin);
        {
            pm->cpu->r0 = 0; // terminate
            clearC(pm->cpu);
        }
        break;
    default:
        // TODO: not implemented
        fprintf(stderr, "/ [ERR] Not implemented: sys %d, %04x: %04x\n", pm->cpu->syscallID, pm->cpu->addr, pm->cpu->bin);
        assert(0);
        break;
    }
}

void syscallString(machine_t *pm, char *str, size_t size, uint8_t id) {
    uint16_t word0 = 0;
    uint16_t word1 = 0;
    switch (id) {
    case 0:
        // indir
        word0 = fetch(pm->cpu);
        snprintf(str, size, "0; 0x%04x", word0);
        break;
    case 1:
        // exit
        snprintf(str, size, "exit");
        break;
    case 2:
        // fork
        snprintf(str, size, "fork");
        break;
    case 3:
        // read
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        snprintf(str, size, "read; 0x%04x; 0x%04x", word0, word1);
        break;
    case 4:
        // write
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        snprintf(str, size, "write; 0x%04x; 0x%04x", word0, word1);
        break;
    case 5:
        // open
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        snprintf(str, size, "open; 0x%04x; 0x%04x", word0, word1);
        break;
    case 6:
        // close
        snprintf(str, size, "close");
        break;
    case 7:
        // wait
        snprintf(str, size, "wait");
        break;
    case 8:
        // creat
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        snprintf(str, size, "creat; 0x%04x; 0x%04x", word0, word1);
        break;
    case 9:
        // link
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        snprintf(str, size, "link; 0x%04x; 0x%04x", word0, word1);
        break;
    case 10:
        // unlink
        word0 = fetch(pm->cpu);
        snprintf(str, size, "unlink; 0x%04x", word0);
        break;
    case 11:
        // exec
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        snprintf(str, size, "exec; 0x%04x; 0x%04x", word0, word1);
        break;
    case 12:
        // chdir
        word0 = fetch(pm->cpu);
        snprintf(str, size, "chdir; 0x%04x", word0);
        break;
    case 13:
        // time
        snprintf(str, size, "time");
        break;
    case 15:
        // chmod
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        snprintf(str, size, "chmod; 0x%04x; 0x%04x", word0, word1);
        break;
    case 17:
        // break
        word0 = fetch(pm->cpu);
        snprintf(str, size, "break; 0x%04x", word0);
        break;
    case 18:
        // stat
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        snprintf(str, size, "stat; 0x%04x; 0x%04x", word0, word1);
        break;
    case 19:
        // seek
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        snprintf(str, size, "seek; 0x%04x; 0x%04x", word0, word1);
        break;
    case 20:
        // getpid
        snprintf(str, size, "getpid");
        break;
    case 23:
        // setuid
        snprintf(str, size, "setuid");
        break;
    case 24:
        // getuid
        snprintf(str, size, "getuid");
        break;
    case 28:
        // fstat
        word0 = fetch(pm->cpu);
        snprintf(str, size, "fstat; 0x%04x", word0);
        break;
    case 41:
        // dup
        snprintf(str, size, "dup");
        break;
    case 42:
        // pipe
        snprintf(str, size, "pipe");
        break;
    case 43:
        // times
        word0 = fetch(pm->cpu);
        snprintf(str, size, "times; 0x%04x", word0);
        break;
    case 46:
        // setgid
        snprintf(str, size, "setgid");
        break;
    case 47:
        // getgid
        snprintf(str, size, "getgid");
        break;
    case 48:
        // signal
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        snprintf(str, size, "sig; 0x%04x; 0x%04x", word0, word1);
        break;
    default:
        // TODO: not implemented
        fprintf(stderr, "/ [ERR] Not implemented, %04x: %04x, sys %d\n", pm->cpu->addr, pm->cpu->bin, id);
        assert(0);
        break;
    }
}
