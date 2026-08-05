#!/usr/bin/env python3
"""
tests/pty_smoke.py - End-to-end tests for the real vix binary.

Spawns vix in a pseudo-terminal (so ncurses sees a real tty), feeds
keystrokes, and checks both the rendered output and the exit status.
Every scenario uses a fresh vix process so a hang in one does not leak
into the next.

Usage: pty_smoke.py /path/to/vix
"""

import os
import pty
import select
import struct
import sys
import tempfile
import time
import fcntl
import termios

ESC = b"\x1b"
CTRL_H = b"\x08"
CTRL_Q = b"\x11"
CTRL_S = b"\x13"
CTRL_G = b"\x07"

ROWS, COLS = 30, 100
STARTUP_DELAY = 1.2
WAIT_TIMEOUT = 8.0


def spawn(vix, cwd, fname=None):
    """Fork, exec vix in a pty sized to ROWS x COLS, return (pid, fd)."""
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(cwd)
        argv = [vix] + ([fname] if fname else [])
        os.execv(vix, argv)
    # Give ncurses a real terminal geometry so it renders without wrapping.
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
    time.sleep(STARTUP_DELAY)
    return pid, fd


def drain(fd, seconds):
    """Read whatever the child wrote over `seconds`, returning bytes."""
    end = time.time() + seconds
    out = b""
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                chunk = os.read(fd, 4096)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
        else:
            time.sleep(0.02)
    return out


def wait_exit(pid, timeout=WAIT_TIMEOUT):
    """Reap pid; return its status, or None if it did not exit in time."""
    end = time.time() + timeout
    while time.time() < end:
        wpid, status = os.waitpid(pid, os.WNOHANG)
        if wpid == pid:
            return status
        time.sleep(0.05)
    return None


def exit_code(status):
    if status is None:
        return None
    if os.WIFEXITED(status):
        return os.WEXITSTATUS(status)
    if os.WIFSIGNALED(status):
        return -os.WTERMSIG(status)
    return -1


def clean_quit(fd, pid):
    """Send Ctrl+Q and answer 'n' to any unsaved-changes prompt."""
    os.write(fd, CTRL_Q)
    time.sleep(0.6)
    out = drain(fd, 0.3)
    if b"Save?" in out:
        os.write(fd, b"n\n")
    status = wait_exit(pid)
    return status


def scenario_open_edit_save(tmp):
    """Open a file, type, save with ^S, quit; the file must contain the text."""
    fpath = os.path.join(tmp, "hello.txt")
    with open(fpath, "w") as f:
        f.write("")

    pid, fd = spawn(sys.argv[1], tmp, fpath)
    try:
        os.write(fd, b"hello vix")
        time.sleep(0.4)
        os.write(fd, CTRL_S)  # filename known -> saves directly
        time.sleep(0.5)
        drain(fd, 0.2)
        status = clean_quit(fd, pid)
        if exit_code(status) != 0:
            return "exit code %r" % exit_code(status)
        with open(fpath) as f:
            got = f.read()
        if got != "hello vix":
            return "file content %r" % got
        return None
    finally:
        os.close(fd)


def scenario_help_menu(tmp):
    """^H opens the help window; a key closes it; editor keeps running."""
    fpath = os.path.join(tmp, "help.txt")
    with open(fpath, "w") as f:
        f.write("")

    pid, fd = spawn(sys.argv[1], tmp)
    try:
        os.write(fd, CTRL_H)
        time.sleep(0.5)
        out = drain(fd, 0.3)
        if b"VIX HELP" not in out:
            return "help title missing from output"
        os.write(fd, b" ")  # any key dismisses the window
        time.sleep(0.4)
        drain(fd, 0.2)
        # still alive: cursor moves or another ^H reopens the window
        os.write(fd, CTRL_H)
        time.sleep(0.5)
        out = drain(fd, 0.3)
        if b"VIX HELP" not in out:
            return "help did not reopen"
        os.write(fd, b" ")
        time.sleep(0.3)
        drain(fd, 0.2)
        status = clean_quit(fd, pid)
        if exit_code(status) != 0:
            return "exit code %r after help" % exit_code(status)
        return None
    finally:
        os.close(fd)


