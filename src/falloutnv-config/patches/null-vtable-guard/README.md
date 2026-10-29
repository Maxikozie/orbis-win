# FalloutNV null-vtable guard candidate

This is a game-executable-only candidate for the repeatable Fallout New Vegas
read fault in the list lookup helper at PE RVA `0x659d30`. The faulting virtual
method load is at RVA `0x659d5e`: the list entry is non-null, but its first
DWORD (the vtable pointer) is zero, and the original code reads `[vtable+0x8c]`.

The replacement preserves the original helper's argument/return convention,
callee-saved registers, early `query < 7` return, traversal order (it saves the
next list node before invoking the method), and match behavior. It adds a null
check on the entry's vtable and skips that entry when the vtable is null. This
is a diagnostic workaround candidate; the underlying reason the game list
contains an uninitialized entry is not yet known.

## Version lock

- Input: FalloutNV.exe PE32, image base `0x400000`
- Required input SHA-256: `a50c0e91b5d729ef10a921c32aa0cc20aac5366630056d1d60c669917a936fbb`
- Patched output SHA-256: `f1950861167a2ae3a0c228bf88f0e58e15c6e065865b01c19e3b481d880bc731`
- The patcher rejects any different input and never overwrites the source.
- Only the 80-byte helper body is changed; the executable size and all bytes
  outside that function remain unchanged.

## Recreate the candidate

```sh
python3 patch_falloutnv.py /path/to/original/FalloutNV.exe /path/to/new/FalloutNV.exe
```

Keep a backup of the installed executable and test this as an isolated prefix
change. This patch is not included in the shared Wine package and has not yet
been validated on console hardware.
