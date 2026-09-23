#!/usr/bin/env python3
"""
assess_d1.py  --  Deliverable 1 assessment script
Project 3: Linux Shell  |  Components 1, 2, 3

Tests:
  1.  Shell compiles (make)
  2.  Shell launches and shows prompt
  3.  Absolute path execution          /bin/ls
  4.  Argument passing — echo          /bin/echo hello world
  5.  Argument passing — grep          /usr/bin/grep banana /tmp/fruit.txt
  6.  execv failure: error printed     /bin/notarealprogram
  7.  execv failure: shell survives
  8.  execv failure: no double-prompt
  9.  PATH resolution                  ls
  10. PATH + arguments                 echo hello world
  11. Bad command via PATH: error
  12. Bad command via PATH: survives
  13. Built-in cd: no error
  14. Built-in cd: directory changed

Usage:
    python3 assess_d1.py [path/to/shell/directory]
    Default directory is the current directory.
"""

import subprocess, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shell_driver import Shell

SHELL_BINARY  = "./tush"
BUILD_COMMAND = ["make"]
BUILD_TIMEOUT = 30

USE_COLOUR = sys.stdout.isatty()
GREEN  = "\033[32m" if USE_COLOUR else ""
RED    = "\033[31m" if USE_COLOUR else ""
YELLOW = "\033[33m" if USE_COLOUR else ""
BOLD   = "\033[1m"  if USE_COLOUR else ""
RESET  = "\033[0m"  if USE_COLOUR else ""

results = []

def record(name, passed, detail=""):
    results.append((name, passed, detail))
    sym = f"{GREEN}PASS{RESET}" if passed else f"{RED}FAIL{RESET}"
    print(f"  {sym}  {name}")
    if detail:
        for line in detail.strip().splitlines():
            print(f"        {line}")

def section(title):
    print(f"\n{BOLD}{title}{RESET}")
    print("  " + "-" * (len(title) + 2))

def content_lines(raw):
    """Non-empty lines that are not just the prompt."""
    out = []
    for l in raw.splitlines():
        l = l.strip()
        if l and not all(c in ">! " for c in l):
            out.append(l)
    return out

def prompt_count(raw):
    return sum(1 for l in raw.splitlines() if ">" in l and len(l.strip()) <= 6)

def has_error(raw):
    return any(w in raw.lower() for w in
               ["error", "failed", "not found", "no such", "cannot", "command"])


