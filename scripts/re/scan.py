"""Quick static queries on a memory dump of the LAS main module (DBG dumpmain). Addresses are module offsets.

Run with .venv-lasr (needs capstone):
  python scan.py str <regex>            strings matching regex (offset, text)
  python scan.py xref <off>             code that builds the address <off> (ADRP+ADD/LDR), with the guessed function start
  python scan.py xstr <regex>           xref for every matching string
  python scan.py callers <off>          BL/B call sites of a function
  python scan.py dis <off> [count]      disassembly
  python scan.py func <off>             guessed start of the function containing <off>
  python scan.py ptr <off>              8-byte words in rodata/data that hold base + <off> (vtables, tables)
Environment: LASAP_DUMP (default D:/DEV/LASAP-sandbox/re/main.bin), LASAP_BASE (runtime base, default 0x8504000).
"""
import os
import re
import struct
import sys
from functools import lru_cache

DUMP = os.environ.get("LASAP_DUMP", "D:/DEV/LASAP-sandbox/re/main.bin")
BASE = int(os.environ.get("LASAP_BASE", "0x8504000"), 16)
TEXT_END = 0x1434000

data = open(DUMP, "rb").read()


@lru_cache(maxsize=1)
def words():
    return struct.unpack_from(f"<{TEXT_END // 4}I", data, 0)


def sx(v: int, bits: int) -> int:
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def func_start(off: int) -> int:
    """Walks back to the previous RET/B (end of the prior function) or a frame-setup STP; a heuristic."""
    w = words()
    i = off // 4
    while i > 0:
        i -= 1
        insn = w[i]
        if insn == 0xD65F03C0 or (insn & 0xFC000000) == 0x14000000:  # ret / b
            j = i + 1
            while j * 4 < off and w[j] == 0xD503201F:  # skip nop padding
                j += 1
            return j * 4
    return 0


def xrefs(target: int):
    w = words()
    out = []
    for i, insn in enumerate(w):
        if (insn & 0x9F000000) != 0x90000000:
            continue
        rd = insn & 0x1F
        imm = sx((((insn >> 5) & 0x7FFFF) << 2) | ((insn >> 29) & 3), 21) << 12
        page = ((i * 4) & ~0xFFF) + imm
        if page != target & ~0xFFF:
            continue
        for k in range(1, 6):
            if i + k >= len(w):
                break
            n = w[i + k]
            if (n & 0xFF800000) == 0x91000000 and ((n >> 5) & 0x1F) == rd:  # add imm
                if page + (((n >> 10) & 0xFFF) << (12 * ((n >> 22) & 1))) == target:
                    out.append((i + k) * 4)
                break
            if (n & 0xFFC00000) == 0xF9400000 and ((n >> 5) & 0x1F) == rd:  # ldr x, [x, #imm]
                if page + ((n >> 10) & 0xFFF) * 8 == target:
                    out.append((i + k) * 4)
                break
    return out


def callers(target: int):
    w = words()
    out = []
    for i, insn in enumerate(w):
        if (insn & 0x7C000000) == 0x14000000:  # b / bl
            if i * 4 + sx(insn & 0x3FFFFFF, 26) * 4 == target:
                out.append((i * 4, "bl" if insn & 0x80000000 else "b"))
    return out


def dis(off: int, count: int) -> None:
    from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    for ins in md.disasm(data[off:off + count * 4], off):
        note = ""
        m = re.match(r"#0x([0-9a-f]+)$", ins.op_str.split(", ")[-1])
        if ins.mnemonic in ("bl", "b") and m:
            note = ""
        print(f"{ins.address:8x}: {ins.mnemonic:8} {ins.op_str}{note}")


def strings(rx: str):
    for m in re.finditer(rb"[\x20-\x7e]{4,}", data[TEXT_END:]):
        s = m.group().decode()
        if re.search(rx, s):
            yield TEXT_END + m.start(), s


def main() -> None:
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "str":
        for off, s in strings(args[0]):
            print(f"{off:8x} {s}")
    elif cmd == "xref":
        t = int(args[0], 16)
        for x in xrefs(t):
            print(f"{x:8x} in func {func_start(x):8x}")
    elif cmd == "xstr":
        for off, s in strings(args[0]):
            refs = xrefs(off)
            print(f"{off:8x} {s!r}: " + ", ".join(f"{x:x}@{func_start(x):x}" for x in refs))
    elif cmd == "callers":
        for x, kind in callers(int(args[0], 16)):
            print(f"{x:8x} {kind} in func {func_start(x):8x}")
    elif cmd == "dis":
        dis(int(args[0], 16), int(args[1]) if len(args) > 1 else 40)
    elif cmd == "func":
        print(f"{func_start(int(args[0], 16)):x}")
    elif cmd == "ptr":
        v = BASE + int(args[0], 16)
        for o in range(TEXT_END, len(data) - 7, 8):
            if struct.unpack_from("<Q", data, o)[0] == v:
                print(f"{o:8x}")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
