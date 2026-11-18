# ELF Loader (`exec_elf.c`)

MyOS can load and run a **32-bit position-independent ELF** (`ET_DYN`/PIE)
directly from kernel memory. The loader is intentionally tiny — it supports
only **`PT_LOAD`** program headers and assumes the whole image fits in the
kernel heap.

## Entry point

```c
int elf32_load_and_run(const void *elf_data, size_t elf_size);
```

Returns the program's integer exit value, or a negative code on failure:

| Code | Reason |
|------|---------|
| `-1` | Image smaller than the ELF header |
| `-2` | Bad ELF magic (`0x7F 'E' 'L' 'F'`) |
| `-3` | No program headers |
| `-4` | No `PT_LOAD` segment |
| `-5` | `kmalloc` failed (out of heap) |
| `-6` | Segment `p_offset + p_filesz` exceeds image size |

## How it works

1. **Validate** the 16-byte `e_ident` magic and presence of program
   headers (`e_phoff`, `e_phnum`).
2. **Compute extent.** Scan `PT_LOAD` segments to find `min_vaddr` and
   `max_vaddr`; `total = max - min`.
3. **Allocate** `kmalloc(total)` and zero it.
4. **Load** each `PT_LOAD` segment: copy `p_filesz` bytes from
   `buf + p_offset` to `mem + (p_vaddr - min_vaddr)`. (`p_memsz >
   p_filesz` BSS is already zeroed.)
5. **Relocate entry.** `entry_rel = e_entry - min_vaddr`;
   `entry = (int(*)(void))(mem + entry_rel)`.
6. **Init syscalls** (`init_syscalls()`) so the program's `int 0x80`
   works.
7. **Call `entry()`** and run until it returns or issues `exit`.

```c
int r = entry();
if (user_exited) return user_exit_code;   // from syscall
return r;                                       // from main return
```

## Why PIE / ET_DYN?

Because the kernel has a single flat address space with no per-program
base, the loaded program **must** be compiled position-independent and must
not assume a fixed load address. The supplied `userprog/hello.c` is built
exactly this way (see [User Programs](user-programs.md)).

## What user programs can do

- Call kernel syscalls `write` (1) and `exit` (2) via `int 0x80`.
- Use the CPU freely (it runs in ring 0).
- **Must not** call libc, `malloc`, or any external symbol — it is linked
  freestanding and self-contained.

## Invocation from the desktop

The Start-menu "Run embedded hello" entry checks
`get_hello_ptr()` / `get_hello_len()`; if a blob is embedded
(`userprog_blob.c`), it calls `elf32_load_and_run()` and prints the exit
code on the console. Otherwise it prints a "run `make userprog`" hint.

Next: [User Programs](user-programs.md).
