#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/times.h>
#include <signal.h>

#include "syscall.h"
#include "machine.h"
#ifdef UU_M68K_MINIX
#include "../m68k/src/cpu.h"
#else
#include "../pdp11/src/cpu.h"
#endif
#include "util.h"

// for debug
#define MY_STRACE 0

#ifdef UU_M68K_MINIX
static void convstat(uint8_t *pi, const struct stat* ps) {
    /* st_mode:
    #define S_IFMT  0170000 // type of file
    #define S_IFREG 0100000 // regular
    #define S_IFBLK 0060000 // block special
    #define S_IFDIR 0040000 // directory
    #define S_IFCHR 0020000 // character special
    #define S_IFIFO 0010000 // this is a FIFO
    #define S_ISUID 0004000 // set user id on execution
    #define S_ISGID 0002000 // set group id on execution
                            // next is reserved for future use
    #define S_ISVTX   01000 // save swapped text even after use
    */
    pi[0] = (ps->st_dev >> 8) & 0xff; // pseudo
    pi[1] = ps->st_dev & 0xff; // pseudo
    pi[2] = (ps->st_ino >> 8) & 0xff;
    pi[3] = ps->st_ino & 0xff;
    pi[4] = (ps->st_mode >> 8) & 0xff;
    pi[5] = ps->st_mode & 0xff;
    pi[6] = (ps->st_nlink >>8) & 0xff;
    pi[7] = ps->st_nlink & 0xff;
    pi[8] = (ps->st_uid >> 8) & 0xff;
    pi[9] = ps->st_uid & 0xff;
    pi[10] = (ps->st_gid >> 8) & 0xff;
    pi[11] = ps->st_gid & 0xff;
    pi[12] = (ps->st_rdev >> 8) & 0xff;
    pi[13] = ps->st_rdev & 0xff;
    pi[14] = (ps->st_size >> 24) & 0xff;
    pi[15] = (ps->st_size >> 16) & 0xff;
    pi[16] = (ps->st_size >> 8) & 0xff;
    pi[17] = ps->st_size & 0xff;
    // atime
    pi[18] = (ps->st_atime >> 24) & 0xff;
    pi[19] = (ps->st_atime >> 16) & 0xff;
    pi[20] = (ps->st_atime >> 8) & 0xff;
    pi[21] = ps->st_atime & 0xff;
    // mtime
    pi[22] = (ps->st_mtime >> 24) & 0xff;
    pi[23] = (ps->st_mtime >> 16) & 0xff;
    pi[24] = (ps->st_mtime >> 8) & 0xff;
    pi[25] = ps->st_mtime & 0xff;
    // ctime
    pi[26] = (ps->st_ctime >> 24) & 0xff;
    pi[27] = (ps->st_ctime >> 16) & 0xff;
    pi[28] = (ps->st_ctime >> 8) & 0xff;
    pi[29] = ps->st_ctime & 0xff;
}

#define M1                 1
#define M3                 3
#define M4                 4
#define M3_STRING         14

typedef struct {uint16_t m1i1, m1i2, m1i3; uint8_t *m1p1, *m1p2, *m1p3;} mess_1;
typedef struct {uint16_t m2i1, m2i2, m2i3; uint32_t m2l1, m2l2; uint8_t *m2p1;} mess_2;
typedef struct {uint16_t m3i1, m3i2; uint8_t *m3p1; char m3ca1[M3_STRING];} mess_3;
typedef struct {uint32_t m4l1, m4l2, m4l3, m4l4;} mess_4;
typedef struct {uint8_t m5c1, m5c2; int m5i1, m5i2; uint32_t m5l1, m5l2, m5l3;} mess_5;
typedef struct {uint16_t m6i1, m6i2, m6i3; uint32_t m6l1; uintptr_t m6f1;} mess_6;

typedef struct {
  uint16_t m_source;                 /* who sent the message */
  uint16_t m_type;                   /* what kind of message is it */
  union {
        mess_1 m_m1;
        mess_2 m_m2;
        mess_3 m_m3;
        mess_4 m_m4;
        mess_5 m_m5;
        mess_6 m_m6;
  } m_u;
} message;

#define m1_i1  m_u.m_m1.m1i1
#define m1_i2  m_u.m_m1.m1i2
#define m1_i3  m_u.m_m1.m1i3
#define m1_p1  m_u.m_m1.m1p1
#define m1_p2  m_u.m_m1.m1p2
#define m1_p3  m_u.m_m1.m1p3

#define m2_i1  m_u.m_m2.m2i1
#define m2_i2  m_u.m_m2.m2i2
#define m2_i3  m_u.m_m2.m2i3
#define m2_l1  m_u.m_m2.m2l1
#define m2_l2  m_u.m_m2.m2l2
#define m2_p1  m_u.m_m2.m2p1

#define m3_i1  m_u.m_m3.m3i1
#define m3_i2  m_u.m_m3.m3i2
#define m3_p1  m_u.m_m3.m3p1
#define m3_ca1 m_u.m_m3.m3ca1

#define m4_l1  m_u.m_m4.m4l1
#define m4_l2  m_u.m_m4.m4l2
#define m4_l3  m_u.m_m4.m4l3
#define m4_l4  m_u.m_m4.m4l4

#define m5_c1  m_u.m_m5.m5c1
#define m5_c2  m_u.m_m5.m5c2
#define m5_i1  m_u.m_m5.m5i1
#define m5_i2  m_u.m_m5.m5i2
#define m5_l1  m_u.m_m5.m5l1
#define m5_l2  m_u.m_m5.m5l2
#define m5_l3  m_u.m_m5.m5l3

#define m6_i1  m_u.m_m6.m6i1
#define m6_i2  m_u.m_m6.m6i2
#define m6_i3  m_u.m_m6.m6i3
#define m6_l1  m_u.m_m6.m6l1
#define m6_f1  m_u.m_m6.m6f1


#define MM                 0
#define FS                 1

