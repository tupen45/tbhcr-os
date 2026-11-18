# Developer Guide: Adding a Syscall

MyOS's `int 0x80` gate (`syscalls.c`) is easy to extend. This guide adds
a third syscall, `beep`, as an example.

## 1. Add the handler branch

In `syscall_handler_c(uint32_t *regs)`, read the syscall number and
arguments from the `regs` array (order: EAX=num, ECX, EDX, EBX):

```c
if (num == 3) {                 // beep(freq_hz) -> EBX
    uint32_t hz = arg_ebx;
    // ... do something with hz ...
}
```

Argument mapping:
- `regs[0]` = EAX = syscall number
- `regs[1]` = ECX = arg2
- `regs[2]` = EDX = arg3
- `regs[3]` = EBX = arg1

## 2. (Optional) Expose to user programs

The only "ABI" is the register convention. A user program triggers it with:

```c
asm volatile(
    "movl %0, %%ebx\n\t"
    "movl $3, %%eax\n\t"     // syscall 3 = beep
    "int $0x80\n\t"
    : : "r"(hz) : "eax","ebx");
```

No header is required — the kernel and the user program agree on numbers
by convention (document them in `syscalls.h`).

## 3. Rebuild

The syscall gate is compiled into `kernel.bin` via `build.sh`; no IDT
schema change is needed because `init_syscalls()` already installs the
`0x80` vector. A new `EAX` value simply hits the existing stub.

## Caveats

- There is **no privilege boundary** — the "user" program runs in ring 0
  and shares the kernel's address space. A syscall is a convention, not a
  security boundary.
- The console (`console_puts`) is the only built-in output sink;
  add your own drawing if you need graphical feedback.
- Keep handlers short; they run synchronously inside the user program's call.

See also: [Syscalls](userland/syscalls.md), [ELF Loader](userland/elf-loader.md).
