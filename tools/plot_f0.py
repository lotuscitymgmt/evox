#!/usr/bin/env python3
"""Plot evox_analyze CSV output (optionally against a ground-truth CSV).

    python3 tools/plot_f0.py analysis.csv [--truth file.truth.csv] [-o plot.png]

Requires matplotlib (pip install matplotlib). Only used offline, never shipped.
"""
import argparse
import csv
import math


def load(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def to_midi(hz):
    return 69 + 12 * math.log2(hz / 440.0) if hz > 0 else float("nan")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("analysis")
    ap.add_argument("--truth")
    ap.add_argument("-o", "--output", default=None)
    args = ap.parse_args()

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    rows = load(args.analysis)
    t = [float(r["time_s"]) for r in rows]
    midi = [to_midi(float(r["f0_hz"])) for r in rows]
    conf = [float(r["confidence"]) for r in rows]

    fig, (ax, axc) = plt.subplots(2, 1, figsize=(12, 6), sharex=True,
                                  gridspec_kw={"height_ratios": [3, 1]})
    fig.patch.set_facecolor("#0E0F12")
    for a in (ax, axc):
        a.set_facecolor("#17191E")
        a.tick_params(colors="#aaa")
        for s in a.spines.values():
            s.set_color("#333")

    if args.truth:
        tr = load(args.truth)
        ax.plot([float(r["time_s"]) for r in tr], [to_midi(float(r["f0_hz"])) for r in tr],
                color="#E5487F", lw=1, alpha=0.7, label="ground truth")

    ax.plot(t, midi, ".", ms=2.5, color="#3FC1C9", label="detected f0")
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    valid = [m for m in midi if not math.isnan(m)]
    if valid:
        lo, hi = int(min(valid)) - 2, int(max(valid)) + 3
        ax.set_ylim(lo, hi)
        step = 1 if hi - lo <= 24 else 2
        ticks = list(range(lo, hi, step))
        ax.set_yticks(ticks)
        ax.set_yticklabels([f"{names[m % 12]}{m // 12 - 1}" for m in ticks], fontsize=7)
    ax.grid(True, color="#2a2d33", lw=0.5)
    ax.legend(facecolor="#17191E", labelcolor="#ddd")
    ax.set_title(args.analysis, color="#ddd")

    axc.plot(t, conf, color="#E5487F", lw=0.8)
    axc.set_ylim(0, 1.05)
    axc.set_ylabel("confidence", color="#aaa")
    axc.set_xlabel("time (s)", color="#aaa")

    out = args.output or args.analysis.rsplit(".", 1)[0] + ".png"
    fig.tight_layout()
    fig.savefig(out, dpi=110, facecolor=fig.get_facecolor())
    print("wrote", out)


if __name__ == "__main__":
    main()