#define SEND               1    /* function code for sending messages */
#define RECEIVE            2    /* function code for receiving messages */
#define BOTH               3    /* function code for SEND + RECEIVE */

// ioctl
#define TIOCGETP  (('t'<<8) |  8)
#define TIOCSETP  (('t'<<8) |  9)
#define TIOCGETC  (('t'<<8) | 18)
#define TIOCSETC  (('t'<<8) | 17)
#define TIOCFLUSH (('t'<<8) | 16)


void setM1(message *m, uint32_t vraw, machine_t *pm) {
    uint16_t vaddrH, vaddrL;
    uint32_t vaddr;

    m->m_source = ntohs(*(uint16_t *)mmuV2R(pm, vraw+0));
    m->m_type = ntohs(*(uint16_t *)mmuV2R(pm, vraw+2));

    m->m1_i1 = ntohs(*(uint16_t *)mmuV2R(pm, vraw+4));
    m->m1_i2 = ntohs(*(uint16_t *)mmuV2R(pm, vraw+6));
    m->m1_i3 = ntohs(*(uint16_t *)mmuV2R(pm, vraw+8));

    vaddrH = ntohs(*(uint16_t *)mmuV2R(pm, vraw+10));
    vaddrL = ntohs(*(uint16_t *)mmuV2R(pm, vraw+12));
    vaddr = (vaddrH << 16) | vaddrL;
    m->m1_p1 = mmuV2R(pm, vaddr);

    vaddrH = ntohs(*(uint16_t *)mmuV2R(pm, vraw+14));
    vaddrL = ntohs(*(uint16_t *)mmuV2R(pm, vraw+16));
    vaddr = (vaddrH << 16) | vaddrL;
    m->m1_p2 = mmuV2R(pm, vaddr);

    vaddrH = ntohs(*(uint16_t *)mmuV2R(pm, vraw+18));
    vaddrL = ntohs(*(uint16_t *)mmuV2R(pm, vraw+20));
    vaddr = (vaddrH << 16) | vaddrL;
    m->m1_p3 = mmuV2R(pm, vaddr);

    return;
}

void setM2(message *m, uint32_t vraw, machine_t *pm) {
    uint16_t hi, lo;
    uint16_t vaddrH, vaddrL;
    uint32_t vaddr;

    m->m_source = ntohs(*(uint16_t *)mmuV2R(pm, vraw+0));
    m->m_type = ntohs(*(uint16_t *)mmuV2R(pm, vraw+2));

    m->m2_i1 = ntohs(*(uint16_t *)mmuV2R(pm, vraw+4));
    m->m2_i2 = ntohs(*(uint16_t *)mmuV2R(pm, vraw+6));
    m->m2_i3 = ntohs(*(uint16_t *)mmuV2R(pm, vraw+8));

    hi = ntohs(*(uint16_t *)mmuV2R(pm, vraw+10));
    lo = ntohs(*(uint16_t *)mmuV2R(pm, vraw+12));
    m->m2_l1 = (hi << 16) | lo;

    hi = ntohs(*(uint16_t *)mmuV2R(pm, vraw+14));
    lo = ntohs(*(uint16_t *)mmuV2R(pm, vraw+16));
    m->m2_l2 = (hi << 16) | lo;

    vaddrH = ntohs(*(uint16_t *)mmuV2R(pm, vraw+18));
    vaddrL = ntohs(*(uint16_t *)mmuV2R(pm, vraw+20));
    vaddr = (vaddrH << 16) | vaddrL;
    // TODO: don't touch an uninitialized pointer
    vaddr = 0;
    m->m2_p1 = mmuV2R(pm, vaddr);

    return;
}

void setM3(message *m, uint32_t vraw, machine_t *pm) {
    uint16_t vaddrH, vaddrL;
    uint32_t vaddr;

    m->m_source = ntohs(*(uint16_t *)mmuV2R(pm, vraw+0));
    m->m_type = ntohs(*(uint16_t *)mmuV2R(pm, vraw+2));

    m->m3_i1 = ntohs(*(uint16_t *)mmuV2R(pm, vraw+4));
    m->m3_i2 = ntohs(*(uint16_t *)mmuV2R(pm, vraw+6));

    vaddrH = ntohs(*(uint16_t *)mmuV2R(pm, vraw+8));
    vaddrL = ntohs(*(uint16_t *)mmuV2R(pm, vraw+10));
    vaddr = (vaddrH << 16) | vaddrL;
    m->m3_p1 = mmuV2R(pm, vaddr);

    const uint8_t *src = mmuV2R(pm, vraw+12);
    memcpy(m->m3_ca1, src, M3_STRING);

    return;
}

void setM6(message *m, uint32_t vraw, machine_t *pm) {
    uint16_t hi, lo;
    uint16_t vaddrH, vaddrL;
    uint32_t vaddr;

    m->m_source = ntohs(*(uint16_t *)mmuV2R(pm, vraw+0));
    m->m_type = ntohs(*(uint16_t *)mmuV2R(pm, vraw+2));

    m->m6_i1 = ntohs(*(uint16_t *)mmuV2R(pm, vraw+4));
    m->m6_i2 = ntohs(*(uint16_t *)mmuV2R(pm, vraw+6));
    m->m6_i3 = ntohs(*(uint16_t *)mmuV2R(pm, vraw+8));

    hi = ntohs(*(uint16_t *)mmuV2R(pm, vraw+10));
    lo = ntohs(*(uint16_t *)mmuV2R(pm, vraw+12));
    m->m6_l1 = (hi << 16) | lo;

    vaddrH = ntohs(*(uint16_t *)mmuV2R(pm, vraw+14));
    vaddrL = ntohs(*(uint16_t *)mmuV2R(pm, vraw+16));
    vaddr = (vaddrH << 16) | vaddrL;
    m->m6_f1 = vaddr;

    return;
}

