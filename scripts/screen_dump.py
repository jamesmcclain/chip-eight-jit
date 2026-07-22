#!/usr/bin/env python3
"""Capture the ncurses screen of a CHIP-8 engine run.

The emulator renders the 64x32 display through ncurses to stdout and dumps
the final register/PC/timer state to stderr on exit. `run_dump.py` captures
the stderr state dump while discarding the screen; this script is its
complement: it captures stdout, replays it through a small vt100 renderer so
the CHIP-8 font glyphs land at their real screen positions, and prints the
resulting text. It also prints the stderr state dump, so one invocation gives
both the on-screen verdict (e.g. a test ROM's "OK"/"FAIL") and the final
machine state.

ROMs do not terminate on their own. This tool drives termination: it feeds an
optional sequence of CHIP-8 keypresses (to advance past a test ROM's splash
screen), waits for the screen to settle, then sends `q` to quit. As a safety
net it SIGKILLs the child if it has not exited within a few seconds of the
quit, so the tool itself always returns.

The settle wait may be given in wall-clock seconds (--hold) or in CHIP-8
clock ticks (--ticks). The CHIP-8 delay/sound timers run at 60 Hz and all
three backends pace frame-drawing ROMs to real time at that rate (the JITs
via SIGALRM, the interpreter by syncing each draw to the next 60 Hz slot), so
--ticks N waits N/60 seconds. Pure spin loops are not tick-paced (only draw
wait on the slot), so use --hold for those.

Usage:
  screen_dump.py <engine> <rom> [--keys KEYS] [--ticks N | --hold SECS]

  --keys   CHIP-8 keys to press before quitting, sent 0.4s apart starting at
           0.3s (default: none). Use to dismiss a "press any key" splash.
  --ticks  Quit after N 60 Hz ticks of settle time (waits N/60 s).
  --hold   Quit after SECS of settle time (default: 2.0). Ignored if --ticks.

Examples:
  screen_dump.py chip8-interp roms/test-roms/4-flags.ch8 --keys 0 --ticks 180
  screen_dump.py chip8-interp roms/PONG --hold 1.5
"""
import os, pty, fcntl, termios, struct, time, sys, select, re, argparse


# DEC Special Graphics (the ACS charset ncurses enters with ESC(0) -> Unicode.
# 'a' is ACS_CKBOARD, the glyph ncurses_io.c draws for an "on" CHIP-8 pixel;
# the l/k/m/j/q/x family is the box border box() draws.
ACS = {
    '`': '◆', 'a': '█', '0': '█', 'f': '°', 'g': '±', 'h': '▒', '~': '·',
    'j': '┘', 'k': '┐', 'l': '┌', 'm': '└', 'n': '┼', 'q': '─', 't': '├',
    'u': '┤', 'v': '┴', 'w': '┬', 'x': '│', 'o': '⎺', 'p': '⎻', 'r': '⎼', 's': '⎽',
}


