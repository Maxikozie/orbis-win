# Low-base release probe

Build with `scripts/build.sh lowbaseprobe --pkg`. This produces a separate
base-mode app, `GLDS10042`, and packages `lowbase_handoff.prx` alongside the
eboot. It does not include Wine, Mesa, or a game prefix.

The eboot reports its module segments, loads the PRX, finds its `PRXDESC1`
export, and transfers control without returning. The PRX then tries to release
eboot segments that overlap the fixed PE range `0x400000-0x55c000`. It reserves
that complete range with the kernel's no-overwrite flag and maps a writable test
page at `0x400000`. The PRX stays active afterward; the test deliberately does
not return to the eboot after unmapping it.

Read the klog records prefixed `lowbaseprobe:`. The useful success sequence is:

1. `pre-release reserve` fails because the eboot occupies part of the range.
2. Every overlapping segment reports `munmap rc=0x0`.
3. `post-release reserve` reports `rc=0x0` and returns `0x400000`.
4. The first-page map succeeds and reads back `0x4c4f5742`.

Any failed unmap or post-release reservation means the range is not yet
available. This probe only establishes that the address can be freed and reused
while code continues in a PRX. It does not yet establish that Sony's process
metadata and libc remain safe for a full Wine session after the eboot is gone.

The experiment unmaps this app's own executable segments. Run only this isolated
probe package; the expected failure mode is this test app terminating or
becoming unresponsive, not a change to the existing Wine package or game data.