void mysyscall16(machine_t *pm) {
    message m;

    const char *name, *name2;
    char path0[PATH_MAX], path1[PATH_MAX];
    mode_t mode;
    int fd;
    uint8_t *buf;
    size_t nbytes;
    int flags;
    pid_t pid; // TODO: support 32-bit
    int sig;
    ssize_t sret;
    int ret;
    int e;

    uint16_t sendrec = getD0(pm->cpu) & 0xffff;
    assert(sendrec == BOTH);
    setD0(pm->cpu, 0); // succeed sendrec itself

    uint16_t mmfs = getD1(pm->cpu) & 0xffff;
    uint32_t vraw = getA0(pm->cpu);
    assert((vraw & 1) == 0); // alignment

    // reply
    uint16_t *pBE_reply_type = (uint16_t *)(mmuV2R(pm, vraw+2));
    // FS
    uint16_t *pBE_reply_i2 = (uint16_t *)(mmuV2R(pm, vraw+6));
    uint16_t *pBE_reply_l1_hi = (uint16_t *)(mmuV2R(pm, vraw+10));
    uint16_t *pBE_reply_l1_lo = (uint16_t *)(mmuV2R(pm, vraw+12));
    // MM
    uint16_t *pBE_reply_i1 = (uint16_t *)(mmuV2R(pm, vraw+4));
    uint16_t *pBE_reply_p1_hi = (uint16_t *)(mmuV2R(pm, vraw+18));
    uint16_t *pBE_reply_p1_lo = (uint16_t *)(mmuV2R(pm, vraw+20));

    uint16_t syscallID = ntohs(*pBE_reply_type);
#if MY_STRACE
    if (syscallID != 4) {
        fprintf(stderr, "/ syscall: %d\n", syscallID);
    }
#endif
    switch (syscallID) {
    case 1:
        // exit
        assert(mmfs == MM);
        setM1(&m, vraw, pm);
        int status = m.m1_i1;
#if MY_STRACE
        fprintf(stderr, "/ _exit(%d)\n", status);
#endif
        _exit(status);
        break;
    case 2:
        // fork
        assert(mmfs == MM);
        setM1(&m, vraw, pm);
#if MY_STRACE
        fprintf(stderr, "/ fork()\n");
#endif
        pid = fork();
        if (pid < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(pid & 0x7fff); // valid 15-bit only
#if MY_STRACE
            fprintf(stderr, "/ [DBG] fork pid: %5d pid15: %5d (pc: %08x)\n", pid, pid&0x7fff, getPC(pm->cpu));
#endif
        }
        break;
    case 3:
        // read
        assert(mmfs == FS);
        setM1(&m, vraw, pm);
        fd = m.m1_i1;
        buf = m.m1_p1;
        nbytes = m.m1_i2;
#if MY_STRACE
        fprintf(stderr, "/ read(%d, %08x, %ld)\n", fd, mmuR2V(pm, buf), nbytes);
#endif
        if (pm->dirfd != -1 && pm->dirp != NULL && fd == pm->dirfd) {
            // dir
            assert((nbytes & 0xf) == 0);
            sret = 0;
            errno = 0;
            for (size_t i = 0; i < nbytes; i += 16) {
                struct dirent *ent;
                ent = readdir(pm->dirp);
                if (ent == NULL) {
                    if (errno == 0) {
                        // EOF
                    } else {
                        // error
                        sret = -1;
                    }
                    break;
                }

                uint8_t *p = buf + i;
                // ino
                p[0] = (ent->d_ino >> 8) & 0xff;
                p[1] = ent->d_ino & 0xff;
                // name
                memcpy((char *)&p[2], ent->d_name, 16 - 2);
#if MY_STRACE
                fprintf(stderr, "/ [DBG] readdir: %016lx, %s\n", ent->d_ino, ent->d_name);
#endif
                sret += 16;
            }
        } else {
            // file
            sret = read(fd, buf, nbytes);
        }
        if (sret < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(sret & 0xffff);
        }
        break;
    case 4:
        // write
        assert(mmfs == FS);
        setM1(&m, vraw, pm);
        fd = m.m1_i1;
        buf = m.m1_p1;
        nbytes = m.m1_i2;
#if MY_STRACE
        if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
            fprintf(stderr, "/ write(%d, %08x, %ld)\n", fd, mmuR2V(pm, buf), nbytes);
        }
#endif
        sret = write(fd, buf, nbytes);
        if (sret < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(sret & 0xffff);
        }
        break;
    case 5:
        // open
        assert(mmfs == FS);
        // common for M1 and M3
        flags = ntohs(*pBE_reply_i2);
        if (flags & O_CREAT) {
            setM1(&m, vraw, pm);
            mode = m.m1_i3;
            name = (const char *)m.m1_p1;
        } else {
            setM3(&m, vraw, pm);
            mode = 0;
            name = (const char *)m.m3_p1;
        }
        addroot(path0, sizeof(path0), name, pm->rootdir);
#if MY_STRACE
        // common for M1 and M3
        int len = ntohs(*pBE_reply_i1);
        fprintf(stderr, "/ open(\"%s\", %d, %06o) // name len=%d, full=%s\n", name, flags, mode, len, path0);
#endif
        ret = open(path0, flags, mode);
        if (ret < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(ret & 0xffff);

            // check file or dir
            fd = ret;
            struct stat s;
            ret = fstat(fd, &s);
            if (ret == 0 && S_ISDIR(s.st_mode)) {
                // dir
                DIR *dirp = fdopendir(fd);
                if (dirp == NULL) {
                    *pBE_reply_type = htons(-errno & 0xffff);
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
        assert(mmfs == FS);
        setM1(&m, vraw, pm);
        fd = m.m1_i1;
#if MY_STRACE
        fprintf(stderr, "/ close(%d)\n", fd);
#endif
        if (pm->dirfd != -1 && pm->dirp != NULL && fd == pm->dirfd) {
            // dir
            ret = closedir(pm->dirp);
            pm->dirfd = -1;
            pm->dirp = NULL;
        } else {
            // file
            ret = close(fd);
        }
        if (ret < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(ret & 0xffff);
        }
        break;
    case 7:
        // wait
        assert(mmfs == MM);
        setM1(&m, vraw, pm);
#if MY_STRACE
        fprintf(stderr, "/ wait(&status)\n");
#endif
        int wstatus;
        pid = wait(&wstatus);
        if (pid < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(pid & 0x7fff); // valid 15-bit only
            *pBE_reply_i1 = htons(wstatus & 0xffff);
#if MY_STRACE
            fprintf(stderr, "/ [DBG] wait pid: %d pid15: %d status: %04x\n", pid, pid&0x7fff, wstatus);
#endif
        }
        break;
    case 8:
        // creat
        assert(mmfs == FS);
        setM3(&m, vraw, pm);
        //size_t len = m.m3_i1;
        mode = m.m3_i2;
        name = (const char *)m.m3_p1; // long and short
        //name = (const char *)&m.m3_ca1[0]; // short only
        addroot(path0, sizeof(path0), name, pm->rootdir);
#if MY_STRACE
        fprintf(stderr, "/ creat(\"%s\", %06o) // name len=%d, full=%s\n", name, mode, m.m3_i1, path0);
#endif
        ret = creat(path0, mode);
        if (ret < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(ret & 0xffff);
        }
        break;
    case 9:
        // link
        assert(mmfs == FS);
        setM1(&m, vraw, pm);
        //size_t len = m.m1_i1;
        //size_t len2 = m.m1_i2;
        name = (const char *)m.m1_p1;
        name2 = (const char *)m.m1_p2;
        addroot(path0, sizeof(path0), name, pm->rootdir);
        addroot(path1, sizeof(path1), name2, pm->rootdir);
#if MY_STRACE
        fprintf(stderr, "/ link(\"%s\", \"%s\") // name len=%d, full=%s, len2=%d, full2=%s\n", name, name2, m.m1_i1, path0, m.m1_i2, path1);
#endif
        ret = link(path0, path1);
        if (ret < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(ret & 0xffff);
        }
        break;
    case 10:
        // unlink
        assert(mmfs == FS);
        setM3(&m, vraw, pm);
        //size_t len = m.m3_i1;
        //uint16_t zero = m.m3_i2;
        name = (const char *)m.m3_p1; // long and short
        //name = (const char *)&m.m3_ca1[0]; // short only
        addroot(path0, sizeof(path0), name, pm->rootdir);
#if MY_STRACE
        fprintf(stderr, "/ unlink(\"%s\") // name len=%d, full=%s\n", name, m.m3_i1, path0);
#endif
        ret = unlink(path0);
        if (ret < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(ret & 0xffff);
        }
        break;
    case 12:
        // chdir
        assert(mmfs == FS);
        setM3(&m, vraw, pm);
        //size_t len = m.m3_i1;
        //uint16_t zero = m.m3_i2;
        name = (const char *)m.m3_p1; // long and short
        //name = (const char *)&m.m3_ca1[0]; // short only
#if MY_STRACE
        fprintf(stderr, "/ chdir(\"%s\") // name len=%d\n", name, m.m3_i1);
#endif
        if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
            // TODO: support
            //  '../foo'
            //  './..'
            //  'foo/bar/../../..'
            char *p = getcwd(path0, sizeof(path0));
            if (p != NULL && strcmp(path0, pm->rootdir) == 0) {
                // do nothing
                *pBE_reply_type = 0;
                break;
            }
            ret = chdir("..");
        } else {
            addroot(path0, sizeof(path0), name, pm->rootdir);
            ret = chdir(path0);
        }
        if (ret < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(ret & 0xffff);
        }
        break;
    case 13:
        // time
        assert(mmfs == FS);
        setM1(&m, vraw, pm);
#if MY_STRACE
        fprintf(stderr, "/ time()\n");
#endif
        {
            time_t t = time(NULL);
            if (t < 0) {
                *pBE_reply_type = htons(-errno & 0xffff);
                // -1
                *pBE_reply_l1_hi = 0xffff;
                *pBE_reply_l1_lo = 0xffff;
            } else {
                *pBE_reply_type = 0;
                *pBE_reply_l1_hi = htons((t>>16) & 0xffff);
                *pBE_reply_l1_lo = htons(t & 0xffff);
            }
        }
        break;
    case 15:
        // chmod
        assert(mmfs == FS);
        setM3(&m, vraw, pm);
        //size_t len = m.m3_i1;
        mode = m.m3_i2;
        name = (const char *)m.m3_p1; // long and short
        //name = (const char *)&m.m3_ca1[0]; // short only
        addroot(path0, sizeof(path0), name, pm->rootdir);
#if MY_STRACE
        fprintf(stderr, "/ chmod(\"%s\", %06o) // name len=%d, full=%s\n", name, mode, m.m3_i1, path0);
#endif
        ret = chmod(path0, mode);
        if (ret < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(ret & 0xffff);
        }
        break;
    case 17:
        // brk
        assert(mmfs == MM);
        setM1(&m, vraw, pm);
        uint32_t addr = mmuR2V(pm, m.m1_p1);
        uint32_t addr256 = (addr + 255) & ~255;
#if MY_STRACE
        fprintf(stderr, "/ brk(%08x)\n", addr);
        fprintf(stderr, "/   bssEnd: %08x\n", pm->bssEnd);
        fprintf(stderr, "/   brk:    %08x -> %08x\n", pm->brk, addr256);
        fprintf(stderr, "/   SP:     %08x\n", getSP(pm->cpu));
#endif
        if (addr256 < pm->bssEnd || getSP(pm->cpu) < addr256) {
            *pBE_reply_type = htons(-ENOMEM & 0xffff);
            // -1
            *pBE_reply_p1_hi = 0xffff;
            *pBE_reply_p1_lo = 0xffff;
        } else {
            pm->brk = addr256;
            *pBE_reply_type = 0;
            *pBE_reply_p1_hi = htons(addr>>16);
            *pBE_reply_p1_lo = htons(addr&0xffff);
        }
        break;
    case 18:
        // stat
        assert(mmfs == FS);
        setM1(&m, vraw, pm);
        //size_t len = m.m1_i1;
        name = (const char *)m.m1_p1;
        buf = m.m1_p2;
        addroot(path0, sizeof(path0), name, pm->rootdir);
#if MY_STRACE
        fprintf(stderr, "/ stat(\"%s\", %08x) // name len=%d, full=%s\n", name, mmuR2V(pm, buf), m.m1_i1, path0);
#endif
        {
            struct stat s;
            ret = stat(path0, &s);
            if (ret < 0) {
                *pBE_reply_type = htons(-errno & 0xffff);
            } else {
                *pBE_reply_type = htons(ret & 0xffff);
                uint8_t *pi = buf;
                convstat(pi, &s);
#if MY_STRACE
                fprintf(stderr, "/ [DBG] inode=%016lx\n", s.st_ino);
                fprintf(stderr, "/ [DBG] stat src: %06o\n", s.st_mode);
                fprintf(stderr, "/ [DBG] stat dst: %06o\n", ntohs(*(uint16_t *)(pi + 4)));
#endif
            }
        }
        break;
    case 19:
        // lseek
        assert(mmfs == FS);
        setM2(&m, vraw, pm);
        fd = m.m2_i1;
        off_t offset = m.m2_l1;
        int whence = m.m2_i2;
#if MY_STRACE
        fprintf(stderr, "/ lseek(%d, %ld, %d)\n", fd, offset, whence);
#endif
        // seekdir
        if (pm->dirp != NULL) {
            assert(offset == 0);
            assert(whence == SEEK_CUR);
        }
        offset = lseek(fd, offset, whence);
        if (offset < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = 0;
            *pBE_reply_l1_hi = htons((offset>>16) & 0xffff);
            *pBE_reply_l1_lo = htons(offset & 0xffff);
        }
        break;
    case 20:
        // getpid
        assert(mmfs == MM);
#if MY_STRACE
        fprintf(stderr, "/ getpid()\n");
#endif
        pid = getpid();
        *pBE_reply_type = htons(pid & 0x7fff); // valid 15-bit only
        break;
    case 24:
        // getuid
        assert(mmfs == MM);
#if MY_STRACE
        fprintf(stderr, "/ getuid(),geteuid()\n");
#endif
        uid_t uid = getuid();
        uid_t euid = geteuid();
        *pBE_reply_type = htons(uid & 0xffff);
        *pBE_reply_i1 = htons(euid & 0xffff);
        break;
    case 28:
        // fstat
        assert(mmfs == FS);
        setM1(&m, vraw, pm);
        fd = m.m1_i1;
        buf = m.m1_p1;
#if MY_STRACE
        fprintf(stderr, "/ fstat(%d, %08x)\n", fd, mmuR2V(pm, buf));
#endif
        {
            struct stat s;
            ret = fstat(fd, &s);
            if (ret < 0) {
                *pBE_reply_type = htons(-errno & 0xffff);
            } else {
                *pBE_reply_type = htons(ret & 0xffff);
                uint8_t *pi = buf;
                convstat(pi, &s);
#if MY_STRACE
                fprintf(stderr, "/ [DBG] inode=%016lx\n", s.st_ino);
                fprintf(stderr, "/ [DBG] fstat src: %06o\n", s.st_mode);
                fprintf(stderr, "/ [DBG] fstat dst: %06o\n", ntohs(*(uint16_t *)(pi + 4)));
#endif
            }
        }
        break;
    case 33:
        // access
        assert(mmfs == FS);
        setM3(&m, vraw, pm);
        //size_t len = m.m3_i1;
        int fmode = m.m3_i2;
        name = (const char *)m.m3_p1; // long and short
        //name = (const char *)&m.m3_ca1[0]; // short only
        addroot(path0, sizeof(path0), name, pm->rootdir);
#if MY_STRACE
        fprintf(stderr, "/ access(\"%s\", %d) // name len=%d, full=%s\n", name, fmode, m.m3_i1, path0);
#endif
        ret = access(path0, fmode);
        if (ret < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(ret & 0xffff);
        }
        break;
    case 37:
        // kill
        assert(mmfs == MM);
        setM1(&m, vraw, pm);
        pid = m.m1_i1;
        sig = m.m1_i2;
#if MY_STRACE
        fprintf(stderr, "/ kill(%d, %d)\n", pid, sig);
#endif
        ret = kill(pid, sig);
        if (ret < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(ret & 0xffff);
        }
        break;
    case 39:
        // mkdir
        assert(mmfs == FS);
        setM1(&m, vraw, pm);
        //size_t len = m.m1_i1;
        mode = m.m1_i2;
        name = (const char *)m.m1_p1;
        addroot(path0, sizeof(path0), name, pm->rootdir);
#if MY_STRACE
        fprintf(stderr, "/ mkdir(\"%s\", %06o) // name len=%d, full=%s\n", name, mode, m.m1_i1, path0);
#endif
        ret = mkdir(name, mode);
        if (ret < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(ret & 0xffff);
        }
        break;
    case 41:
        // dup, dup2
        assert(mmfs == FS);
        setM1(&m, vraw, pm);
        fd = m.m1_i1;
        int rfd = fd & ~(0100); // mask to distinguish dup2 from dup
        int fd2 = m.m1_i2;
        if (fd == rfd) {
#if MY_STRACE
            fprintf(stderr, "/ dup(%d)\n", fd);
#endif
            ret = dup(fd);
        } else {
#if MY_STRACE
            fprintf(stderr, "/ dup2(%d, %d)\n", rfd, fd2);
#endif
            ret = dup2(rfd, fd2);
        }
        if (ret < 0) {
            *pBE_reply_type = htons(-errno & 0xffff);
        } else {
            *pBE_reply_type = htons(ret & 0xffff);
        }
        break;
    case 42:
        // pipe
        assert(mmfs == FS);
        setM1(&m, vraw, pm);
        {
#if MY_STRACE
            fprintf(stderr, "/ pipe()\n");
#endif
            int pipefd[2];
            ret = pipe(pipefd);
            e = errno;
#if MY_STRACE
            fprintf(stderr, "/ [DBG] ret=%d, fd0=%d, fd1=%d\n", ret, pipefd[0], pipefd[1]);
#endif
            if (ret < 0) {
                *pBE_reply_type = htons(-e & 0xffff);
            } else {
                *pBE_reply_type = 0;
                *pBE_reply_i1 = htons(pipefd[0] & 0xffff);
                *pBE_reply_i2 = htons(pipefd[1] & 0xffff);
            }
        }
        break;
    case 47:
        // getgid
        assert(mmfs == MM);
#if MY_STRACE
        fprintf(stderr, "/ getgid(),getegid()\n");
#endif
        uid_t gid = getgid();
        uid_t egid = getegid();
        *pBE_reply_type = htons(gid & 0xffff);
        *pBE_reply_i1 = htons(egid & 0xffff);
        break;
    case 48:
        // signal
        assert(mmfs == MM);
        setM6(&m, vraw, pm);
        sig = m.m6_i1;
        uintptr_t func = m.m6_f1;
        if (func == (uintptr_t)SIG_DFL/* 0 */ || func == (uintptr_t)SIG_IGN/* 1 */) {
#if MY_STRACE
            fprintf(stderr, "/ signal(%d, %08lx)\n", sig, func);
#endif
            func = (uintptr_t)signal(sig, (void (*)(int))func);
            e = errno;
#if MY_STRACE
            fprintf(stderr, "/ [DBG] ret=%08lx\n", func);
#endif
            if (func == (uintptr_t)SIG_ERR) {
                *pBE_reply_type = htons(-e & 0xffff);
            } else {
                *pBE_reply_type = htons(func & 0xffff);
            }
        } else {
            fprintf(stderr, "/ [WRN] ignore signal(%d, %08lx)\n", sig, func);
            *pBE_reply_type = htons(-EINVAL & 0xffff);
        }
        break;
    case 54:
        // ioctl
        assert(mmfs == FS);
        setM2(&m, vraw, pm);
        fd = m.m2_i1;
        int request = m.m2_i3;
        //uint32_t spek = m.m2_l1;
        //flags = m.m2_l2;
        // support only isatty()
        if (request != TIOCGETP) {
            *pBE_reply_type = htons(-EBADF & 0xffff);
            break;
        }
#if MY_STRACE
        fprintf(stderr, "/ isatty(%d)\n", fd);
#endif
        ret = isatty(fd);
        e = errno;
#if MY_STRACE
        fprintf(stderr, "/ [DBG] ret=%d\n", ret);
#endif
        if (ret == 0) {
            *pBE_reply_type = htons(-e & 0xffff);
        } else {
            *pBE_reply_type = htons(ret & 0xffff);
        }
        break;
    case 59:
        // exec
        assert(mmfs == MM);
        setM1(&m, vraw, pm);
        const char *exec_name = (const char *)m.m1_p1;
        uint8_t *stack_ptr = m.m1_p2;
#if MY_STRACE
        size_t exec_len = m.m1_i1;
        size_t stack_bytes = m.m1_i2;
        fprintf(stderr, "/ exec(\"%s\"(%ld), %08x[%ld])\n", exec_name, exec_len, mmuR2V(pm, stack_ptr), stack_bytes);
        fprintf(stderr, "/ [DBG] reply vaddr: %08x\n", vraw+2);
        for (size_t i = 0; i < stack_bytes; i += 16) {
            fprintf(stderr, "/ [DBG] %08x:", mmuR2V(pm, stack_ptr+i));
            for (size_t j = 0; j < 16; ++j) {
                if (i + j < stack_bytes) {
                    if (j == 8) fprintf(stderr, " ");
                    fprintf(stderr, " %02x", stack_ptr[i + j]);
                }
            }
            fprintf(stderr, "\n");
        }
#endif
        // calc size of args & copy args
        ret = serializeArgvVirt(pm, mmuR2V(pm, stack_ptr));
        if (ret < 0) {
            pm->argc = 0;
            pm->argsbytes = 0;
            *pBE_reply_type = htons(-E2BIG & 0xffff);
        } else {
            ret = load(pm, exec_name);
            if (ret != 0) {
#if MY_STRACE
                fprintf(stderr, "/ [DBG] load(\"%s\"): %s\n", exec_name, strerror(ret));
#endif
                *pBE_reply_type = htons(-ret & 0xffff);
            } else {
                // Do NOT reply if succeeded! (*pBE_reply_type = 0)
                // It may cause damage to the aout that is loaded just now.

                // goto the end of the memory, then run the new text
                uint32_t isp = getISP(pm->cpu);
                assert((isp & 1) == 0); // isp is word-aligned.
                uint32_t eom = pm->sizeOfVM - 1;
#if MY_STRACE
                fprintf(stderr, "/ [DBG] new ppc: %08x\n", eom-2);
                fprintf(stderr, "/ [DBG] new pc:  %08x\n", eom);
#endif
                *(uint16_t *)(mmuV2R(pm, isp+2)) = htons((eom >> 16) & 0xffff);
                *(uint16_t *)(mmuV2R(pm, isp+4)) = htons(eom & 0xffff);
            }
        }
        break;
    case 60:
        // umask
        assert(mmfs == FS);
        setM1(&m, vraw, pm);
        mode_t mask = m.m1_i1;
#if MY_STRACE
        fprintf(stderr, "/ umask(%#03o)\n", mask);
#endif
        ret = umask(mask);
        *pBE_reply_type = htons(ret & 0xffff);
        break;
    default:
        // TODO: not implemented
        fprintf(stderr, "/ [ERR] Not implemented: syscall: %d (addr=%08x)\n", syscallID, vraw);
        assert(0);
        break;
    }
}
#else
static void convstat16(uint8_t *pi, const struct stat* ps) {
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

void mysyscall16(machine_t *pm) {
    uint16_t addr;

    uint16_t word0 = 0;
    uint16_t word1 = 0;
    char path0[PATH_MAX];
    char path1[PATH_MAX];
    ssize_t sret;
    int ret;
    int e;

#if MY_STRACE
    if (pm->cpu->syscallID != 4 && pm->cpu->syscallID != 0) {
        fprintf(stderr, "/ syscall: %d (addr=%04x, bin=%04x)\n", pm->cpu->syscallID, pm->cpu->addr, pm->cpu->bin);
    }
#endif
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
            mysyscall16(pm);
        }
        // syscall exec(11) overwrites pc!
        if (pm->cpu->syscallID == 11) {
            uint16_t eom16 = (pm->sizeOfVM - 1) & 0xffff;
            if (pm->cpu->pc != eom16) {
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
#if MY_STRACE
        fprintf(stderr, "/ _exit(%d)\n", (int16_t)pm->cpu->r0);
#endif
        _exit((int16_t)pm->cpu->r0);
        break;
    case 2:
        // fork
#if MY_STRACE
        fprintf(stderr, "/ fork()\n");
#endif
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
#if MY_STRACE
            fprintf(stderr, "/ [DBG] fork pid: %5d (pc: %04x)\n", ret, pm->cpu->pc);
#endif
            pm->cpu->r0 = ret & 0xffff;
            clearC(pm->cpu);
        }
        break;
    case 3:
        // read
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
#if MY_STRACE
        fprintf(stderr, "/ read(%d, %04x, %d)\n", (int16_t)pm->cpu->r0, word0, word1);
#endif
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
                memcpy((char *)&p[2], ent->d_name, 16 - 2);
#if MY_STRACE
                fprintf(stderr, "/ [DBG] readdir: %016lx, %s\n", ent->d_ino, ent->d_name);
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
#if MY_STRACE
        if ((int16_t)pm->cpu->r0 != STDOUT_FILENO && (int16_t)pm->cpu->r0 != STDERR_FILENO) {
            fprintf(stderr, "/ write(%d, %04x, %d)\n", (int16_t)pm->cpu->r0, word0, word1);
        }
#endif
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
#if MY_STRACE
        fprintf(stderr, "/ open(\"%s\", %d) // full=%s\n",
            (const char *)&pm->virtualMemory[word0],
            word1,
            path0);
#endif
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
#if MY_STRACE
        fprintf(stderr, "/ close(%d)\n", (int16_t)pm->cpu->r0);
#endif
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
#if MY_STRACE
        fprintf(stderr, "/ wait(&status)\n");
#endif
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
#if MY_STRACE
                fprintf(stderr, "/ [DBG] wait pid: %d status: %04x\n", ret, status);
#endif
            }
        }
        break;
    case 8:
        // creat
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        addroot(path0, sizeof(path0), (const char *)&pm->virtualMemory[word0], pm->rootdir);
#if MY_STRACE
        fprintf(stderr, "/ creat(\"%s\", %06o) // full=%s\n",
            (const char *)&pm->virtualMemory[word0],
            word1,
            path0);
#endif
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
#if MY_STRACE
        fprintf(stderr, "/ link(\"%s\", \"%s\") // full=%s, full2=%s\n",
            (const char *)&pm->virtualMemory[word0],
            (const char *)&pm->virtualMemory[word1],
            path0,
            path1);
#endif
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
#if MY_STRACE
        fprintf(stderr, "/ unlink(\"%s\") // full=%s\n",
            (const char *)&pm->virtualMemory[word0],
            path0);
#endif
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
#if MY_STRACE
        fprintf(stderr, "/ exec(\"%s\", %04x)\n",
            (const char *)&pm->virtualMemory[word0],
            word1);
#endif
        // calc size of args & copy args
        ret = serializeArgvVirt16(pm, &pm->virtualMemory[word1]);
        if (ret < 0) {
            fprintf(stderr, "/ [ERR] Too big argv\n");
            pm->argc = 0;
            pm->argsbytes = 0;
            pm->cpu->r0 = 0xffff;
            setC(pm->cpu); // error bit
        } else {
            ret = load(pm, (const char *)&pm->virtualMemory[word0]);
            if (ret != 0) {
#if MY_STRACE
                fprintf(stderr, "/ [DBG] load(\"%s\"): %s\n", (const char *)&pm->virtualMemory[word0], strerror(ret));
#endif
                pm->cpu->r0 = 0xffff;
                setC(pm->cpu); // error bit
            } else {
                pm->cpu->r0 = 0;
                // goto the end of the memory, then run the new text
                uint16_t eom16 = (pm->sizeOfVM - 1) & 0xffff;
#if MY_STRACE
                fprintf(stderr, "/ [DBG] new pc:  %04x\n", eom16);
#endif
                pm->cpu->pc = eom16;
                clearC(pm->cpu);
            }
        }
        break;
    case 12:
        // chdir
        word0 = fetch(pm->cpu);
        const char *name = (const char *)&pm->virtualMemory[word0];
#if MY_STRACE
        fprintf(stderr, "/ chdir(\"%s\")\n", name);
#endif
        if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
            // TODO: support
            //  '../foo'
            //  './..'
            //  'foo/bar/../../..'
            char *p = getcwd(path0, sizeof(path0));
            if (p != NULL && strcmp(path0, pm->rootdir) == 0) {
                // do nothing
                pm->cpu->r0 = 0;
                clearC(pm->cpu);
                break;
            }
            ret = chdir("..");
        } else {
            addroot(path0, sizeof(path0), name, pm->rootdir);
            ret = chdir(path0);
        }
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
#if MY_STRACE
        fprintf(stderr, "/ time()\n");
