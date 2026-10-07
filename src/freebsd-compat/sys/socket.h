/* ⚠ THREE LINUX SOCKET OPTIONS THIS KERNEL DOES NOT IMPLEMENT.
 *
 * musl's headers define SO_BINDTODEVICE (25), SO_ATTACH_FILTER (26) and
 * IP_UNICAST_IF (50) — Linux's numbers — and this kernel is a FreeBSD
 * derivative with none of them. Wine's server/sock.c selects its
 * bind-to-interface strategy on exactly those three:
 *
 *   #elif defined(IP_UNICAST_IF) && defined(SO_ATTACH_FILTER) && defined(SO_BINDTODEVICE)
 *
 * so it builds a Linux BPF filter, which needs struct sock_filter and would
 * then be attached with a setsockopt this kernel will refuse. Wine has an
 * #else fallback for precisely the platforms without them.
 *
 * ⚠ UNDEFINING IS THE FIX, NOT SUPPLYING net/bpf.h. A BPF header would make it
 * compile and produce a filter that assembles, attaches to nothing, and
 * silently never matches — the worse failure. This is the same family as
 * orbis-compat's SA_* correction: musl's numbers for a kernel that parses
 * FreeBSD's.
 */
#ifndef _FREEBSD_COMPAT_SYS_SOCKET_H_
#define _FREEBSD_COMPAT_SYS_SOCKET_H_
#include_next <sys/socket.h>
#undef SO_ATTACH_FILTER
#undef SO_DETACH_FILTER
#undef SO_BINDTODEVICE
#endif
