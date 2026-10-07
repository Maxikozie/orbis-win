/* FreeBSD's ACPI ioctl header — the two battery-status bits ntdll reads.
 *
 * ⚠ I GOT THIS WRONG TWICE BEFORE SETTLING IT. First I wrote the file
 * deliberately EMPTY, reasoning that Wine would then take its absent-hardware
 * branch — but configure tests header EXISTENCE, so the empty file set
 * HAVE_DEV_ACPICA_ACPIIO_H and Wine demanded ACPI_BATT_STAT_CHARGING from it.
 * Then I deleted the file, and the build failed with "file not found", because
 * dlls/ntdll/unix/system.c includes it UNCONDITIONALLY under
 *
 *     #elif defined(__FreeBSD__)
 *     #include <dev/acpica/acpiio.h>
 *
 * There is no branch to fall into. On a FreeBSD target Wine always compiles its
 * ACPI battery path, so the header has to exist and has to carry the constants.
 *
 * ⚠ THE PS4 HAS NO /dev/acpi, so the ioctl behind these will fail at runtime and
 * fill_battery_state() returns the error Wine already handles. That is the
 * correct outcome for a console with no removable battery — the declarations
 * exist so the code can reach that failure, not so it can succeed. */
#ifndef _DEV_ACPICA_ACPIIO_H_
#define _DEV_ACPICA_ACPIIO_H_

/* acpi_battinfo.state bits, FreeBSD 9 sys/dev/acpica/acpiio.h */
#define ACPI_BATT_STAT_DISCHARG   0x0001
#define ACPI_BATT_STAT_CHARGING   0x0002
#define ACPI_BATT_STAT_CRITICAL   0x0004
#define ACPI_BATT_STAT_INVALID    (ACPI_BATT_STAT_DISCHARG | ACPI_BATT_STAT_CHARGING)
#define ACPI_BATT_STAT_BST_MASK   (ACPI_BATT_STAT_INVALID | ACPI_BATT_STAT_CRITICAL)
#define ACPI_BATT_STAT_NOT_PRESENT ACPI_BATT_STAT_BST_MASK

#endif
