"""The sound's samples for the GBA's mixer (targets/gba/services/mixer.c),
converted from the cartridge's BRR to 8-bit PCM when the cartridge is packed
(pack.py), so that the GBA plays them from the cartridge and never decodes
BRR as it plays (docs/PLATFORM.md, "The GBA build, as built", "The sound").

The sound driver sends a song's sample block into sound memory whole, and
writes the boot's samples a byte at a time, so the mixer knows a sample by
where it comes from or by what it writes: each block the driver can write (the
songs' sample blocks 1-62, and the boot's block of its own samples) is listed
with its length, a hash of its bytes and the bus address the SNES cartridge
holds them at, and the samples it holds, each from where it starts in the
block. A sample is decoded as the S-DSP plays it: from
its start to its last block, which a sample that ends without looping
doesn't play, then the loop's blocks again from where it loops, decoded
again with the samples before them as they are when the sample comes round
(the loop as it sounds every time but the first). Each sample is then
resampled to the GBA's mix rate as it sounds at its own pitch (`resample`),
low-passed first so that what the mix rate can't hold is left out rather
than folded back, the way the S-DSP's own interpolation smooths it: at a
note's own pitch the mixer then reads one sample an output sample, and the
cartridge holds a third as much. A loop is resampled to a whole number of
the mix's samples, the rate it was made at kept beside it so the mixer plays
it at its pitch. Each sample is scaled to fill 8 bits, its peak kept for
the mixer to scale it back, and stored offset by 128 (0-255, its silence
128), which the mixer's loop reads with the processor's unsigned byte load.

The table (mixer.h's MixerSamples): a count of blocks, a count of
samples, where the noise is, the GBA's samples a frame the PCM was made for,
then each block, shortest first (its length, its hash, where the
cartridge holds it, its first sample and how many), then each sample (where it starts in its block, its rate, where it loops in
its PCM or none, its PCM's place in the table and its length, its peak), then
the PCM, and last the noise: the S-DSP's noise generator's whole sequence,
which a voice played as noise plays as a sample. A sample that loops, and the
noise, is followed by LOOP_SPARE more samples of its loop, so that the mixer
reads on through a chunk without stopping at the loop's end to go back.
"""
import math
import struct

FNV_OFFSET, FNV_PRIME = 0x811C9DC5, 0x01000193
BLOCK_BYTES = 9
NO_LOOP = 0xFFFFFFFF
SAMPLE_BLOCKS = 62
HEAD = struct.Struct("<HHIHH")
BLOCK = struct.Struct("<IIIHH")
SAMPLE = struct.Struct("<HHIIIHH")

# The mix's rate (targets/gba/services/direct_sound.h): MIX_A_FRAME samples a
# GBA frame, which plays a game frame's DSP_A_FRAME of the S-DSP's frames
# (mixer.c's OUT_PER and FRAMES_PER, twice these).
MIX_A_FRAME = 176
DSP_A_FRAME = 532.5
RATE_ONE = 4096              # a rate of 1.0: one PCM sample an output sample at the pitch $1000
SILENCE = 128                # the PCM's zero, offset
LOOP_SPARE = 256             # samples of a loop's PCM repeated past its end (mixer.h's MIXER_LOOP_SPARE)

# The low-pass: a windowed sinc ZEROS zero crossings each side, its window a
# Blackman's, tabulated KERNEL_STEPS to a crossing; the cut at CUT of the
# mix's half rate.
ZEROS, KERNEL_STEPS, CUT = 16, 256, 0.9


def fnv1a(data):
    """The mixer's hash of a block's bytes (mixer.c's)."""
    h = FNV_OFFSET
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & 0xFFFFFFFF
    return h


def clamp16(v):
    return max(-32768, min(32767, v))


def wrap16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def brr_sample(nibble, header, last, before_last):
    """One sample of BRR, as runtime/platform/native/mixer.c's brr_sample."""
    shift = header >> 4
    s = (nibble * (1 << shift)) >> 1 if shift <= 12 else (-2048 if nibble < 0 else 0)
    f = (header >> 2) & 3
    if f == 1:
        s += last + ((-last) >> 4)
    elif f == 2:
        s += 2 * last + ((-3 * last) >> 5) - before_last + (before_last >> 4)
    elif f == 3:
        s += 2 * last + ((-13 * last) >> 6) - before_last + ((3 * before_last) >> 4)
    return wrap16(clamp16(s) * 2) >> 1