def scenario_unsaved_prompt(tmp):
    """Typing then ^Q prompts to save; answering 'n' drops changes."""
    fpath = os.path.join(tmp, "unsaved.txt")
    with open(fpath, "w") as f:
        f.write("original\n")

    pid, fd = spawn(sys.argv[1], tmp, fpath)
    try:
        os.write(fd, b"edited!")
        time.sleep(0.4)
        drain(fd, 0.2)
        os.write(fd, CTRL_Q)
        time.sleep(0.6)
        out = drain(fd, 0.3)
        if b"Save?" not in out:
            return "unsaved prompt missing"
        os.write(fd, b"n\n")
        status = wait_exit(pid)
        if exit_code(status) != 0:
            return "exit code %r after answering no" % exit_code(status)
        with open(fpath) as f:
            got = f.read()
        if got != "original\n":
            return "file changed despite answering no: %r" % got
        return None
    finally:
        os.close(fd)


def scenario_crlf_roundtrip(tmp):
    """A CRLF file edited then saved keeps its CRLF endings."""
    fpath = os.path.join(tmp, "win.txt")
    with open(fpath, "wb") as f:
        f.write(b"one\r\ntwo\r\n")

    pid, fd = spawn(sys.argv[1], tmp, fpath)
    try:
        os.write(fd, b"X")  # dirty the buffer
        time.sleep(0.4)
        drain(fd, 0.2)
        os.write(fd, CTRL_S)
        time.sleep(0.5)
        drain(fd, 0.2)
        status = clean_quit(fd, pid)
        if exit_code(status) != 0:
            return "exit code %r" % exit_code(status)
        with open(fpath, "rb") as f:
            got = f.read()
        if got != b"Xone\r\ntwo\r\n":
            return "CRLF not preserved: %r" % got
        return None
    finally:
        os.close(fd)


def scenario_save_as(tmp):
    """Ctrl+S on an untitled buffer prompts 'Save As' and writes the file."""
    pid, fd = spawn(sys.argv[1], tmp)
    try:
        os.write(fd, b"brand new")
        time.sleep(0.4)
        drain(fd, 0.2)
        os.write(fd, CTRL_S)
        time.sleep(0.6)
        out = drain(fd, 0.3)
        if b"Save As" not in out:
            return "'Save As' prompt missing"
        os.write(fd, b"untitled.txt\n")
        time.sleep(0.5)
        drain(fd, 0.2)
        status = clean_quit(fd, pid)
        if exit_code(status) != 0:
            return "exit code %r after save-as" % exit_code(status)
        fpath = os.path.join(tmp, "untitled.txt")
        if not os.path.exists(fpath):
            return "save-as file not created"
        with open(fpath) as f:
            got = f.read()
        if got != "brand new":
            return "save-as content %r" % got
        return None
    finally:
        os.close(fd)


def main():
    if len(sys.argv) < 2:
        print("usage: pty_smoke.py /path/to/vix", file=sys.stderr)
        return 2

    scenarios = [
        ("open_edit_save", scenario_open_edit_save),
        ("help_menu", scenario_help_menu),
        ("unsaved_prompt", scenario_unsaved_prompt),
        ("crlf_roundtrip", scenario_crlf_roundtrip),
        ("save_as", scenario_save_as),
    ]

    failures = 0
    with tempfile.TemporaryDirectory(prefix="vix_pty_") as tmp:
        for name, fn in scenarios:
            err = fn(tmp)
            if err:
                print("FAIL %-16s %s" % (name, err))
                failures += 1
            else:
                print("PASS %-16s" % name)

    print("summary: %d/%d scenarios passed" % (len(scenarios) - failures, len(scenarios)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
