/* musl spells the speed fields __c_ispeed/__c_ospeed; BSD code (and FreeBSD's
 * own termios) spells them c_ispeed/c_ospeed. The SDK struct has NO member by
 * the BSD name, so these macros are unambiguous -- there is nothing they can
 * shadow. include_next takes the SDK header first, then adds the BSD spelling;
 * cfget/cfset operate on the same struct, so both spellings stay consistent. */
#ifndef PW_FBSD_TERMIOS_H
#define PW_FBSD_TERMIOS_H
#include_next <termios.h>
#ifndef c_ispeed
#define c_ispeed __c_ispeed
#endif
#ifndef c_ospeed
#define c_ospeed __c_ospeed
#endif
#endif
