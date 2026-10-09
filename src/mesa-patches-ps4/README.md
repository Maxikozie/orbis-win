# PS4 Mesa patches

Base: `mesa-build/mesa-ps4` commit `1a59cc147b54e3970814b37313e2f27357672e9d`.
Apply in filename order. 0001 preserves the Gladius detection change that was
already present in the ignored checkout before this session. 0002 implements
placed CPU aliases and reserve/unmap without changing the GPU VA. 0003 fixes
three defects in the ORBIS_DUMP_HUNG instrumentation: the dumper was a
process-wide one-shot that spent itself on a hang that recovered, the
fence-slot wait that the fatal hang actually takes never dumped at all, and the
user-data block stated a fill-overrun conclusion that its own dispatch grid
contradicts. See docs/FINDINGS.md, "The GPU's own account of the hang".

The working checkout already contains all three patches. Check the newest with
`git -C mesa-build/mesa-ps4 apply --check --reverse ../../src/mesa-patches-ps4/0003-orbis-dump-the-hang-that-kills.patch`.
The checkout is gitignored: these patches preserve its changes.

Build the archive using its Mesa development environment, then use
`./scripts/build.sh vkmapprobe --pkg --neo --dev-mesa` for the hardware test.
Host checks exercised the actual mapping functions with shared memfd-backed
views, including failed map/unmap/reservation calls; hardware validation is
required for GPU access and console coherency. Hardware passed GTT, visible VRAM and cached GTT: six GPU copies with zero
mismatches, reserve/remap and cleanup; `DONE tested=3 result=PASS`.
