# Project notes for Codex

- PS4 `.sorbisdmp` crash dumps must be analyzed with the local dump/debugging
  tools available on this workstation. Do not conclude that a dump is
  undecodable based only on its `FACECS` header, `file` output, or readable
  strings. Find and run the existing decoder/debugger workflow, then symbolize
  the result against the exact package binaries and symbols.
- Reuse the established crash workflow; do not rediscover it on every run:
  arm `scripts/watch-coredump.py HOST 2121 KLOG OUT_DIR TITLE_ID` against the
  active appended klog before launch (GoldHEN deletes dumps shortly after
  reporting them), then retain the retrieved `.sorbisdmp`, matching klog
  window, package hash/build marker, and exact runtime symbols together under
  `/home/zpeirce/codex_scratch/<title>-coredumps/`. For Wine guest faults, use
  the first-exception register/context and bounded guest-stack/object records
  emitted by the PS4 Wine diagnostics, correlate their timestamp with klog,
  then map the guest EIP/RVA against the exact game PE and map PRX PCs against
  the exact loaded module plus its matching `.shared.elf`. Use
  `x86_64-w64-mingw32-objdump -d` for PE disassembly and
  `llvm-addr2line -e <matching-unstripped-elf> <module-relative-address>` for
  ELF symbols. Distinguish guest CPU faults from SCE async/GPU suspend-timeout
  exceptions: the latter explicitly have no thread context, so they do not
  identify a guest CPU fault or prove which guest instruction failed. If they
  coincide with a user action, correlate GPU/submit logs before dismissing the
  action; a stalled render submission may still be relevant. Check the handoff
  for an identical signature before proposing another run.
- Preserve crash dumps and record their path, title ID, package/build marker,
  selected CPU mode, and matching symbols before analysis.
- Before investigating a new crash, search earlier runs, handoffs, and logs for
  the same signature and reuse those findings instead of repeating ruled-out
  experiments.
- When bringing up a new game, prefer controlled A/B runs against a known
  baseline, changing one relevant variable at a time where practical.
- Build relocatable packages by default and keep the relocation fix in every
  test package. Do not create or upload a nonrelocatable package unless the
  user explicitly asks for that comparison.
- Keep project changes additive: preserve known-good fixes and behavior in
  each new package (including relocation, input, filesystem, and close-path
  fixes). Before packaging, compare the candidate against the latest known-good
  baseline and account for every omitted or changed fix. Remove a fix only for
  an explicitly scoped A/B test, and restore it in the next package.
- Do not use `/tmp` for project artifacts; use `/home/zpeirce/codex_scratch`.
- Fallout New Vegas gameplay testing should default to the native WoW64 CPU
  backend. Do not ask the user to repeat translator runs unless a specific,
  controlled A/B is necessary to distinguish a native-only regression.
