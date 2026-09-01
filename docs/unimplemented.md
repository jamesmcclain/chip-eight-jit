# Unimplemented behavior

Things a full CHIP-8 platform provides that this emulator tracks but does not
act on. Recorded here so the gap is deliberate and the intended fix is known.

## Sound / buzzer (`Fx18`)

CHIP-8 has one audio device: a buzzer that sounds while a countdown timer is
non-zero. See "The Art of CHIP-8" (beyondloom.com/blog/artofchip8.html),
"Output" section, for the expected semantics.

`Fx18` (`LD ST, Vx`) stores `Vx` into `sound_timer`
(`src/interp.c:364`, `src/llvm_jit.cpp:1080`, `src/libgccjit_jit.c:986`), and
every engine counts `sound_timer` down at 60 Hz alongside the delay timer
(`src/interp.c:105`, `src/llvm_jit.cpp:361`, `src/libgccjit_jit.c:211`). No
engine ever turns that into sound: the value is maintained and then ignored.

### Intended implementation

Drive an ncurses `beep()` on the rising edge of `sound_timer > 0` and stop on
the fall to zero, or use `flash()` for an Octo-style silent "visual buzzer".
The edge detection belongs in the shared timer-service path so all three
engines get it from one place. Add the entry point to `src/io.h` next to the
other `*_io` calls, implement it in `src/ncurses_io.c`, and add a no-op stub in
`src/bench_io.c` so headless differential runs (`-DBENCH`) stay silent and
unaffected. Sound has no observable VM state, so `bench_diff.py` is not
impacted either way.