#endif
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
#if MY_STRACE
        fprintf(stderr, "/ chmod(\"%s\", %06o) // full=%s\n",
            (const char *)&pm->virtualMemory[word0],
            word1,
            path0);
#endif
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
        uint16_t addr64 = (word0 + 63) & ~63;
#if MY_STRACE
        fprintf(stderr, "/ break(%04x)\n", word0);
        fprintf(stderr, "/   bssEnd: %04x\n", pm->bssEnd);
        fprintf(stderr, "/   brk:    %04x -> %04x\n", pm->brk, addr64);
        fprintf(stderr, "/   SP:     %04x\n", pm->cpu->sp);
#endif
        if (addr64 < pm->bssEnd || pm->cpu->sp < addr64) {
            pm->cpu->r0 = 0xffff;
            setC(pm->cpu); // error bit
        } else {
            pm->cpu->r0 = pm->brk;
            pm->brk = addr64;
            clearC(pm->cpu);
        }
        break;
    case 18:
        // stat
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        {
            addroot(path0, sizeof(path0), (const char *)&pm->virtualMemory[word0], pm->rootdir);
#if MY_STRACE
            fprintf(stderr, "/ stat(\"%s\", %04x) // full=%s\n",
                (const char *)&pm->virtualMemory[word0],
                word1,
                path0);
#endif

            struct stat s;
            ret = stat(path0, &s);
            if (ret < 0) {
                pm->cpu->r0 = errno & 0xffff;
                setC(pm->cpu); // error bit
            } else {
                pm->cpu->r0 = ret & 0xffff;
                clearC(pm->cpu);
                uint8_t *pi = &pm->virtualMemory[word1];
                convstat16(pi, &s);
#if MY_STRACE
                fprintf(stderr, "/ [DBG] inode=%016lx\n", s.st_ino);
                fprintf(stderr, "/ [DBG] stat src: %06o\n", s.st_mode);
                fprintf(stderr, "/ [DBG] stat dst: %06o\n", *(uint16_t *)(pi + 4));
#endif
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
#if MY_STRACE
        fprintf(stderr, "/ lseek(%d, %ld, %d)\n", (int16_t)pm->cpu->r0, offset, word1);
#endif
        // TODO: seekdir
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
#if MY_STRACE
        fprintf(stderr, "/ getpid()\n");
#endif
        pm->cpu->r0 = getpid() & 0xffff;
        break;
    case 23:
        // setuid
        fprintf(stderr, "/ [WRN] ignore setuid(), (addr=%04x, bin=%04x)\n", pm->cpu->addr, pm->cpu->bin);
        {
            pm->cpu->r0 = 0;
            clearC(pm->cpu);
        }
        break;
    case 24:
        // getuid
#if MY_STRACE
        fprintf(stderr, "/ getuid(),geteuid()\n");
#endif
        pm->cpu->r0 = ((geteuid() & 0xff) << 8) | (getuid() & 0xff);
        break;
    case 28:
        // fstat
        word0 = fetch(pm->cpu);
#if MY_STRACE
        fprintf(stderr, "/ fstat(%d, %04x)\n", (int16_t)pm->cpu->r0, word0);
#endif
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
                convstat16(pi, &s);
#if MY_STRACE
                fprintf(stderr, "/ [DBG] inode=%016lx\n", s.st_ino);
                fprintf(stderr, "/ [DBG] fstat src: %06o\n", s.st_mode);
                fprintf(stderr, "/ [DBG] fstat dst: %06o\n", *(uint16_t *)(pi + 4));
#endif
            }
        }
        break;
    case 41:
        // dup
