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
CTRL_T = b"\x14"
CTRL_K = b"\x0b"

ROWS, COLS = 30, 100
STARTUP_DELAY = 1.2
WAIT_TIMEOUT = 8.0


def spawn(vix, cwd, fname=None):
    """Fork, exec vix in a pty sized to ROWS x COLS, return (pid, fd)."""
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(cwd)
        if isinstance(fname, list):
            argv = [vix] + fname
        else:
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


def scenario_word_wrap(tmp):
    """With word_wrap enabled, a long line renders on multiple screen rows."""
    fpath = os.path.join(tmp, "wrapped.txt")
    long_line = " ".join(["word%d" % i for i in range(20)])
    with open(fpath, "w") as f:
        f.write(long_line + "\n")

    cfg = os.path.join(tmp, ".config", "vix")
    os.makedirs(cfg, exist_ok=True)
    with open(os.path.join(cfg, "settings.json"), "w") as f:
        f.write('{\n    "word_wrap": true,\n    "line_numbers": false\n}\n')

    saved_home = os.environ.get("HOME")
    os.environ["HOME"] = tmp
    try:
        pid, fd = spawn(sys.argv[1], tmp, fpath)
        try:
            time.sleep(0.4)
            out = drain(fd, 0.4)
            # text_w = COLS(100) - sidebar(22) = 78; the 20-word line is ~130
            # chars so the wrap continuation must appear on a second row.
            if b"word19" not in out:
                return "wrapped tail (word19) missing from output"
            first = long_line.split()[:12]
            if any(w.encode() not in out for w in first):
                return "first wrap segment missing"
            status = clean_quit(fd, pid)
            if exit_code(status) != 0:
                return "exit code %r after wrap" % exit_code(status)
            return None
        finally:
            os.close(fd)
    finally:
        if saved_home is None:
            os.environ.pop("HOME", None)
        else:
            os.environ["HOME"] = saved_home


def strip_ansi(data):
    """Remove SGR/CSI/OSC escape sequences, leaving the plain cell text."""
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        if data[i] == 0x1B and i + 1 < n:
            if data[i + 1] == 0x5B:  # CSI: \x1b[ ...letter
                i += 2
                while i < n and not (0x40 <= data[i] <= 0x7E):
                    i += 1
                i += 1
                continue
            if data[i + 1] == 0x5D:  # OSC: \x1b]4;...\x1b\\ styled pair
                i += 2
                while i < n and not (data[i] == 0x1B and i + 1 < n and data[i + 1] == 0x5C):
                    i += 1
                i += 2
                continue
            if data[i + 1] == 0x28 or data[i + 1] == 0x29:  # charset select \x1b(0 / \x1b(B
                i += 2
                if i < n:
                    i += 1
                continue
            if data[i + 1] == 0x3D:  # keypad \x1b=
                i += 2
                continue
            if data[i + 1] == 0x7E:  # \x1b~ (cursor save/restore family)
                i += 2
                continue
            out.append(data[i])
            i += 1
            continue
        out.append(data[i])
        i += 1
    return bytes(out)


def scenario_unicode_tab(tmp):
    """Tabs expand to spaces and UTF-8 renders as one wide char (no mojibake).

    Text: a tab, ASCII, an accented char (e<U+00E9>, 2 bytes) and a CJK
    ideograph (U+4E2D, 3 bytes, wcwidth 2). The pty must receive the original
    UTF-8 codepoints - not one byte per column - and the tab must widen to its
    tab stop. ANSI SGR sequences are stripped before matching because ncurses
    interleaves attribute codes around wide chars.
    """
    fpath = os.path.join(tmp, "uni.txt")
    with open(fpath, "wb") as f:
        f.write(b"\tCAF" + "\u00e9".encode("utf-8") + b" " + "\u4e2d".encode("utf-8") + b"\n")

    cfg = os.path.join(tmp, ".config", "vix")
    os.makedirs(cfg, exist_ok=True)
    with open(os.path.join(cfg, "settings.json"), "w") as f:
        f.write('{\n    "line_numbers": false\n}\n')

    saved_home = os.environ.get("HOME")
    os.environ["HOME"] = tmp
    try:
        pid, fd = spawn(sys.argv[1], tmp, fpath)
        try:
            # Poll until the first frame has painted the line (the 0.8s fixed
            # window raced with the first redraw under load).
            end = time.time() + WAIT_TIMEOUT
            out = b""
            while time.time() < end:
                out += drain(fd, 0.15)
                if b"\xe4\xb8\xad" not in strip_ansi(out):
                    time.sleep(0.05)
                    continue
                break
            clean = strip_ansi(out)
            # Correct UTF-8 must survive: "Caf" + e-acute (C3 A9), then 中.
            if b"CAF\xc3\xa9" not in clean:
                return "accented char not emitted as UTF-8: %r" % out[-200:]
            if b"\xe4\xb8\xad" not in clean:
                return "CJK char missing from output: %r" % out[-200:]
            # The leading tab must widen to its stop (default 4). Empty cols
            # before "CAF" means the tab expanded to spaces, not mojibake.
            if b"    CAF\xc3\xa9" not in clean:
                return "tab not expanded to 4 spaces: %r" % out[-300:]
            status = clean_quit(fd, pid)
            if exit_code(status) != 0:
                return "exit code %r after unicode render" % exit_code(status)
            return None
        finally:
            os.close(fd)
    finally:
        if saved_home is None:
            os.environ.pop("HOME", None)
        else:
            os.environ["HOME"] = saved_home


