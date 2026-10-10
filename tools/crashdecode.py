#!/usr/bin/env python3
"""Turn the crash lines of an amisynth.log into function names and source lines.

    python3 tools/crashdecode.py amisynth.log [out/amisynth.elf]

The elf has to come from the same build as the program that wrote the log:
compare the build id on the first log line with `git describe --always --dirty`.
"""

import glob
import os
import platform
import re
import shutil
import subprocess
import sys

ADDR2LINE = "m68k-amiga-elf-addr2line"
OBJDUMP = "m68k-amiga-elf-objdump"


def find_tool(name):
    found = shutil.which(name)
    if found:
        return found
    host = {"Darwin": "darwin", "Windows": "win32"}.get(platform.system(), "linux")
    suffix = ".exe" if host == "win32" else ""
    for root in ("~/.cursor/extensions", "~/.vscode/extensions"):
        pattern = os.path.join(os.path.expanduser(root), "bartmanabyss.amiga-debug-*", "bin", host, "opt", "bin", name + suffix)
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[-1]
    sys.exit(f"{name} not found: put the Amiga Debug toolchain on PATH")


def text_size(elf):
    out = subprocess.run([find_tool(OBJDUMP), "-h", elf], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        fields = line.split()
        if len(fields) > 2 and fields[1] == ".text":
            return int(fields[2], 16)
    sys.exit(f"no .text section in {elf}")


def describe(elf, offsets):
    if not offsets:
        return {}
    args = [find_tool(ADDR2LINE), "-f", "-i", "-p", "-e", elf] + [hex(o) for o in offsets]
    out = subprocess.run(args, capture_output=True, text=True, check=True).stdout.strip().splitlines()
    # -i adds "(inlined by)" lines, which belong to the address above them.
    result, index = {}, -1
    for line in out:
        if not line.startswith(" (inlined by)"):
            index += 1
            result[offsets[index]] = [line]
        else:
            result[offsets[index]].append(line.strip())
    return result


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    log = open(sys.argv[1], encoding="latin-1").read()
    elf = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", "out", "amisynth.elf")

    header = re.search(r"build (\S+), code at \$([0-9A-Fa-f]{8})", log)
    if not header:
        sys.exit("no 'code at' line in the log")
    build, base = header.group(1), int(header.group(2), 16)
    print(f"log build {build}, code at ${base:08X}")

    summary = re.search(r"amisynth: (CRASH|WATCHDOG).*", log)
    if not summary:
        print("the log has no crash or watchdog record")
        return
    print(summary.group(0))

    size = text_size(elf)
    pc = re.search(r"crash\.pc \$([0-9A-Fa-f]{8})", log)
    stack = [int(v, 16) for line in re.findall(r"crash\.stack\d+: (.*)", log) for v in line.split()]
    regs = [int(v, 16) for line in re.findall(r"crash\.[da]\d+: (.*)", log) for v in re.findall(r"\b[0-9A-Fa-f]{8}\b", line)]

    def in_code(value):
        return base <= value < base + size

    candidates = []
    if pc:
        candidates.append(("pc", int(pc.group(1), 16)))
    candidates += [(f"stack[{i}]", v) for i, v in enumerate(stack) if in_code(v)]
    candidates += [("register", v) for v in regs if in_code(v)]

    lines = describe(elf, sorted({v - base for _, v in candidates if in_code(v)}))
    for label, value in candidates:
        where = lines.get(value - base, ["outside the program's code"])
        print(f"{label:>10} ${value:08X} code+${value - base:X}: {where[0]}")
        for extra in where[1:]:
            print(f"{'':>10} {extra}")


if __name__ == "__main__":
    main()
