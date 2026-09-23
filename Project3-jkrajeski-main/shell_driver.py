"""
shell_driver.py  --  shared Shell driver for all three assessment scripts.

Reads output one character at a time in a background thread so the
prompt (which has no trailing newline) is captured reliably.
"""
import subprocess, threading, queue, time

PROMPT   = ">"     # substring that identifies the shell prompt
SETTLE   = 0.20    # seconds of silence after prompt before stopping


class Shell:
    def __init__(self, binary, cwd):
        self.proc = subprocess.Popen(
            [binary],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            cwd=cwd, bufsize=0
        )
        self.cwd  = cwd
        self.bin  = binary
        self._q   = queue.Queue()
        t = threading.Thread(target=self._reader, daemon=True)
        t.start()
        self._drain(timeout=3)   # consume initial prompt

    def _reader(self):
        """Read one byte at a time and put decoded chars on the queue."""
        try:
            while True:
                b = self.proc.stdout.read(1)
                if not b:
                    break
                self._q.put(b.decode("utf-8", errors="replace"))
        except Exception:
            pass
        self._q.put(None)   # EOF sentinel

    def _drain(self, timeout=5):
        """
        Collect chars until the prompt string appears and the shell
        goes quiet for SETTLE seconds, or until timeout elapses.
        Returns (text, hung).
        """
        buf        = ""
        saw_prompt = False
        deadline   = time.time() + timeout

        while time.time() < deadline:
            try:
                ch = self._q.get(timeout=0.02)
            except queue.Empty:
                if saw_prompt:
                    # Shell went quiet after prompt — try one more settle wait
                    try:
                        ch = self._q.get(timeout=SETTLE)
                        if ch is None:
                            break
                        buf += ch
                        saw_prompt = buf.rstrip(" ").endswith(PROMPT)
                    except queue.Empty:
                        break     # settled — done
                continue

            if ch is None:    # EOF
                break
            buf += ch
            # Prompt: e.g. "tush> " — look for > optionally followed by spaces
            # at the end of what we have so far
            trimmed = buf.rstrip(" ")
            if trimmed.endswith(PROMPT):
                saw_prompt = True

        return buf, not saw_prompt

    def run(self, command, timeout=5):
        if not self.is_alive():
            return "", False
        try:
            self.proc.stdin.write((command + "\n").encode())
            self.proc.stdin.flush()
        except BrokenPipeError:
            return "", False
        return self._drain(timeout=timeout)

    def run_timed(self, command, wait=2.5):
        """Returns (output, elapsed).  Used for & background timing check."""
        if not self.is_alive():
            return "", 0.0
        try:
            self.proc.stdin.write((command + "\n").encode())
            self.proc.stdin.flush()
        except BrokenPipeError:
            return "", 0.0
        start = time.time()
        out, _ = self._drain(timeout=wait + 0.5)
        return out, time.time() - start

    def restart(self, binary=None, cwd=None):
        self.proc.kill(); self.proc.wait()
        self.__init__(binary or self.bin, cwd or self.cwd)

    def close(self):
        if not self.is_alive():
            return
        try:
            self.proc.stdin.write(b"exit\n")
            self.proc.stdin.flush()
        except Exception:
            pass
        try:
            self.proc.wait(timeout=3)
        except Exception:
            self.proc.kill()

    def is_alive(self):
        return self.proc.poll() is None
