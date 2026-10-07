# freebsd-compat

FreeBSD 9 headers the OpenOrbis SDK omits although the platform has them.

This is `orbis-compat` §2.2's category — *"Headers the SDK omits although the
platform has them"* — and the overlay already carries eight of the same kind
(`cpuset.h`, `dirent.h`, `endian.h`, `ioccom.h`, `ioctl.h`, `param.h`,
`sysctl.h`, `umtx.h`). Wine needs these six more.

⚠ **These are declarations, not implementations.** The kernel on the other side
is a FreeBSD 9 derivative and already understands every one of these ioctls and
syscalls; only the headers describing them are absent from the SDK. The values
are FreeBSD 9's, which is what that kernel parses.

⚠ **Nothing here invents a value.** Where a constant could not be taken from
FreeBSD 9's own headers it is not defined at all, so a use fails to compile
rather than compiling against a guess. A wrong ioctl number does not error — it
addresses a different command.

Measured 2026-10-07: without these, Wine's Unix side produces 307 compile errors
and none of them are in prospero-win's patched arms.
