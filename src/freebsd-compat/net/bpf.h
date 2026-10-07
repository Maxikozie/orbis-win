/* FreeBSD 9 <net/bpf.h> — the filter machinery server/sock.c builds with.
 *
 * ⚠ struct sock_filter IS LINUX'S NAME. FreeBSD calls it bpf_insn with the same
 * four fields in the same order; Wine's server/sock.c uses the Linux spelling
 * under its BPF path. Both are declared so either compiles, and they are
 * layout-identical rather than merely similar. */
#ifndef _NET_BPF_H_
#define _NET_BPF_H_
#include <sys/types.h>

struct bpf_insn {
    u_short code;
    u_char  jt;
    u_char  jf;
    u_int   k;
};
struct sock_filter {
    u_short code;
    u_char  jt;
    u_char  jf;
    u_int   k;
};

/* instruction classes */
#define BPF_CLASS(code) ((code) & 0x07)
#define BPF_LD      0x00
#define BPF_LDX     0x01
#define BPF_ST      0x02
#define BPF_STX     0x03
#define BPF_ALU     0x04
#define BPF_JMP     0x05
#define BPF_RET     0x06
#define BPF_MISC    0x07

/* ld/ldx fields */
#define BPF_SIZE(code)  ((code) & 0x18)
#define BPF_W       0x00
#define BPF_H       0x08
#define BPF_B       0x10
#define BPF_MODE(code)  ((code) & 0xe0)
#define BPF_IMM     0x00
#define BPF_ABS     0x20
#define BPF_IND     0x40
#define BPF_MEM     0x60
#define BPF_LEN     0x80
#define BPF_MSH     0xa0

/* alu/jmp fields */
#define BPF_OP(code)    ((code) & 0xf0)
#define BPF_ADD     0x00
#define BPF_JA      0x00
#define BPF_JEQ     0x10
#define BPF_JGT     0x20
#define BPF_JGE     0x30
#define BPF_JSET    0x40
#define BPF_SRC(code)   ((code) & 0x08)
#define BPF_K       0x00
#define BPF_X       0x08

#define BPF_STMT(code, k)       { (u_short)(code), 0, 0, k }
#define BPF_JUMP(code, k, jt, jf) { (u_short)(code), jt, jf, k }

/* ⚠ SKF_AD_* ARE LINUX ANCILLARY-DATA OFFSETS, NOT FreeBSD's. Wine reaches for
 * them to ask a socket filter for the interface index. FreeBSD's BPF has no
 * such mechanism, so these are Linux's values and a filter using them will not
 * do on this kernel what it does on Linux. Declared so the file compiles;
 * flagged because a filter that assembles and then silently never matches is
 * worse than one that refuses to build. */
#define SKF_AD_OFF      (-0x1000)
#define SKF_AD_IFINDEX  8

#endif /* _NET_BPF_H_ */
