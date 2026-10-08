/* Collect .ctors into .init_array so a module's constructors actually run.
 *
 * ⚠ On x86_64-scei-ps4, __attribute__((constructor)) emits a legacy .ctors
 * entry, NOT .init_array. prospero-win's generated module_start walks
 * __init_array_start..__init_array_end, which is consequently EMPTY, so no
 * constructor in a PRX ever runs.
 *
 * That is not cosmetic: wine/ps5/pw_wine_dl_libc.c wires the dl backend from a
 * constructor ("The title's first call may be pw_wine_dl_adopt, which bypasses
 * the wrappers below, so configure as the module starts as well"). Without it
 * ops.module_info is NULL, register_module() returns NULL and the title fails
 * at stage 6 (PW_WINE_START_ADOPT) with PW_ERR_STATE -- which is exactly what
 * the console reported on 2026-10-07.
 *
 * Folding .ctors into the same output section makes upstream's walk find them,
 * so no change to prospero-win is needed. .ctors is emitted in reverse
 * priority order by convention; these are independent initialisers, so order
 * does not matter here, and SORT_BY_INIT_PRIORITY keeps any that do declare one
 * in a defined sequence. */
SECTIONS {
  .init_array : ALIGN(8) {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP(*(.ctors))
    PROVIDE_HIDDEN(__init_array_end = .);
  }
} INSERT AFTER .data.rel.ro;