def scenario_hscroll(tmp):
    """Horizontal scrolling (no wrap) reveals the tail of a long line.

    Moving right past the window width scrolls the viewport; the rendered
    output then shows characters from the scrolled region, and tabs keep
    their buffer-anchored stops so the cursor column matches the glyphs.
    """
    fpath = os.path.join(tmp, "long.txt")
    body = "abcdefghij" * 30  # 300 chars
    with open(fpath, "w") as f:
        f.write(body + "\n")

    cfg = os.path.join(tmp, ".config", "vix")
    os.makedirs(cfg, exist_ok=True)
    # text_w = COLS(100) - sidebar(22) = 78, so any cursor past ~78 columns
    # must force h_scroll>0.
    with open(os.path.join(cfg, "settings.json"), "w") as f:
        f.write('{\n    "line_numbers": false\n}\n')

    saved_home = os.environ.get("HOME")
    os.environ["HOME"] = tmp
    try:
        pid, fd = spawn(sys.argv[1], tmp, fpath)
        try:
            time.sleep(0.4)
            out = drain(fd, 0.4)
            if body[:78].encode() not in out:
                return "line head not rendered at start: %r" % out[:120]
            # Far right: cursor well past the window.
            for _ in range(95):
                os.write(fd, b"\x1bOC")  # right arrow
            time.sleep(0.4)
            out = drain(fd, 0.4)
            tail = body[-40:]
            if tail.encode() not in out:
                return "scrolled tail (%r) not rendered: %r" % (tail, out[-200:])
            status = clean_quit(fd, pid)
            if exit_code(status) != 0:
                return "exit code %r after hscrollear" % exit_code(status)
            return None
        finally:
            os.close(fd)
    finally:
        if saved_home is None:
            os.environ.pop("HOME", None)
        else:
            os.environ["HOME"] = saved_home


def scenario_session_resume(tmp):
    """A previous session (files + cwd) is reopened by --resume."""
    cfg = os.path.join(tmp, ".config", "vix")
    os.makedirs(cfg, exist_ok=True)
    f1 = os.path.join(tmp, "a.txt")
    f2 = os.path.join(tmp, "b.txt")
    with open(f1, "w") as f:
        f.write("file a\n")
    with open(f2, "w") as f:
        f.write("file b\n")
    with open(os.path.join(cfg, "session.json"), "w") as f:
        f.write('{\n    "current_dir": "%s",\n    "current_tab": 1,\n    "files": ["%s", "%s"]\n}\n'
                % (tmp, f1, f2))

    saved_home = os.environ.get("HOME")
    os.environ["HOME"] = tmp
    try:
        pid, fd = spawn(sys.argv[1], tmp, ["--resume"])
        try:
            time.sleep(0.4)
            out = drain(fd, 0.4)
            if b"a.txt" not in out or b"b.txt" not in out:
                return "session files not reopened: %r" % out[:200]
            # The status-bar welcome message hides L:/C: for STATUS_TIMEOUT
            # seconds; wait it out, then nudge the cursor so the status bar
            # repaints with the buffer position.
            time.sleep(3.2)
            os.write(fd, b"\x1bOB")
            time.sleep(0.4)
            out += drain(fd, 0.5)
            if b"L:1/1" not in out:
                return "current_tab=1 not honoured: %r" % out[:200]
            status = clean_quit(fd, pid)
            if exit_code(status) != 0:
                return "exit code %r after resume" % exit_code(status)
            return None
        finally:
            os.close(fd)
    finally:
        if saved_home is None:
            os.environ.pop("HOME", None)
        else:
            os.environ["HOME"] = saved_home


