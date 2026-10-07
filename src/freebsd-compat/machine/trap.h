/* FreeBSD 9 <machine/trap.h>, amd64 trap numbers. ntdll's signal code maps
 * mc_trapno onto Windows exception codes with these. */
#ifndef _MACHINE_TRAP_H_
#define _MACHINE_TRAP_H_
#define T_PRIVINFLT  1   /* privileged instruction */
#define T_BPTFLT     3   /* breakpoint */
#define T_ARITHTRAP  6   /* arithmetic trap */
#define T_PROTFLT    9   /* protection fault */
#define T_TRCTRAP   10   /* trace trap */
#define T_PAGEFLT   12   /* page fault */
#define T_ALIGNFLT  14   /* alignment fault */
#define T_DIVIDE    18   /* integer divide fault */
#define T_NMI       19   /* non-maskable interrupt */
#define T_OFLOW     20   /* overflow trap */
#define T_BOUND     21   /* bound instruction fault */
#define T_DNA       22   /* device not available */
#define T_DOUBLEFLT 23
#define T_FPOPFLT   24   /* fp coprocessor operand fetch fault */
#define T_TSSFLT    25
#define T_SEGNPFLT  26   /* segment not present fault */
#define T_STKFLT    27   /* stack fault */
#define T_MCHK      28   /* machine check trap */
#define T_XMMFLT    29   /* SIMD floating-point exception */
#endif
