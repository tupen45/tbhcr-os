# User Programs (`userprog/`)

The `userprog/` directory holds a sample program that the kernel can load
and run through the [ELF loader](elf-loader.md) using the
[syscall gate](syscalls.md).

## `userprog/hello.c`

A 36-line demonstration. It defines `int entry(void)` and uses inline
assembly to invoke the two kernel syscalls:

```c
/* syscall: write(ptr, len) */
asm volatile(
    "movl %0, %%ebx\n\t"   /* arg1 = msg */
    "movl %1, %%ecx\n\t"   /* arg2 = len */
    "movl $1, %%eax\n\t"   /* syscall 1 = write */
    "int $0x80\n\t"
    : : "r"(msg), "r"(len) : "eax","ebx","ecx");

/* syscall: exit(code) */
asm volatile(
    "movl $42, %%ebx\n\t"   /* exit code 42 */
    "movl $2, %%eax\n\t"   /* syscall 2 = exit */
    "int $0x80\n\t");
```

It prints `"Hello from userprog!\n"` (via syscall `write`) and exits with
code `42`. The kernel's console then shows `[user exit]42`.

## Building (`Makefile.inc`, `tools/embed_hello.sh`)

```bash
make -f Makefile.inc userprog        # gcc -m32 -fPIC -nostdlib ... -> out/hello.so
make -f Makefile.inc embed-userprog   # xxd -> userprog_blob.c + userprog_hello.h
```

- `userprog` compiles `hello.c` as a 32-bit freestanding PIE
  (`-fPIC -nostdlib`), links it as a shared object (`-shared`), and
  strips it. The result is a position-independent `ET_DYN` ELF — exactly
  what `elf32_load_and_run()` expects.
- `embed-userprog` runs `tools/embed_hello.sh`, which `xxd -i`'s the
  `.so` into a C byte array (`out_hello_so[]` / `out_hello_so_len`) and
  emits `userprog_hello.h` with `get_hello_ptr()` / `get_hello_len()`.

When `userprog_blob.c` is present, `build.sh` compiles it into
`kernel.bin`; `kernel.c` then detects it at runtime.

## Running it

Open the **Start menu → Run embedded hello**. The desktop calls
`elf32_load_and_run(get_hello_ptr(), get_hello_len())` and prints the
exit code on the on-screen console. If the blob is missing, the menu entry
prints:

```
No embedded program present. Run make userprog and embed-userprog first.
```

## Writing your own

1. Create `userprog/yourname.c` with `int entry(void)`.
2. Use only `int 0x80` syscalls (`write`=1, `exit`=2); no libc.
3. Add a `userprog`/`embed-userprog` rule (or reuse the pattern) and
   regenerate the blob.
4. Rebuild the kernel.

Next: [Desktop & Shell](apps/desktop.md).
