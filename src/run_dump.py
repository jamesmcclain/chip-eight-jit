#!/usr/bin/env python3
"""Run a ROM under one engine in a pty, send 'q', capture the stderr
register dump. ncurses reads stdin and writes the display to stdout, so
both go to the pty slave; stderr is captured on a separate pipe so the
ncurses display does not contaminate the register dump.

Usage: run_dump.py <engine> <rom>
"""
import os, pty, fcntl, termios, struct, time, sys, select

def main():
    engine, rom = sys.argv[1], sys.argv[2]
    master, slave = pty.openpty()
    err_r, err_w = os.pipe()
    pid = os.fork()
    if pid == 0:  # child
        os.close(master); os.close(err_r)
        os.dup2(slave, 0); os.dup2(slave, 1); os.dup2(err_w, 2)
        os.close(slave); os.close(err_w)
        os.execvp(engine, [engine, rom])
        os._exit(127)
    # parent
    os.close(slave); os.close(err_w)
    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 80, 0, 0))
    err = b""
    deadline = time.time() + 8.0
    sent_q = False
    t0 = time.time()
    while time.time() < deadline:
        if not sent_q and time.time() - t0 > 0.4:
            try: os.write(master, b"q")
            except OSError: pass
            sent_q = True
        r, _, _ = select.select([master, err_r], [], [], 0.1)
        if master in r:
            try: os.read(master, 4096)  # drain ncurses display, discard
            except OSError: pass
        if err_r in r:
            try: chunk = os.read(err_r, 4096)
            except OSError: chunk = b""
            if chunk: err += chunk
        wpid, status = os.waitpid(pid, os.WNOHANG)
        if wpid != 0:
            while True:
                r, _, _ = select.select([err_r], [], [], 0.1)
                if err_r not in r: break
                try: chunk = os.read(err_r, 4096)
                except OSError: break
                if not chunk: break
                err += chunk
            break
    else:
        try: os.kill(pid, 9)
        except OSError: pass
        os.waitpid(pid, 0)
    sys.stdout.buffer.write(err)
    sys.stdout.buffer.flush()

if __name__ == "__main__":
    main()
