/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Define __dso_handle inside the image, so the title can be linked anywhere.
 *
 * ⚠ Without this the title cannot be moved off 0x400000, and 0x400000 is where
 * a 32-bit Windows executable has to go.
 *
 * lld synthesises __dso_handle at address 0 for an executable. Every reference
 * to it is R_X86_64_PC32, so with the image at its default base the distance is
 * nothing and it links; move the image and the distance becomes the base
 * itself, which overflows a signed 32-bit displacement:
 *
 *   orbis-ld: error: relocation R_X86_64_PC32 out of range: -3221640031
 *             references '__dso_handle'
 *
 * Defining it here puts it in this image's own data, within reach of the code
 * that references it, and the link succeeds at any base -- measured at
 * 0xC0000000 and at 0x200000000, both with no errors. The code model never
 * needed changing; only this one synthetic symbol did. */

void *__dso_handle __attribute__((visibility("hidden"))) = &__dso_handle;
