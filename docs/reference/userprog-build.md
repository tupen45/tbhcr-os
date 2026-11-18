# User Program Build Deep-Dive

Step-by-step of how `userprog/hello.c` becomes an embedded blob the
kernel can run. See also [User Programs](../userland/user-programs.md)
and [ELF Loader](../userland/elf-loader.md).

## 1. Compile freestanding PIE (`Makefile.inc`)

```bash
gcc -m32 -fPIC -nostdlib -ffreestanding -fno-builtin -Os -Wall -Wextra \
    -o userprog/hello.o -c userprog/hello.c
ld -m elf_i386 -shared -o userprog/hello.so userprog/hello.o
strip --strip-all userprog/hello.so
```

Critical flags:

- `-m32` — must match the 32-bit kernel.
- `-fPIC -nostdlib -ffreestanding` — no libc, position-independent.
- `ld -shared` — produces an `ET_DYN` (PIE) object. `elf32_load_and_run`
  requires exactly this: it computes `entry_rel = e_entry - min_vaddr`,
  so a fixed-load `ET_EXEC` would break.

## 2. Embed as a C byte array (`tools/embed_hello.sh`)

```bash
xxd -i out/hello.so > userprog_hello_inc.c
# wrap into userprog_blob_inc.c with array + accessors
mv userprog_blob_inc.c userprog_blob.c
# write userprog_hello.h with get_hello_ptr()/get_hello_len()
```

Resulting symbols (used by `kernel.c`):

```c
extern const unsigned char *get_hello_ptr(void);
extern size_t get_hello_len(void);
```

`userprog_blob.c` defines `out_hello_so[]` / `out_hello_so_len` and the
two accessors.

## 3. Compile into the kernel (`build.sh`)

`build.sh` checks for `userprog_blob.c` and, if present, compiles it
(`userprog_blob.o`) and links it. The Start-menu "Run embedded hello"
entry then calls:

```c
elf32_load_and_run(get_hello_ptr(), (size_t)get_hello_len());
```

If the blob is absent, the menu entry prints a "run `make userprog` and
`embed-userprog` first" hint instead.

## 4. Runtime

`elf32_load_and_run` validates the ELF magic, loads `PT_LOAD` segments
into a `kmalloc`'d buffer, relocates the entry, calls `init_syscalls()`,
and runs `entry()`. The program uses `int 0x80` (syscall `write`=1,
`exit`=2) — see [Syscalls](../userland/syscalls.md). On return/the
`exit` syscall, the kernel prints the exit code on the console.

## Gotchas

- **Do not** link against libc or use external symbols — the loaded image
  is self-contained.
- **Must** be PIE/ET_DYN (position-independent) or the loader's relative
  entry math is wrong.
- The kernel has **no** loader for `ET_EXEC` or non-`PT_LOAD` segments.
- Regenerate the blob after any change to `hello.c`; stale blobs embed
  old code.

See also: [Developer: Add a Syscall](../reference/dev-add-syscall.md).
