/* Keep .data.sce_module_param as its own output section on a PRX link.
 *
 * ⚠ WITHOUT THIS lld MERGES IT INTO .data and create-fself crashes:
 *     panic: nil pointer dereference in GenerateProgramHeaders
 * because it looks the section up by name (OELFGenProgramHeaders.go:51,
 * ".data.sce_module_param") and dereferences the nil result.
 *
 * crtlib.o supplies the section — module_start and module_stop do reach the
 * output, so the object is linked — but nothing references the param block, so
 * it needs KEEP as well as a home. The executable script (orbis-tls.ld) already
 * does exactly this for both .data.sce_process_param and this one; a shared
 * link does not use that script, so the rule is restated here.
 *
 * INSERT BEFORE .data rather than a bare SECTIONS block: this augments lld's
 * default layout instead of replacing it. */
SECTIONS {
  .data.sce_module_param : ALIGN(0x4000) {
    KEEP(*(.data.sce_module_param))
  }
} INSERT BEFORE .data;