def render(s, rows=40, cols=80):
    """Replay a byte stream through a minimal vt100 renderer into a grid.

    Handles the cursor-positioning and erase sequences ncurses emits
    (CSI H/f, A/B/C/D, J, K), the DEC Special Graphics charset (ESC(0/ESC(B)
    used for the box border and the ACS_CKBOARD pixel glyph, mapped to
    readable Unicode), and ignores SGR/attributes and private modes, so
    glyphs land at their true screen coordinates."""
    grid = [[' '] * cols for _ in range(rows)]
    r = c = 0
    i = 0
    n = len(s)
    state = 'ground'
    params = ''
    g0_graphics = False  # G0 designated DEC Special Graphics (ESC(0)
    g1_graphics = False  # G1 designated DEC Special Graphics (ESC)0)
    gl = 0               # which charset (G0/G1) is locked in for printing

    def clamp_row(v): return max(0, min(rows - 1, v))
    def clamp_col(v): return max(0, min(cols - 1, v))

    while i < n:
        ch = s[i]
        if state == 'ground':
            if ch == '\x1b':
                state = 'esc'
            elif ch == '\r':
                c = 0
            elif ch == '\n':
                r = clamp_row(r + 1)
            elif ch == '\b':
                c = clamp_col(c - 1)
            elif ch == '\t':
                c = clamp_col(c + 8 - c % 8)
            elif ch == '\x0e':   # SO: lock G1 in (ncurses puts ACS in G1)
                gl = 1
            elif ch == '\x0f':   # SI: lock G0 in (ASCII)
                gl = 0
            elif ch >= ' ':      # printable (ACS glyphs are >= 0x60)
                if 0 <= r < rows and 0 <= c < cols:
                    acs = (gl == 1 and g1_graphics) or (gl == 0 and g0_graphics)
                    grid[r][c] = (ACS.get(ch, ch) if acs else ch)
                c = clamp_col(c + 1)
            # else: other control char (BEL, etc.): ignore, still advance
            i += 1
        elif state == 'esc':
            if ch == '[':
                state = 'csi'; params = ''
            elif ch == ']':
                state = 'osc'
            elif ch == '(':
                state = 'g0'
            elif ch == ')':
                state = 'g1'
            elif ch in '*+':
                state = 'charset'
            else:
                state = 'ground'
            i += 1
        elif state == 'csi':
            # accumulate numeric params, ';', and private/intermediate prefixes
            if ch in '0123456789;?<>=':
                params += ch; i += 1
            else:
                # final byte; only the cursor/erase commands carry coordinates
                if ch in 'HfABCDJKdG':
                    clean = params.lstrip('?<>=')
                    pp = [int(p) if p else 0 for p in clean.split(';')] if clean else []
                    if ch in 'Hf':
                        nr = (pp[0] - 1) if len(pp) > 0 and pp[0] else 0
                        nc = (pp[1] - 1) if len(pp) > 1 and pp[1] else 0
                        r = clamp_row(nr); c = clamp_col(nc)
                    elif ch == 'd':   # VPA: row absolute (1-based)
                        r = clamp_row((pp[0] - 1) if pp and pp[0] else 0)
                    elif ch == 'G':   # CHA: column absolute (1-based)
                        c = clamp_col((pp[0] - 1) if pp and pp[0] else 0)
                    elif ch == 'A':
                        r = clamp_row(r - (pp[0] if pp and pp[0] else 1))
                    elif ch == 'B':
                        r = clamp_row(r + (pp[0] if pp and pp[0] else 1))
                    elif ch == 'C':
                        c = clamp_col(c + (pp[0] if pp and pp[0] else 1))
                    elif ch == 'D':
                        c = clamp_col(c - (pp[0] if pp and pp[0] else 1))
                    elif ch == 'J':
                        mode = pp[0] if pp else 0
                        if mode == 0:
                            for cc in range(c, cols): grid[r][cc] = ' '
                            for rr in range(r + 1, rows): grid[rr] = [' '] * cols
                        elif mode == 1:
                            for cc in range(0, c + 1): grid[r][cc] = ' '
                            for rr in range(0, r): grid[rr] = [' '] * cols
                        elif mode == 2:
                            grid = [[' '] * cols for _ in range(rows)]
                    elif ch == 'K':
                        mode = pp[0] if pp else 0
                        if mode == 0:
                            for cc in range(c, cols): grid[r][cc] = ' '
                        elif mode == 1:
                            for cc in range(0, c + 1): grid[r][cc] = ' '
                        elif mode == 2:
                            grid[r] = [' '] * cols
                # else (SGR 'm', private-mode 'h'/'l', etc.): ignore
                state = 'ground'; i += 1
        elif state == 'g0':
            g0_graphics = (ch == '0')  # ESC(0 = DEC Special Graphics; ESC(B = ASCII
            state = 'ground'; i += 1
        elif state == 'g1':
            g1_graphics = (ch == '0')  # ESC)0 = DEC Special Graphics; ESC)B = ASCII
            state = 'ground'; i += 1
        elif state == 'osc':
            if ch == '\x07':
                state = 'ground'; i += 1
            elif ch == '\x1b' and i + 1 < n and s[i + 1] == '\\':
                state = 'ground'; i += 2
            else:
                i += 1
        elif state == 'charset':
            state = 'ground'; i += 1

    lines = [''.join(row).rstrip() for row in grid]
    return '\n'.join(l for l in lines if l.strip())