def decode_block(data, at, history):
    """A block's sixteen samples, with `history` the two before them."""
    header = data[at]
    out = []
    last, before_last = history
    for i in range(16):
        byte = data[at + 1 + i // 2]
        nibble = ((byte >> 4) if i % 2 == 0 else byte) & 0x0F
        nibble = (nibble ^ 8) - 8
        s = brr_sample(nibble, header, last, before_last)
        out.append(s)
        before_last, last = last, s
    return out, (last, before_last)


def decode(data, start, loop):
    """The sample that starts at byte `start` of `data`, as it plays: its
    samples (15 bits each) and where it loops among them (None if it ends).
    `loop` is the byte its loop starts at."""
    out, history, at = [], (0, 0), start
    while at + BLOCK_BYTES <= len(data):
        header = data[at]
        if header & 3 == 1:          # the end, without a loop: not played
            return out, None
        samples, history = decode_block(data, at, history)
        out += samples
        if header & 1:               # the last block, which loops
            break
        at += BLOCK_BYTES
    else:
        return out, None
    if loop is None or loop < start or (loop - start) % BLOCK_BYTES or loop >= at + BLOCK_BYTES:
        return out, None
    first = (loop - start) // BLOCK_BYTES * 16
    again = []
    for block in range(loop, at + BLOCK_BYTES, BLOCK_BYTES):
        samples, history = decode_block(data, block, history)
        again += samples
    return out[:first] + again, first


def to_pcm8(samples):
    """8-bit PCM scaled to its peak, offset by 128, and the peak."""
    peak = max((abs(s) for s in samples), default=0) or 1
    return bytes(SILENCE + max(-127, min(127, (s * 127 + (peak // 2 if s >= 0 else -(peak // 2))) // peak))
                 for s in samples), peak


def _kernel():
    """The low-pass's shape, sinc(v) under a Blackman window, at v = i / KERNEL_STEPS for i
    from 0 to ZEROS * KERNEL_STEPS (it is even), and a zero past the end."""
    out = []
    for i in range(ZEROS * KERNEL_STEPS + 2):
        v = min(i / KERNEL_STEPS, ZEROS)
        sinc = 1.0 if v == 0 else math.sin(math.pi * v) / (math.pi * v)
        w = 0.42 + 0.5 * math.cos(math.pi * v / ZEROS) + 0.08 * math.cos(2 * math.pi * v / ZEROS)
        out.append(sinc * w)
    return out


KERNEL = _kernel()


def mix_ratio():
    """The mix's samples to one of the S-DSP's frames: a sample's at the pitch $1000."""
    return MIX_A_FRAME / DSP_A_FRAME


def resample(samples, loop):
    """A decoded sample at the mix's rate (the pitch $1000's), low-passed:
    its samples, where it loops among them (None if it ends), and its rate,
    in RATE_ONE's, the mix's steps through it an output sample at the pitch
    $1000. A loop of L samples becomes a whole number of the mix's, so its
    rate is that number over L's at the mix's ratio; the loop is resampled
    as it sounds coming round, the loop's end before its start and its start
    after its end."""
    ratio = mix_ratio()
    n = len(samples)
    if not n:
        return [], None, RATE_ONE
    if loop is None:
        r = ratio
        out_length = math.ceil(n * r)
        out_loop = None
    else:
        span = n - loop
        r = max(1, round(span * ratio)) / span
        out_loop = math.ceil(loop * r)
        out_length = out_loop + round(span * r)

    def at(k, round_again):
        if loop is not None and (k >= n or (round_again and k < loop)):
            return samples[loop + (k - loop) % (n - loop)]
        return samples[k] if 0 <= k < n else 0

    fc = min(0.5, 0.5 * r * CUT)             # the cut, in cycles a sample at the S-DSP's rate
    scale = 2 * fc * KERNEL_STEPS            # a sample apart, in the kernel's steps
    width = int(ZEROS / (2 * fc)) + 1
    out = []
    for j in range(out_length):
        t = j / r
        round_again = out_loop is not None and j >= out_loop   # the loop as it comes round: the loop before it too
        base = int(math.floor(t))
        total = weight = 0.0
        for k in range(base - width + 1, base + width + 1):
            p = abs(t - k) * scale
            i = int(p)
            if i >= ZEROS * KERNEL_STEPS:
                continue
            h = KERNEL[i] + (KERNEL[i + 1] - KERNEL[i]) * (p - i)
            total += at(k, round_again) * h
            weight += h
        out.append(int(round(total / weight)) if weight else 0)
    return out, out_loop, round(RATE_ONE * r / ratio)


def noise():
    """The noise generator's sequence from its start (mixer.c's), as 8 bits offset by 128."""
    n, out = 0x4000, bytearray()
    for _ in range(0x7FFF):
        out.append(((wrap16(n * 2) >> 8) + SILENCE) & 0xFF)
        n = (n >> 1) | (((n ^ (n >> 1)) & 1) << 14)
    return bytes(out)


def word(data, at):
    return data[at] | data[at + 1] << 8


def blocks(cartridge, rows, bus_to_offset):
    """Each block the driver writes into sound memory: its bytes, each
    sample in it as (where it starts, the byte its loop starts at), and the
    bus address the SNES cartridge holds its bytes at."""
    by_name = {r.name: r for r in rows}

    def row(name):
        r = by_name[name]
        at = bus_to_offset(r.at)
        return cartridge[at:at + r.end - r.at + 1]

    out = []
    pointers, loops = row("sound_block_ptrs"), row("sound_block_loop")
    for k in range(SAMPLE_BLOCKS):
        bus = pointers[3 * k] | pointers[3 * k + 1] << 8 | pointers[3 * k + 2] << 16
        at = bus_to_offset(bus)
        length = word(cartridge, at)
        out.append((cartridge[at + 2:at + 2 + length], [(0, word(loops, 2 * k))], bus + 2))
    boot, directory, program = row("sound_boot_samples"), row("sound_boot_directory"), row("sound_program_byte")
    samples_at = word(program, 0x15 + 2 * 2)       # where the boot puts its samples (sound_boot_block_to)
    boot_bytes = boot[2:2 + word(boot, 0)]
    entries = [(word(directory, 2 + 4 * i), word(directory, 4 + 4 * i)) for i in range(word(directory, 0) // 4)]
    out.append((boot_bytes, sorted({(s - samples_at, l - s) for s, l in entries
                                    if 0 <= s - samples_at < len(boot_bytes)}), by_name["sound_boot_samples"].at + 2))
    return out


def table(cartridge, rows, bus_to_offset):
    """The mixer's table of the samples, as bytes."""
    found = sorted(blocks(cartridge, rows, bus_to_offset), key=lambda b: len(b[0]))   # shortest first
    block_rows, sample_rows, pcm = [], [], bytearray()
    sample_count = sum(len(s) for _, s, _ in found)
    pcm_base = HEAD.size + BLOCK.size * len(found) + SAMPLE.size * sample_count
    for data, samples, source in found:
        block_rows.append(BLOCK.pack(len(data), fnv1a(data), source, len(sample_rows), len(samples)))
        for start, loop in samples:
            decoded, loop_at = decode(data, start, start + loop if loop is not None else None)
            mixed, loop_at, rate = resample(decoded, loop_at)
            bytes8, peak = to_pcm8(mixed)
            pcm += bytes(-len(pcm) % 4)
            sample_rows.append(SAMPLE.pack(start, rate, NO_LOOP if loop_at is None else loop_at,
                                           pcm_base + len(pcm), len(bytes8), min(peak, 0xFFFF), 0))
            pcm += bytes8
            if loop_at is not None:      # the loop again past the end, for a mix that reads on through it
                span = bytes8[loop_at:]
                pcm += (span * (LOOP_SPARE // len(span) + 1))[:LOOP_SPARE]
    pcm += bytes(-len(pcm) % 4)
    noise_at = pcm_base + len(pcm)
    sequence = noise()
    pcm += sequence + sequence[:LOOP_SPARE]
    head = HEAD.pack(len(block_rows), len(sample_rows), noise_at, MIX_A_FRAME, 0)
    return head + b"".join(block_rows) + b"".join(sample_rows) + bytes(pcm)