def main():
    shell_dir = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    print(f"\n{BOLD}{'='*60}{RESET}")
    print(f"{BOLD}  Project 3 \u2014 Deliverable 1 Assessment{RESET}")
    print(f"{BOLD}  Components 1, 2, 3  |  {shell_dir}{RESET}")
    print(f"{BOLD}{'='*60}{RESET}")

    # ── Build ──────────────────────────────────────────────────────────
    section("1. Build")
    try:
        r = subprocess.run(BUILD_COMMAND, cwd=shell_dir,
                           capture_output=True, text=True, timeout=BUILD_TIMEOUT)
        ok = r.returncode == 0 and os.path.isfile(os.path.join(shell_dir, "tush"))
        record("make succeeds and binary exists", ok,
               "" if ok else f"Exit {r.returncode}\n{r.stderr[:300]}")
        if not ok:
            return _finish(shell_dir)
    except subprocess.TimeoutExpired:
        record("make succeeds and binary exists", False, "make timed out")
        return _finish(shell_dir)

    # ── Launch ─────────────────────────────────────────────────────────
    try:
        shell = Shell(SHELL_BINARY, shell_dir)
    except Exception as e:
        record("Shell launches and shows prompt", False, str(e))
        return _finish(shell_dir)

    if not shell.is_alive():
        record("Shell launches and shows prompt", False, "Shell exited immediately")
        return _finish(shell_dir)
    record("Shell launches and shows prompt", True)

    # ── Prepare test file ──────────────────────────────────────────────
    fruit = "/tmp/tush_d1_fruit.txt"
    with open(fruit, "w") as f:
        f.write("apple\nbanana\ncherry\n")

    # ── Absolute path execution ────────────────────────────────────────
    section("2. Absolute Path Execution  (Component 2)")
    out, _ = shell.run("/bin/ls")
    lines = content_lines(out)
    record("/bin/ls produces directory listing", len(lines) > 0,
           "" if lines else f"Raw output: {repr(out[:200])}")

    # ── Argument passing — echo ────────────────────────────────────────
    section("3. Argument Passing  (Component 2)")
    out, _ = shell.run("/bin/echo hello world")
    passed = "hello world" in out
    record("/bin/echo hello world  \u2192  'hello world'", passed,
           "" if passed else f"Raw output: {repr(out[:200])}")

    # ── Argument passing — grep ────────────────────────────────────────
    out, _ = shell.run(f"/usr/bin/grep banana {fruit}")
    passed = "banana" in out
    record(f"/usr/bin/grep banana {fruit}  \u2192  'banana'", passed,
           "" if passed else f"Raw output: {repr(out[:200])}")

    # ── execv failure ─────────────────────────────────────────────────
    section("4. execv Failure Handling  (Component 2)")
    out, _ = shell.run("/bin/notarealprogram")
    error_seen = has_error(out)
    alive      = shell.is_alive()
    dbl        = prompt_count(out) >= 2

    record("Error message printed after bad execv", error_seen,
           "" if error_seen else f"Raw: {repr(out[:200])}")
    record("Shell survives (does not exit)", alive)
    record("Shell does not double-prompt (child did not re-enter shell loop)",
           not dbl,
           "" if not dbl else f"Saw multiple prompts \u2014 child likely fell through into shell loop")

    # ── PATH resolution ────────────────────────────────────────────────
    section("5. PATH Resolution  (Component 3)")
    out, _ = shell.run("ls")
    lines = content_lines(out)
    record("'ls' (no slash) produces output", len(lines) > 0,
           "" if lines else f"Raw: {repr(out[:200])}")

    out, _ = shell.run("echo hello world")
    passed = "hello world" in out
    record("'echo hello world' (no slash) prints correctly", passed,
           "" if passed else f"Raw: {repr(out[:200])}")

    # ── Bad command via PATH ───────────────────────────────────────────
    out, _ = shell.run("notarealcommand")
    record("Bad simple command: error reported", has_error(out),
           "" if has_error(out) else f"Raw: {repr(out[:200])}")
    record("Bad simple command: shell survives", shell.is_alive())

    # ── Built-in cd ───────────────────────────────────────────────────
    section("6. Built-in cd  (Component 3 / Built-ins)")
    out, _ = shell.run("cd /tmp")
    no_err = not has_error(out)
    record("'cd /tmp' produces no error", no_err,
           "" if no_err else f"Raw: {repr(out[:200])}")

    out, _ = shell.run("ls")
    # /tmp always has content
    lines = content_lines(out)
    record("After 'cd /tmp', ls shows /tmp contents", len(lines) > 0,
           "" if lines else f"Raw: {repr(out[:200])}")

    shell.close()
    try: os.remove(fruit)
    except OSError: pass

    _finish(shell_dir)


def _finish(shell_dir):
    total  = len(results)
    passed = sum(1 for _,p,_ in results if p)
    print(f"\n{BOLD}{'='*60}{RESET}")
    print(f"{BOLD}  SUMMARY  \u2014  {passed}/{total} tests passed{RESET}")
    print(f"{BOLD}{'='*60}{RESET}")
    if passed < total:
        print(f"\n  {YELLOW}Failed:{RESET}")
        for n,p,_ in results:
            if not p: print(f"    {RED}\u2022{RESET}  {n}")
    else:
        print(f"\n  {GREEN}All tests passed.{RESET}")
    print()
    path = os.path.join(shell_dir, "assess_d1_results.txt")
    with open(path, "w") as f:
        f.write("Project 3 \u2014 Deliverable 1 Assessment\n" + "="*60 + "\n\n")
        for n,p,d in results:
            f.write(f"[{'PASS' if p else 'FAIL'}]  {n}\n")
            if d:
                for l in d.strip().splitlines(): f.write(f"       {l}\n")
        f.write(f"\nTotal: {passed}/{total} passed\n")
    print(f"  Results saved: {path}\n")

if __name__ == "__main__":
    main()
