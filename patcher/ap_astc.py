"""ASTC texture decoding (DESIGN.md, Universal Tracker map): LDR 2D blocks, as the game's UI textures use, to RGBA.
Standard library only, so the client can turn the player's RomFS into the tracker's images.

decode() takes a texture's blocks in row order (ap_bntx.Texture.level0) and returns the RGBA bytes of the image scaled
down by `step`: only the texels at multiples of `step` in each block are decoded, so a half-size image costs about a
quarter of a full one. Identical blocks are decoded once. Everything a block's mode and partition fields fix (the
weight grid, the value ranges, the infill taps) is worked out once per kind of block (layout()).

The format follows the Khronos Data Format Specification's ASTC chapter: integer sequence encoding (ise_read), colour
endpoint modes (endpoints), weight infill (infill), the partition hash (select_partition). The HDR endpoint modes do
not occur in the game's textures and decode as magenta, as do blocks the specification calls errors. A last group of
packed values whose bits the block leaves out reads them as zeros.
"""
from functools import lru_cache
from typing import Dict, List, Optional, Sequence, Tuple

# Weight ranges by the block mode's precision bit and range bits; colour ranges from the largest
WEIGHT_RANGES = ((0, 0, 2, 3, 4, 5, 6, 8), (0, 0, 10, 12, 16, 20, 24, 32))
COLOR_RANGES = (256, 192, 160, 128, 96, 80, 64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2)
MAGENTA = bytes((255, 0, 255, 255))
REVERSE = bytes(int(f"{i:08b}"[::-1], 2) for i in range(256))  # bit-reversed bytes, for the weights


@lru_cache(maxsize=None)
def range_parts(levels: int) -> Tuple[int, int]:
    """(low bits, digit kind: 0 none, 1 trit, 2 quint) of an integer sequence range with this many levels."""
    for kind, base in ((1, 3), (2, 5)):
        rest = levels // base
        if levels % base == 0 and rest & (rest - 1) == 0:
            return rest.bit_length() - 1, kind
    return levels.bit_length() - 1, 0


