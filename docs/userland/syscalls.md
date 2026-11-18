# Syscall Gate (`syscalls.c`)

MyOS has no memory protection between kernel and user code — everything runs
in one 32-bit ring-0 address space. To run a *user program* loaded by the
ELF loader, the kernel provides a tiny **`int 0x80`** syscall gate.

## IDT setup — `init_syscalls()`

```c
struct idt_entry { uint16_t offset_low; uint16_t sel; uint8_t zero;
                     uint8_t flags; uint16_t offset_high; };
```

`init_syscalls()` zeroes a 256-entry IDT, installs entry `0x80` pointing
at `irq80_stub` (assembled in `irqstubs.S`) with selector `0x08`
(kernel code) and flags `0x8E` (present, 32-bit interrupt gate), then
loads the IDT with `lidt`.

## Calling convention

A user program triggers a syscall with `int 0x80`. The stub pushes the
general registers and calls `syscall_handler_c(regs)`, where `regs` is an
array of `pusha`-order words:

```
regs[0] = EAX   ; syscall number
regs[1] = ECX
regs[2] = EDX
regs[3] = EBX   ; arg1
```

| Call | `EAX` | `EBX` | `ECX` |
|------|--------|--------|--------|
| `write` | `1` | `char *buf` | `int len` |
| `exit`  | `2` | `int code`  | — |

## Implemented syscalls

### `write` (1)

Copies up to 511 bytes from the (shared) user buffer at `EBX` into a
stack `tmp[]`, null-terminates, and prints it via `console_puts()`.

### `exit` (2)

```c
user_exit_code = (int)arg_ebx;
user_exited   = 1;
```

It records the exit code and sets `user_exited`. Crucially, it does
**not** halt — returning lets the user function unwind naturally back to
`elf32_load_and_run()`, which checks `user_exited` and returns the code.

## On-screen console — `console_puts()`

A minimal debugging console draws strings at a scrolling position
(`cons_x=6, cons_y=6`, 8px/10px line spacing, wraps to the top at the
bottom of the framebuffer). Because the desktop owns the framebuffer, the
console is mostly used by the embedded "hello" program and for development
output.

## State

```c
volatile int user_exited   = 0;
volatile int user_exit_code = 0;
```

These are read by the ELF loader after the user entry returns.

## Limitations

- No `read`, no file/device syscalls, no `mmap`, no threads.
- No privilege separation — `int 0x80` is a convention, not an enforcement
  boundary.
- Single simultaneous user program (the kernel is single-threaded).

Next: [ELF Loader](elf-loader.md).