#if MY_STRACE
        fprintf(stderr, "/ dup(%d)\n", (int16_t)pm->cpu->r0);
#endif
        ret = dup((int16_t)pm->cpu->r0);
#if MY_STRACE
        if (ret == 2) {
            fprintf(stderr, "/ dup(%d)\n", (int16_t)pm->cpu->r0);
            fprintf(stderr, "/ [DBG] ret is stderr: %d\n", ret);
        }
#endif
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
#if MY_STRACE
            fprintf(stderr, "/ pipe()\n");
#endif
            int pipefd[2];
            ret = pipe(pipefd);
            e = errno;
#if MY_STRACE
            fprintf(stderr, "/ [DBG] ret=%d, fd0=%d, fd1=%d\n", ret, pipefd[0], pipefd[1]);
#endif
            if (ret < 0) {
                pm->cpu->r0 = e & 0xffff;
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
#if MY_STRACE
        fprintf(stderr, "/ times(%04x)\n", word0);
#endif
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
        fprintf(stderr, "/ [WRN] ignore setgid(), (addr=%04x, bin=%04x)\n", pm->cpu->addr, pm->cpu->bin);
        {
            pm->cpu->r0 = 0;
            clearC(pm->cpu);
        }
        break;
    case 47:
        // getgid
#if MY_STRACE
        fprintf(stderr, "/ getgid(),getegid()\n");
#endif
        pm->cpu->r0 = ((getegid() & 0xff) << 8) | (getgid() & 0xff);
        break;
    case 48:
        // signal
        word0 = fetch(pm->cpu);
        word1 = fetch(pm->cpu);
        fprintf(stderr, "/ [WRN] ignore signal(%d, %04x), (addr=%04x, bin=%04x)\n", word0, word1, pm->cpu->addr, pm->cpu->bin);
        {
            pm->cpu->r0 = 0; // terminate
            clearC(pm->cpu);
        }
        break;
    default:
        // TODO: not implemented
        fprintf(stderr, "/ [ERR] Not implemented: syscall: %d (addr=%04x, bin=%04x)\n", pm->cpu->syscallID, pm->cpu->addr, pm->cpu->bin);
        assert(0);
        break;
    }
}

void syscallString16(machine_t *pm, char *str, size_t size, uint8_t id) {}
#endif