@lru_cache(maxsize=None)
def ise_bits(levels: int, count: int) -> int:
    n, kind = range_parts(levels)
    return n * count + ((8 * count + 4) // 5 if kind == 1 else (7 * count + 2) // 3 if kind == 2 else 0)


def _trits(t: int) -> Tuple[int, ...]:
    if (t >> 2) & 7 == 7:
        c = ((t >> 5) & 7) << 2 | (t & 3)
        t4 = t3 = 2
    else:
        c = t & 0x1f
        t4, t3 = (2, (t >> 7) & 1) if (t >> 5) & 3 == 3 else ((t >> 7) & 1, (t >> 5) & 3)
    if c & 3 == 3:
        t2, t1, t0 = 2, (c >> 4) & 1, ((c >> 3) & 1) << 1 | ((c >> 2) & 1 & ~(c >> 3) & 1)
    elif (c >> 2) & 3 == 3:
        t2, t1, t0 = 2, 2, c & 3
    else:
        t2, t1, t0 = (c >> 4) & 1, (c >> 2) & 3, ((c >> 1) & 1) << 1 | (c & 1 & ~(c >> 1) & 1)
    return t0, t1, t2, t3, t4


def _quints(q: int) -> Tuple[int, ...]:
    if (q >> 1) & 3 == 3 and (q >> 5) & 3 == 0:
        b0 = q & 1
        return 4, 4, b0 << 2 | ((q >> 4) & 1 & ~b0 & 1) << 1 | ((q >> 3) & 1 & ~b0 & 1)
    if (q >> 1) & 3 == 3:
        q2, c = 4, ((q >> 3) & 3) << 3 | (~(q >> 5) & 3) << 1 | (q & 1)
    else:
        q2, c = (q >> 5) & 3, q & 0x1f
    q1, q0 = (4, (c >> 3) & 3) if c & 7 == 5 else ((c >> 3) & 3, c & 7)
    return q0, q1, q2


TRITS = [_trits(t) for t in range(256)]
QUINTS = [_quints(q) for q in range(128)]


@lru_cache(maxsize=None)
def ise_plan(levels: int, count: int):
    """How `count` values of this range lie in their bits: (low bits, kind, per group of 5 trits or 3 quints: the
    positions of the values' low bits and the (position, mask, shift) of its packed digit bits, total bits)."""
    n, kind = range_parts(levels)
    groups, pos = [], 0
    if kind:
        size, widths, shifts = ((5, (2, 2, 1, 2, 1), (0, 2, 4, 5, 7)) if kind == 1 else (3, (3, 2, 2), (0, 3, 5)))
        for _ in range(0, count, size):
            lows, digit = [], []
            for i in range(size):
                lows.append(pos)
                pos += n
                digit.append((pos, (1 << widths[i]) - 1, shifts[i]))
                pos += widths[i]
            groups.append((lows, digit))
    return n, kind, groups, ise_bits(levels, count)


def ise_read(value: int, start: int, levels: int, count: int) -> List[int]:
    """The `count` values (digit << low bits | low bits) of this range from bit `start` of `value`."""
    n, kind, groups, nbits = ise_plan(levels, count)
    value = (value >> start) & ((1 << nbits) - 1)  # bits past the sequence are zeros
    mask = (1 << n) - 1
    if not kind:
        return [(value >> (i * n)) & mask for i in range(count)]
    table = TRITS if kind == 1 else QUINTS
    out: List[int] = []
    for lows, digit in groups:
        d = 0
        for pos, dmask, shift in digit:
            d |= ((value >> pos) & dmask) << shift
        out.extend((t << n) | ((value >> low) & mask) for t, low in zip(table[d], lows))
    return out[:count]


def _replicate(value: int, bits: int, to: int) -> int:
    if not bits:
        return 0
    out, have = 0, 0
    while have < to:
        out, have = out << bits | value, have + bits
    return out >> (have - to)


def _bit(m: int, i: int) -> int:
    return (m >> i) & 1


def _color_bc(m: int, n: int, kind: int) -> Tuple[int, int]:
    """The specification's B and C of colour unquantization for a trit or quint value with n low bits m."""
    b, c, d, e, f = (_bit(m, i) for i in range(1, 6))
    if kind == 1:
        return ((0, 204), ((b << 8) | (b << 4) | (b << 2) | (b << 1), 93),
                ((c << 8) | (b << 7) | (c << 3) | (b << 2) | (c << 1) | b, 44),
                ((d << 8) | (c << 7) | (b << 6) | (d << 2) | (c << 1) | b, 22),
                ((e << 8) | (d << 7) | (c << 6) | (b << 5) | (e << 1) | d, 11),
                ((f << 8) | (e << 7) | (d << 6) | (c << 5) | (b << 4) | f, 5))[n - 1]
    return ((0, 113), ((b << 8) | (b << 3) | (b << 2), 54),
            ((c << 8) | (b << 7) | (c << 2) | (b << 1) | c, 26),
            ((d << 8) | (c << 7) | (b << 6) | (d << 1) | c, 13),
            ((e << 8) | (d << 7) | (c << 6) | (b << 5) | e, 6))[n - 1]


@lru_cache(maxsize=None)
def color_values(levels: int) -> List[int]:
    """Colour value (digit << n | low bits) -> 0..255."""
    n, kind = range_parts(levels)
    table = [0] * ((3 if kind == 1 else 5 if kind == 2 else 1) << n)
    for digit in range(len(table) >> n):
        for m in range(1 << n):
            if not kind:
                v = _replicate(m, n, 8)
            else:
                a = 0x1ff if m & 1 else 0
                b, c = _color_bc(m, n, kind)
                t = (digit * c + b) ^ a
                v = (a & 0x80) | (t >> 2)
            table[digit << n | m] = v
    return table


@lru_cache(maxsize=None)
def weight_values(levels: int) -> List[int]:
    """Weight value (digit << n | low bits) -> 0..64."""
    n, kind = range_parts(levels)
    table = [0] * ((3 if kind == 1 else 5 if kind == 2 else 1) << n)
    for digit in range(len(table) >> n):
        for m in range(1 << n):
            if not kind:
                v = _replicate(m, n, 6)
            elif n == 0:
                v = (0, 32, 63)[digit] if kind == 1 else (0, 16, 32, 47, 63)[digit]
            else:
                a = 0x7f if m & 1 else 0
                b, c = _bit(m, 1), _bit(m, 2)
                if kind == 1:
                    bb, cc = ((0, 50), ((b << 6) | (b << 2) | b, 23), ((c << 6) | (b << 5) | (c << 1) | b, 11))[n - 1]
                else:
                    bb, cc = ((0, 28), ((b << 6) | (b << 1), 13))[n - 1]
                t = (digit * cc + bb) ^ a
                v = (a & 0x20) | (t >> 2)
            table[digit << n | m] = v + 1 if v > 32 else v
    return table


@lru_cache(maxsize=None)
def block_mode(mode: int) -> Optional[Tuple[int, int, int, int]]:
    """(weight grid width, height, dual plane, weight levels) of an 11-bit block mode; None for an invalid one."""
    if mode & 3:
        r = ((mode >> 4) & 1) | ((mode & 3) << 1)
        h, d = (mode >> 9) & 1, (mode >> 10) & 1
        a, b = (mode >> 5) & 3, (mode >> 7) & 3
        sel = (mode >> 2) & 3
        if sel == 0:
            w, ht = b + 4, a + 2
        elif sel == 1:
            w, ht = b + 8, a + 2
        elif sel == 2:
            w, ht = a + 2, b + 8
        elif (mode >> 8) & 1:
            w, ht = (b & 1) + 2, a + 2
        else:
            w, ht = a + 2, (b & 1) + 6
    else:
        r = ((mode >> 4) & 1) | (((mode >> 2) & 3) << 1)
        if r == 0:
            return None
        h, d = (mode >> 9) & 1, (mode >> 10) & 1
        a = (mode >> 5) & 3
        sel = (mode >> 7) & 3
        if sel == 0:
            w, ht = 12, a + 2
        elif sel == 1:
            w, ht = a + 2, 12
        elif sel == 2:
            w, ht, h, d = a + 6, ((mode >> 9) & 3) + 6, 0, 0
        elif a < 2:
            w, ht = (6, 10) if a == 0 else (10, 6)
        else:
            return None
    levels = WEIGHT_RANGES[h][r]
    count = w * ht * (d + 1)
    if count > 64 or not 24 <= ise_bits(levels, count) <= 96:
        return None
    return w, ht, d, levels


@lru_cache(maxsize=None)
def infill(bw: int, bh: int, wx: int, wy: int, step: int) -> List[Tuple[int, ...]]:
    """Per decoded texel (row order): its four weight grid indices and their factors (of 16)."""
    ds, dt = (1024 + bw // 2) // (bw - 1), (1024 + bh // 2) // (bh - 1)
    taps = []
    for t in range(0, bh, step):
        gt = (dt * t * (wy - 1) + 32) >> 6
        jt, ft = gt >> 4, gt & 0xf
        for s in range(0, bw, step):
            gs = (ds * s * (wx - 1) + 32) >> 6
            js, fs = gs >> 4, gs & 0xf
            v0 = js + jt * wx
            w11 = (fs * ft + 8) >> 4
            taps.append((v0, v0 + 1, v0 + wx, v0 + wx + 1, 16 - fs - ft + w11, fs - w11, ft - w11, w11))
    return taps


def _hash52(p: int) -> int:
    p &= 0xffffffff
    p ^= p >> 15
    p = (p - (p << 17)) & 0xffffffff
    p = (p + (p << 7)) & 0xffffffff
    p = (p + (p << 4)) & 0xffffffff
    p ^= p >> 5
    p = (p + (p << 16)) & 0xffffffff
    p ^= p >> 7
    p ^= p >> 3
    p = (p ^ (p << 6)) & 0xffffffff
    return p ^ (p >> 17)


def select_partition(seed: int, x: int, y: int, count: int, small: bool) -> int:
    if small:
        x, y = x << 1, y << 1
    seed += (count - 1) * 1024
    rnum = _hash52(seed)
    s = [(rnum >> (4 * i)) & 0xf for i in range(8)]
    s += [(rnum >> 18) & 0xf, (rnum >> 22) & 0xf, (rnum >> 26) & 0xf, ((rnum >> 30) | (rnum << 2)) & 0xf]
    s = [v * v for v in s]
    if seed & 1:
        sh1, sh2 = (4 if seed & 2 else 5), (6 if count == 3 else 5)
    else:
        sh1, sh2 = (6 if count == 3 else 5), (4 if seed & 2 else 5)
    sh3 = sh1 if seed & 0x10 else sh2
    s = [v >> sh for v, sh in zip(s, (sh1, sh2, sh1, sh2, sh1, sh2, sh1, sh2, sh3, sh3, sh3, sh3))]
    a = (s[0] * x + s[1] * y + (rnum >> 14)) & 0x3f
    b = (s[2] * x + s[3] * y + (rnum >> 10)) & 0x3f
    c = (s[4] * x + s[5] * y + (rnum >> 6)) & 0x3f if count >= 3 else 0
    d = (s[6] * x + s[7] * y + (rnum >> 2)) & 0x3f if count >= 4 else 0
    if a >= b and a >= c and a >= d:
        return 0
    if b >= c and b >= d:
        return 1
    return 2 if c >= d else 3


@lru_cache(maxsize=None)
def partition_map(seed: int, count: int, bw: int, bh: int, step: int) -> List[int]:
    small = bw * bh < 31
    return [select_partition(seed, s, t, count, small) for t in range(0, bh, step) for s in range(0, bw, step)]


def _clamp(v: int) -> int:
    return 0 if v < 0 else 255 if v > 255 else v


def _bit_transfer(a: int, b: int) -> Tuple[int, int]:
    b = (b >> 1) | (a & 0x80)
    a = (a >> 1) & 0x3f
    return (a - 0x40 if a & 0x20 else a), b


def _blue_contract(r: int, g: int, b: int, a: int) -> Tuple[int, int, int, int]:
    return (r + b) >> 1, (g + b) >> 1, b, a


def endpoints(cem: int, v: Sequence[int]) -> Tuple[Tuple[int, ...], Tuple[int, ...]]:
    """The two RGBA endpoints of a colour endpoint mode from its unquantized values."""
    if cem == 0:
        return (v[0], v[0], v[0], 255), (v[1], v[1], v[1], 255)
    if cem == 1:
        l0 = (v[0] >> 2) | (v[1] & 0xc0)
        l1 = min(255, l0 + (v[1] & 0x3f))
        return (l0, l0, l0, 255), (l1, l1, l1, 255)
    if cem == 4:
        return (v[0], v[0], v[0], v[2]), (v[1], v[1], v[1], v[3])
    if cem == 5:
        v1, v0 = _bit_transfer(v[1], v[0])
        v3, v2 = _bit_transfer(v[3], v[2])
        lum = _clamp(v0 + v1)
        return (v0, v0, v0, v2), (lum, lum, lum, _clamp(v2 + v3))
    if cem in (6, 10):
        alpha = (v[4], v[5]) if cem == 10 else (255, 255)
        return ((v[0] * v[3]) >> 8, (v[1] * v[3]) >> 8, (v[2] * v[3]) >> 8, alpha[0]), (v[0], v[1], v[2], alpha[1])
    if cem in (8, 12):
        a0, a1 = (v[6], v[7]) if cem == 12 else (255, 255)
        if v[1] + v[3] + v[5] >= v[0] + v[2] + v[4]:
            return (v[0], v[2], v[4], a0), (v[1], v[3], v[5], a1)
        return _blue_contract(v[1], v[3], v[5], a1), _blue_contract(v[0], v[2], v[4], a0)
    if cem in (9, 13):
        v1, v0 = _bit_transfer(v[1], v[0])
        v3, v2 = _bit_transfer(v[3], v[2])
        v5, v4 = _bit_transfer(v[5], v[4])
        v7, v6 = _bit_transfer(v[7], v[6]) if cem == 13 else (0, 255)
        moved = (_clamp(v0 + v1), _clamp(v2 + v3), _clamp(v4 + v5), _clamp(v6 + v7))
        if v1 + v3 + v5 >= 0:
            return (v0, v2, v4, v6), moved
        return _blue_contract(*moved), _blue_contract(v0, v2, v4, v6)
    return (255, 0, 255, 255), (255, 0, 255, 255)  # HDR modes


@lru_cache(maxsize=None)
def _extra_bits(mode11: int, parts: int) -> Optional[Tuple[int, int]]:
    """Where the extra colour endpoint mode bits of a multi-partition block lie, and how many."""
    mode = block_mode(mode11)
    if mode is None:
        return None
    wx, wy, dual, levels = mode
    count = 3 * parts - 4
    return 128 - ise_bits(levels, wx * wy * (dual + 1)) - count, count


@lru_cache(maxsize=None)
def layout(mode11: int, parts: int, field: int, extra: int, bw: int, bh: int, step: int):
    """What a block's mode and partition fields fix: (endpoint modes, colour data start, colour levels, colour
    value count, weight levels, weight count, dual plane, colour component selector position, colour table, weight
    table, infill taps, weight grid width); None for an error block."""
    mode = block_mode(mode11)
    if mode is None:
        return None
    wx, wy, dual, wlevels = mode
    nweights = wx * wy * (dual + 1)
    below = 128 - ise_bits(wlevels, nweights)
    if parts == 1:
        cems, color_start = [field], 17
    else:
        color_start = 29
        if field & 3 == 0:
            cems = [field >> 2] * parts
        else:
            below -= 3 * parts - 4
            combined = field | (extra << 6)
            base = (combined & 3) - 1
            cems = [((base + ((combined >> (2 + i)) & 1)) << 2) | ((combined >> (2 + parts + 2 * i)) & 3)
                    for i in range(parts)]
    ccs_at = None
    if dual:
        below -= 2
        ccs_at = below
    nvalues = sum(((c >> 2) + 1) * 2 for c in cems)
    if nvalues > 18:
        return None
    clevels = next((lv for lv in COLOR_RANGES if ise_bits(lv, nvalues) <= below - color_start), None)
    if clevels is None:
        return None
    return (cems, color_start, clevels, nvalues, wlevels, nweights, dual, ccs_at, color_values(clevels),
            weight_values(wlevels), infill(bw, bh, wx, wy, step), wx)


def decode_block(block: bytes, bw: int, bh: int, step: int = 1) -> bytes:
    """RGBA bytes of a block's texels at multiples of `step`, row order."""
    bits = int.from_bytes(block, "little")
    texels = ((bw + step - 1) // step) * ((bh + step - 1) // step)
    if bits & 0x1ff == 0x1fc:  # void extent: one colour, 16 bits per channel
        return bytes(((bits >> (64 + 16 * i)) & 0xffff) >> 8 for i in range(4)) * texels
    mode11 = bits & 0x7ff
    parts = ((bits >> 11) & 3) + 1
    field = (bits >> 13) & 0xf if parts == 1 else (bits >> 23) & 0x3f
    extra = 0
    if parts > 1 and field & 3:
        where = _extra_bits(mode11, parts)
        if where is None:
            return MAGENTA * texels
        extra = (bits >> where[0]) & ((1 << where[1]) - 1)
    lay = layout(mode11, parts, field, extra, bw, bh, step)
    if lay is None:
        return MAGENTA * texels
    cems, color_start, clevels, nvalues, wlevels, nweights, dual, ccs_at, ctab, wtab, taps, wx = lay
    values = [ctab[v] for v in ise_read(bits, color_start, clevels, nvalues)]
    eps, i = [], 0
    for cem in cems:
        k = ((cem >> 2) + 1) * 2
        e0, e1 = endpoints(cem, values[i:i + k])
        # each channel as 64 * 257 * e0 + 32 and 257 * (e1 - e0): (base + delta * weight) >> 14 is the interpolation
        eps.append(tuple(c * 257 * 64 + 32 for c in e0) + tuple((c1 - c0) * 257 for c0, c1 in zip(e0, e1)))
        i += k
    reversed_bits = int.from_bytes(block[::-1].translate(REVERSE), "little")  # the weights run down from the top
    weights = [wtab[v] for v in ise_read(reversed_bits, 0, wlevels, nweights)]
    pad = [0] * (wx + 2)
    planes = (weights[0::2] + pad, weights[1::2] + pad) if dual else (weights + pad,)
    plane_weights = [[(p[a] * fa + p[b] * fb + p[c] * fc + p[d] * fd + 8) >> 4 for a, b, c, d, fa, fb, fc, fd in taps]
                     for p in planes]
    w0 = plane_weights[0]
    if parts == 1 and not dual:
        r0, g0, b0, a0, dr, dg, db, da = eps[0]
        return bytes([v for w in w0 for v in ((r0 + dr * w) >> 14, (g0 + dg * w) >> 14, (b0 + db * w) >> 14,
                                              (a0 + da * w) >> 14)])
    pmap = partition_map((bits >> 13) & 0x3ff, parts, bw, bh, step) if parts > 1 else [0] * texels
    ccs = (bits >> ccs_at) & 3 if dual else -1
    w1 = plane_weights[1] if dual else w0
    out = []
    for t in range(texels):
        e = eps[pmap[t]]
        for ch in range(4):
            out.append((e[ch] + e[ch + 4] * (w1[t] if ch == ccs else w0[t])) >> 14)
    return bytes(out)


def decode(blocks: bytes, width: int, height: int, bw: int, bh: int, step: int = 1) -> Tuple[bytes, int, int]:
    """RGBA bytes, width and height of a texture scaled down by `step` (which divides the block size), from its
    blocks in row order; the size is the texture's, rounded up to whole blocks, over `step`."""
    if bw % step or bh % step:
        raise ValueError(f"step {step} does not divide the {bw}x{bh} block")
    cols, rows = -(-width // bw), -(-height // bh)
    sw, sh = bw // step, bh // step
    ow = cols * sw
    out = bytearray(ow * rows * sh * 4)
    cache: Dict[bytes, bytes] = {}
    row_bytes = sw * 4
    for by in range(rows):
        for bx in range(cols):
            i = (by * cols + bx) * 16
            block = blocks[i:i + 16]
            px = cache.get(block)
            if px is None:
                px = cache[block] = decode_block(block, bw, bh, step)
            dst = (by * sh * ow + bx * sw) * 4
            for row in range(sh):
                out[dst:dst + row_bytes] = px[row * row_bytes:(row + 1) * row_bytes]
                dst += ow * 4
    return bytes(out), ow, rows * sh