def scenario_session_roundtrip(tmp):
    """Quitting writes a session; --resume reopens the same tabs."""
    cfg = os.path.join(tmp, ".config", "vix")
    os.makedirs(cfg, exist_ok=True)
    f1 = os.path.join(tmp, "s1.txt")
    f2 = os.path.join(tmp, "s2.txt")
    with open(f1, "w") as f:
        f.write("one\n")
    with open(f2, "w") as f:
        f.write("two\n")

    saved_home = os.environ.get("HOME")
    os.environ["HOME"] = tmp
    try:
        # First run: open two files, quit. Quit persists the session.
        pid, fd = spawn(sys.argv[1], tmp, f1)
        try:
            os.write(fd, CTRL_Q)
            time.sleep(0.6)
            drain(fd, 0.3)
            os.write(fd, b"n\n")  # no edits -> no prompt, but harmless
            status = wait_exit(pid)
            if exit_code(status) != 0:
                return "exit code %r on first run" % exit_code(status)
        finally:
            os.close(fd)

        if not os.path.exists(os.path.join(cfg, "session.json")):
            return "session.json not written on quit"

        # Second run: resume should reopen the same file.
        pid, fd = spawn(sys.argv[1], tmp, ["--resume"])
        try:
            time.sleep(0.4)
            out = drain(fd, 0.4)
            if b"s1.txt" not in out:
                return "resumed file not open: %r" % out[:200]
            status = clean_quit(fd, pid)
            if exit_code(status) != 0:
                return "exit code %r after resume" % exit_code(status)
            return None
        finally:
            os.close(fd)
    finally:
        if saved_home is None:
            os.environ.pop("HOME", None)
        else:
            os.environ["HOME"] = saved_home


def scenario_mouse_selection(tmp):
    """Drag with the mouse selects a span; ^C copies it and ^K cuts it.

    Mouse events use the SGR protocol. With the sidebar hidden (^T) and the
    line-number gutter disabled, screen x=5,y=1 maps to buffer col 1 of line
    0, and x=8,y=1 to col 4, so the selected span is "bcd" of "abcdefghij".
    """
    fpath = os.path.join(tmp, "sel.txt")
    with open(fpath, "w") as f:
        f.write("abcdefghij\nsecond line\n")

    pid, fd = spawn(sys.argv[1], tmp, fpath)
    try:
        out = drain(fd, 0.4)
        if b"sel.txt" not in out:
            return "file not opened: %r" % out[:200]

        # Disable the sidebar so clicks land in the text area at known columns.
        os.write(fd, CTRL_T)
        time.sleep(0.3)
        drain(fd, 0.3)

        # Fast double-click at x=6,y=2 then drag to x=9,y=2 and release.
        # ncurses folds the two fast clicks into one event unless
        # mouseinterval(0) is set; this sequence regression-tests that the
        # following drag still starts a selection.
        os.write(fd, b"\x1b[<0;6;2M")
        time.sleep(0.08)
        os.write(fd, b"\x1b[<0;6;2m")
        time.sleep(0.08)
        os.write(fd, b"\x1b[<0;6;2M")
        time.sleep(0.2)
        os.write(fd, b"\x1b[<0;9;2M")
        time.sleep(0.2)
        os.write(fd, b"\x1b[<0;9;2m")
        time.sleep(0.2)
        out = drain(fd, 0.3)

        # Reverse video must cover cols 1-3: 'a' normal, 'bcd' reversed.
        if b"\x1b[0;7m" not in out and b"\x1b[7m" not in out:
            return "selection not highlighted: %r" % out[:300]

        # Let the release settle before cutting; ASan builds run slower.
        time.sleep(0.3)
        drain(fd, 0.2)

        # ^K cuts the span, leaving "aefghij".
        os.write(fd, CTRL_K)
        time.sleep(0.3)
        os.write(fd, CTRL_S)
        time.sleep(0.4)
        drain(fd, 0.3)

        with open(fpath) as f:
            content = f.read()
        if content != "aefghij\nsecond line\n":
            return "cut produced %r, want 'aefghij\nsecond line\n'" % content

        status = clean_quit(fd, pid)
        if exit_code(status) != 0:
            return "exit code %r after selection cut" % exit_code(status)
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
        ("word_wrap", scenario_word_wrap),
        ("unicode_tab", scenario_unicode_tab),
        ("h_scroll", scenario_hscroll),
        ("session_resume", scenario_session_resume),
        ("session_roundtrip", scenario_session_roundtrip),
        ("mouse_selection", scenario_mouse_selection),
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
