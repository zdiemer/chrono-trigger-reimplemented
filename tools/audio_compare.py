#!/usr/bin/env python3
"""Compare two recordings of the same movie's audio, such as ct_dev --wav and
ct_oracle --wav (docs/RUNTIME.md, S-DSP).

    python3 tools/audio_compare.py dev.wav oracle.wav [--segment 4] [--hop 4] [--max-lag 1.0]

Both files are treated as streams of S-DSP output samples, one per sample
index, whatever rate their headers declare: Snes9x labels its DSP samples
32040 Hz, and music the sound driver times itself lines up sample for sample.
What doesn't line up is when the main CPU starts things (a new song, a sound
effect), because the two systems run the SPC700 at slightly different speeds
relative to the CPU. So the comparison is per segment: each segment of the
first file is cross-correlated against the second around the previous
segment's lag, and scored at its best lag.

Per segment it reports:
  lag      offset into the second file (samples) at the best match
  corr     Pearson correlation at that lag (mono mix), 1.0 = same waveform
  snr      rms(a) / rms(a - b) in dB (after the lag, no gain adjustment)
  exact    fraction of sample values (both channels) that are identical
  spec     correlation of the two log-magnitude spectra (averaged 2048-sample frames)
  lsd      log-spectral distance in dB (rms over frequency of the dB difference)
Silent segments (both below --silence) are skipped.

Needs numpy.
"""

import argparse
import sys
import wave

try:
    import numpy as np
except ImportError:  # pragma: no cover
    sys.exit("audio_compare.py needs numpy (pip install numpy)")


def load(path):
    with wave.open(str(path), "rb") as w:
        if w.getsampwidth() != 2 or w.getnchannels() != 2:
            sys.exit(f"{path}: expected 16-bit stereo")
        rate = w.getframerate()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2)
    return rate, data.astype(np.float64)


def best_lag(x, y, lo, hi):
    """Lag in [lo, hi] maximizing the normalized correlation of x against y[lag:lag+len(x)]."""
    n = len(x)
    lo = max(lo, 0)
    hi = min(hi, len(y) - n)
    if hi < lo:
        return None, 0.0
    seg = y[lo : hi + n]
    size = 1 << int(np.ceil(np.log2(len(seg) + n)))
    xc = x - x.mean()
    spec = np.fft.rfft(seg, size) * np.conj(np.fft.rfft(xc, size))
    corr = np.fft.irfft(spec, size)[: hi - lo + 1]
    # Energy of each window of seg, mean removed.
    c1 = np.concatenate(([0.0], np.cumsum(seg)))
    c2 = np.concatenate(([0.0], np.cumsum(seg * seg)))
    s1 = c1[n : n + hi - lo + 1] - c1[: hi - lo + 1]
    s2 = c2[n : n + hi - lo + 1] - c2[: hi - lo + 1]
    energy = np.maximum(s2 - s1 * s1 / n, 1e-9)
    norm = corr / np.sqrt(energy * max(np.dot(xc, xc), 1e-9))
    i = int(np.argmax(norm))
    return lo + i, float(norm[i])


def log_spectrum(x, frame=2048):
    frames = len(x) // frame
    if frames == 0:
        return None
    win = np.hanning(frame)
    mags = np.abs(np.fft.rfft(x[: frames * frame].reshape(frames, frame) * win, axis=1)) ** 2
    return 10 * np.log10(mags.mean(axis=0) + 1e-3)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a", help="first recording (reference timeline, e.g. ct_dev --wav)")
    ap.add_argument("b", help="second recording (e.g. ct_oracle --wav)")
    ap.add_argument("--segment", type=float, default=4.0, help="segment length in seconds of 32000 Hz (default 4)")
    ap.add_argument("--hop", type=float, default=None, help="step between segments in seconds (default: --segment)")
    ap.add_argument("--start", type=float, default=0.0, help="skip this many seconds of the first file")
    ap.add_argument("--max-lag", type=float, default=1.0, help="search +-seconds around the previous lag (default 1)")
    ap.add_argument("--silence", type=float, default=30.0, help="rms below which a segment counts as silent")
    ap.add_argument("--min-corr", type=float, default=0.9, help="segments below this are listed as mismatches")
    args = ap.parse_args()

    rate_a, a = load(args.a)
    rate_b, b = load(args.b)
    sr = 32000
    seg = int(args.segment * sr)
    hop = int((args.hop or args.segment) * sr)
    max_lag = int(args.max_lag * sr)
    ma, mb = a.mean(axis=1), b.mean(axis=1)
    print(f"{args.a}: {len(a)} frames ({rate_a} Hz header)")
    print(f"{args.b}: {len(b)} frames ({rate_b} Hz header)")
    print(f"{'start s':>8} {'lag':>7} {'corr':>7} {'snr dB':>7} {'exact':>6} {'spec':>6} {'lsd dB':>6}  rms a/b")

    lag = None
    rows = []
    for start in range(int(args.start * sr), len(a) - seg + 1, hop):
        x, xs = ma[start : start + seg], a[start : start + seg]
        rms_a = float(np.sqrt(np.mean(xs * xs)))
        if lag is None:
            # First audible segment: search everything plausible.
            lo, hi = start - 8 * sr, start + 8 * sr
        else:
            lo, hi = start + lag - max_lag, start + lag + max_lag
        pos, corr = best_lag(x, mb, lo, hi)
        if pos is None:
            break
        ys = b[pos : pos + seg]
        rms_b = float(np.sqrt(np.mean(ys * ys)))
        if rms_a < args.silence and rms_b < args.silence:
            print(f"{start / sr:8.1f} {'':>7} {'silent':>7}")
            continue
        lag = pos - start
        diff = xs - ys
        snr = 10 * np.log10(np.mean(xs * xs) / max(np.mean(diff * diff), 1e-9))
        exact = float(np.mean(xs == ys))
        sa, sb = log_spectrum(x), log_spectrum(mb[pos : pos + seg])
        spec = float(np.corrcoef(sa, sb)[0, 1])
        lsd = float(np.sqrt(np.mean((sa - sb) ** 2)))
        rows.append((start / sr, lag, corr, snr, exact, spec, lsd))
        print(f"{start / sr:8.1f} {lag:7d} {corr:7.4f} {snr:7.1f} {exact:6.3f} {spec:6.3f} {lsd:6.2f}  {rms_a:.0f}/{rms_b:.0f}")

    if not rows:
        print("no audible segments")
        return 1
    r = np.array(rows)
    print()
    print(f"audible segments: {len(r)}")
    print(f"correlation: median {np.median(r[:, 2]):.4f}, min {r[:, 2].min():.4f}, "
          f">= {args.min_corr}: {int(np.sum(r[:, 2] >= args.min_corr))} of {len(r)}")
    print(f"snr dB: median {np.median(r[:, 3]):.1f}, min {r[:, 3].min():.1f}")
    print(f"exact samples: median {np.median(r[:, 4]):.3f}")
    print(f"spectrum correlation: median {np.median(r[:, 5]):.4f}, min {r[:, 5].min():.4f}; "
          f"log-spectral distance median {np.median(r[:, 6]):.2f} dB")
    bad = r[r[:, 2] < args.min_corr]
    if len(bad):
        print("below --min-corr at: " + ", ".join(f"{t:.0f}s ({c:.3f})" for t, c in zip(bad[:, 0], bad[:, 2])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