def main():
    ap = argparse.ArgumentParser(description="Capture a CHIP-8 engine's ncurses screen + state dump.")
    ap.add_argument("engine")
    ap.add_argument("rom")
    ap.add_argument("--keys", default="", help="CHIP-8 keys to press before quitting, 0.4s apart")
    ap.add_argument("--ticks", type=int, default=None, help="quit after N 60Hz ticks of settle time")
    ap.add_argument("--hold", type=float, default=2.0, help="quit after SECS of settle time (default 2.0)")
    args = ap.parse_args()

    wait_secs = (args.ticks / 60.0) if args.ticks is not None else args.hold

    master, slave = pty.openpty()
    err_r, err_w = os.pipe()
    pid = os.fork()
    if pid == 0:  # child
        os.close(master); os.close(err_r)
        os.dup2(slave, 0); os.dup2(slave, 1); os.dup2(err_w, 2)
        os.close(slave); os.close(err_w)
        os.environ.setdefault('TERM', 'xterm-256color')
        os.execvp(args.engine, [args.engine, args.rom])
        os._exit(127)
    # parent
    os.close(slave); os.close(err_w)
    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 80, 0, 0))

    # Schedule: keys at 0.3s + 0.4s*k, then 'q' after `wait_secs` more seconds.
    key_times = [(0.3 + 0.4 * k, kch.encode()) for k, kch in enumerate(args.keys)]
    quit_time = (key_times[-1][0] if key_times else 0.3) + wait_secs
    deadline = quit_time + 5.0  # hard kill 5s after sending q

    out = b""; err = b""
    t0 = time.time()
    sent = set()
    sent_q = False
    while True:
        now = time.time() - t0
        for t, kb in key_times:
            if kb not in sent and now >= t:
                try: os.write(master, kb)
                except OSError: pass
                sent.add(kb)
        if not sent_q and now >= quit_time:
            try: os.write(master, b"q")
            except OSError: pass
            sent_q = True
        r, _, _ = select.select([master, err_r], [], [], 0.05)
        if master in r:
            try: out += os.read(master, 4096)
            except OSError: pass
        if err_r in r:
            try: err += os.read(err_r, 4096)
            except OSError: pass
        wpid, _ = os.waitpid(pid, os.WNOHANG)
        if wpid != 0:
            # Drain trailing output until both fds hit EOF (pty master raises
            # EIO, stderr pipe returns b''); without this the loop spins on
            # the perpetually-readable EOF condition and never returns.
            eof_m = eof_e = False
            while not (eof_m and eof_e):
                r, _, _ = select.select([master, err_r], [], [], 0.05)
                if not r:
                    continue
                if master in r:
                    try:
                        chunk = os.read(master, 4096); out += chunk
                        if not chunk: eof_m = True
                    except OSError:
                        eof_m = True
                if err_r in r:
                    try:
                        chunk = os.read(err_r, 4096); err += chunk
                        if not chunk: eof_e = True
                    except OSError:
                        eof_e = True
            break
        if now > deadline:
            try: os.kill(pid, 9)
            except OSError: pass
            os.waitpid(pid, 0)
            break

    print("===== SCREEN =====")
    print(render(out.decode("latin-1", "replace")))
    print("===== STDERR (state dump) =====")
    sys.stdout.write(err.decode("latin-1", "replace"))
    sys.stdout.flush()


if __name__ == "__main__":
    main()
