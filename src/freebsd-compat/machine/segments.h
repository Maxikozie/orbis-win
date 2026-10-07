/* FreeBSD 9 <machine/segments.h>, the amd64 selectors ntdll's signal code uses. */
#ifndef _MACHINE_SEGMENTS_H_
#define _MACHINE_SEGMENTS_H_
#define GSEL(s, r)      (((s) << 3) | (r))
#define SEL_UPL         3           /* user privilege level */
#define GUCODE32_SEL    4
#define GUDATA_SEL      5
#define GUCODE_SEL      6
#define GUFS32_SEL      7
#define GUGS32_SEL      8
#endif
