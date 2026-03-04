#!/usr/bin/env python3
"""
benchmark_thesis_v5_pub.py

Thesis-ready plotting script (publication quality):
- Throughput: Signing + Verification in ONE figure (linear + log)
- Energy: Package and Cores separate, each includes Signing + Verification (linear + log)
- Value labels ABOVE bars (no clipping) using bbox_extra_artists
- Friendly number formatting (avoid scientific notation)
- Vector outputs (PDF/SVG with embedded fonts) + ultra-high DPI PNG

Run:
  py benchmark_thesis_v5_pub.py --input ./logs --out ./out --pattern "*.txt"

Disable value labels:
  py benchmark_thesis_v5_pub.py --input ./logs --out ./out --pattern "*.txt" --no-label-values
"""

from __future__ import annotations

import argparse
import math
import re
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from textwrap import fill

import pandas as pd
import matplotlib as mpl
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter

try:
    from scipy.stats import t as student_t  # type: ignore
    _HAVE_SCIPY = True
except Exception:
    student_t = None
    _HAVE_SCIPY = False

mpl.rcParams["pdf.fonttype"] = 42
mpl.rcParams["ps.fonttype"] = 42
mpl.rcParams["svg.fonttype"] = "none"


@dataclass
class RunMetrics:
    file: str
    algorithm: str
    sign_ops_per_s: float = math.nan
    verify_ops_per_s: float = math.nan
    sign_dyn_pkg_mj_per_op: float = math.nan
    verify_dyn_pkg_mj_per_op: float = math.nan
    sign_dyn_cores_mj_per_op: float = math.nan
    verify_dyn_cores_mj_per_op: float = math.nan


def _to_float(x: str) -> float:
    try:
        return float(x)
    except Exception:
        return math.nan


def parse_log_text(text: str, file_name: str) -> Optional[RunMetrics]:
    m_alg = re.search(r"^\s*Algorithm:\s+(.+?)\s*$", text, flags=re.MULTILINE)
    if not m_alg:
        m_alg2 = re.search(r"Starting Power Consumption Benchmark for\s+([^\s]+)", text)
        if not m_alg2:
            return None
        algorithm = m_alg2.group(1).strip()
    else:
        algorithm = m_alg.group(1).strip()

    run = RunMetrics(file=file_name, algorithm=algorithm)

    m_sign_tp = re.search(r"-\s*Signing:\s+.*?->\s*([0-9]*\.?[0-9]+)\s*ops/sec", text)
    m_ver_tp = re.search(r"-\s*Verification:\s+.*?->\s*([0-9]*\.?[0-9]+)\s*ops/sec", text)
    if m_sign_tp:
        run.sign_ops_per_s = _to_float(m_sign_tp.group(1))
    if m_ver_tp:
        run.verify_ops_per_s = _to_float(m_ver_tp.group(1))

    def _parse_dynamic_block(domain_label: str) -> Tuple[float, float]:
        pattern = (
            r"Dynamic Energy Analysis\s*\(\s*"
            + re.escape(domain_label)
            + r"\s*,\s*Idle-Subtracted\s*\)\s*:\s*"
            r"(.*?)(?:\n\s*\n|$)"
        )
        m = re.search(pattern, text, flags=re.DOTALL)
        if not m:
            return (math.nan, math.nan)
        block = m.group(1)
        ms = re.search(r"-\s*Signing:\s*([0-9]*\.?[0-9]+)\s*mJ/op", block)
        mv = re.search(r"-\s*Verification:\s*([0-9]*\.?[0-9]+)\s*mJ/op", block)
        return (
            _to_float(ms.group(1)) if ms else math.nan,
            _to_float(mv.group(1)) if mv else math.nan,
        )

    run.sign_dyn_pkg_mj_per_op, run.verify_dyn_pkg_mj_per_op = _parse_dynamic_block("CPU Package")
    run.sign_dyn_cores_mj_per_op, run.verify_dyn_cores_mj_per_op = _parse_dynamic_block("CPU Cores")
    return run


def read_logs(input_dir: Path, pattern: str) -> List[RunMetrics]:
    runs: List[RunMetrics] = []
    for p in sorted(input_dir.rglob(pattern)):
        if not p.is_file():
            continue
        text = p.read_text(encoding="utf-8", errors="replace")
        m = parse_log_text(text, p.name)
        if m is not None:
            runs.append(m)
    return runs


def _finite(vals: List[float]) -> List[float]:
    return [v for v in vals if isinstance(v, (int, float)) and math.isfinite(v)]


def stats_mean_sd_ci(vals: List[float], alpha: float = 0.05) -> Dict[str, float]:
    x = _finite(vals)
    n = len(x)
    if n == 0:
        return {"n": 0, "mean": math.nan, "sd": math.nan, "se": math.nan, "ci": math.nan, "cv": math.nan}
    mean = sum(x) / n
    if n == 1:
        return {"n": 1, "mean": mean, "sd": math.nan, "se": math.nan, "ci": math.nan, "cv": math.nan}

    var = sum((v - mean) ** 2 for v in x) / (n - 1)
    sd = math.sqrt(var)
    se = sd / math.sqrt(n)

    if _HAVE_SCIPY and student_t is not None:
        tcrit = float(student_t.ppf(1 - alpha / 2, df=n - 1))
    else:
        tcrit = 1.96

    ci = tcrit * se
    cv = (sd / mean * 100.0) if (mean != 0 and math.isfinite(sd) and math.isfinite(mean)) else math.nan
    return {"n": n, "mean": mean, "sd": sd, "se": se, "ci": ci, "cv": cv}


def summarize_by_algorithm(df_runs: pd.DataFrame) -> pd.DataFrame:
    metrics = [
        "sign_ops_per_s",
        "verify_ops_per_s",
        "sign_dyn_pkg_mj_per_op",
        "verify_dyn_pkg_mj_per_op",
        "sign_dyn_cores_mj_per_op",
        "verify_dyn_cores_mj_per_op",
    ]
    rows = []
    for alg, g in df_runs.groupby("algorithm", sort=True):
        row = {"algorithm": alg, "runs": int(len(g))}
        for col in metrics:
            s = stats_mean_sd_ci(g[col].tolist())
            row[f"{col}__n"] = s["n"]
            row[f"{col}__mean"] = s["mean"]
            row[f"{col}__sd"] = s["sd"]
            row[f"{col}__ci95"] = s["ci"]
            row[f"{col}__cv_pct"] = s["cv"]
        rows.append(row)
    return pd.DataFrame(rows).sort_values("algorithm").reset_index(drop=True)


def _format_cell(mean: float, sd: float, ci: float) -> str:
    if not math.isfinite(mean):
        return "—"
    if not math.isfinite(sd):
        return f"{mean:.3g}"
    if math.isfinite(ci):
        return f"{mean:.3g} ± {sd:.2g}\n(95% CI ±{ci:.2g})"
    return f"{mean:.3g} ± {sd:.2g}"


def build_thesis_table(df_summary: pd.DataFrame) -> pd.DataFrame:
    out = pd.DataFrame()
    out["Algorithm"] = df_summary["algorithm"]
    out["Runs (n)"] = df_summary["runs"]

    def cell(col):
        return [
            _format_cell(m, s, c)
            for m, s, c in zip(df_summary[f"{col}__mean"], df_summary[f"{col}__sd"], df_summary[f"{col}__ci95"])
        ]

    out["Signing throughput (ops/s)"] = cell("sign_ops_per_s")
    out["Verification throughput (ops/s)"] = cell("verify_ops_per_s")
    out["Signing energy/op, package (mJ/op)"] = cell("sign_dyn_pkg_mj_per_op")
    out["Verification energy/op, package (mJ/op)"] = cell("verify_dyn_pkg_mj_per_op")
    out["Signing energy/op, cores (mJ/op)"] = cell("sign_dyn_cores_mj_per_op")
    out["Verification energy/op, cores (mJ/op)"] = cell("verify_dyn_cores_mj_per_op")
    return out


def _wrap_labels(algs: List[str], width: int = 16) -> List[str]:
    return [fill(a, width=width, break_long_words=False, break_on_hyphens=True) for a in algs]


def _auto_figsize(algs: List[str]) -> Tuple[float, float]:
    n = len(algs)
    maxlen = max((len(a) for a in algs), default=8)
    width = max(9.5, 1.05 * n + 3.0)
    width = min(width, 27.0)
    height = max(5.4, min(9.5, 4.6 + 0.04 * maxlen))
    return (width, height)


def _filter_for_log(
    algs: List[str],
    series: List[Tuple[List[float], List[float], str]],
) -> Tuple[List[str], List[Tuple[List[float], List[float], str]]]:
    keep_idx = []
    for i in range(len(algs)):
        ok = True
        for means, _sds, _name in series:
            m = means[i]
            if not (math.isfinite(m) and m > 0):
                ok = False
                break
        if ok:
            keep_idx.append(i)

    algs2 = [algs[i] for i in keep_idx]
    series2 = []
    for means, sds, name in series:
        means2 = [means[i] for i in keep_idx]
        sds2 = [sds[i] if math.isfinite(sds[i]) else 0.0 for i in keep_idx]
        series2.append((means2, sds2, name))
    return algs2, series2


def plot_grouped_bars(
    algs_raw: List[str],
    series: List[Tuple[List[float], List[float], str]],
    title: str,
    ylabel: str,
    out_base: Path,
    log_scale: bool,
    label_values: bool,
):
    algs_wrapped = _wrap_labels(algs_raw, width=16)

    if log_scale:
        algs_wrapped, series = _filter_for_log(algs_wrapped, series)
        if len(algs_wrapped) == 0:
            return

    figsize = _auto_figsize(algs_wrapped)
    fig, ax = plt.subplots(figsize=figsize, dpi=350, constrained_layout=True)

    n_algs = len(algs_wrapped)
    n_series = len(series)
    x = list(range(n_algs))

    total_width = 0.82
    bar_w = total_width / max(1, n_series)
    offsets = [(-total_width / 2 + bar_w / 2) + j * bar_w for j in range(n_series)]

    def _fmt_label(v: float) -> str:
        if not math.isfinite(v):
            return ""
        return f"{v:.1f}"


    max_top = 0.0
    max_label_y = 0.0
    extra_artists = []  

    for j, (means, sds, name) in enumerate(series):
        xs = [xi + offsets[j] for xi in x]
        sds_safe = [sd if math.isfinite(sd) else 0.0 for sd in sds]
        bars = ax.bar(xs, means, yerr=sds_safe, capsize=4, width=bar_w, label=name)

        for m, sd in zip(means, sds_safe):
            if math.isfinite(m):
                max_top = max(max_top, m + (sd if math.isfinite(sd) else 0.0))

        if label_values:
            if log_scale:
                for rect, val in zip(bars, means):
                    if not math.isfinite(val) or val <= 0:
                        continue
                    y = val * 1.12
                    max_label_y = max(max_label_y, y)
                    t = ax.text(
                        rect.get_x() + rect.get_width() / 2,
                        y,
                        _fmt_label(val),
                        ha="center",
                        va="bottom",
                        fontsize=9,
                        clip_on=False,
                    )
                    extra_artists.append(t)
            else:
                pad = max_top * 0.02 if max_top > 0 else 0.1
                for rect, val in zip(bars, means):
                    if not math.isfinite(val):
                        continue
                    y = val + pad
                    max_label_y = max(max_label_y, y)
                    t = ax.text(
                        rect.get_x() + rect.get_width() / 2,
                        y,
                        _fmt_label(val),
                        ha="center",
                        va="bottom",
                        fontsize=9,
                        clip_on=False,
                    )
                    extra_artists.append(t)

    ax.set_xticks(x)
    ax.set_xticklabels(algs_wrapped, rotation=30, ha="right")
    ax.set_ylabel(ylabel)
    ax.set_title(title, pad=10)
    ax.legend()

    top_needed = max(max_top, max_label_y)

    if log_scale:
        ax.set_yscale("log")
        if top_needed > 0:
            ax.set_ylim(top=top_needed * 1.40)
    else:
        if top_needed > 0:
            ax.set_ylim(0, top_needed * 1.18)

    if not log_scale and "ops/s" in ylabel:
        ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:,.0f}"))

    for ext in [".pdf", ".svg"]:
        fig.savefig(
            out_base.with_suffix(ext),
            bbox_inches="tight",
            bbox_extra_artists=extra_artists,
            pad_inches=0.20,
        )
    fig.savefig(
        out_base.with_suffix(".png"),
        dpi=900,
        bbox_inches="tight",
        bbox_extra_artists=extra_artists,
        pad_inches=0.20,
    )

    plt.close(fig)


def make_grouped_pair_scales(
    df_summary: pd.DataFrame,
    metric_a: str, label_a: str,
    metric_b: str, label_b: str,
    title_base: str,
    ylabel: str,
    out_prefix: Path,
    label_values: bool,
):
    algs = df_summary["algorithm"].tolist()

    means_a = df_summary[f"{metric_a}__mean"].tolist()
    sds_a = df_summary[f"{metric_a}__sd"].tolist()
    means_b = df_summary[f"{metric_b}__mean"].tolist()
    sds_b = df_summary[f"{metric_b}__sd"].tolist()

    series = [
        (means_a, sds_a, label_a),
        (means_b, sds_b, label_b),
    ]

    plot_grouped_bars(
        algs, series,
        f"{title_base} (mean ± SD) [linear]",
        ylabel,
        Path(str(out_prefix) + "_linear"),
        log_scale=False,
        label_values=label_values,
    )
    plot_grouped_bars(
        algs, series,
        f"{title_base} (mean ± SD) [log scale]",
        ylabel,
        Path(str(out_prefix) + "_log"),
        log_scale=True,
        label_values=label_values,
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", type=str, required=True)
    ap.add_argument("--out", type=str, required=True)
    ap.add_argument("--pattern", type=str, default="*.txt")
    ap.add_argument("--no-label-values", action="store_true")
    args = ap.parse_args()

    in_dir = Path(args.input).expanduser().resolve()
    out_dir = Path(args.out).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    runs = read_logs(in_dir, args.pattern)
    if len(runs) == 0:
        raise SystemExit(f"No benchmark logs found in {in_dir} with pattern {args.pattern}")

    if not _HAVE_SCIPY:
        print(
            "NOTE: SciPy not found. 95% CI uses normal approximation (±1.96*SE). "
            "For small n, install SciPy for t-based CI: pip install scipy"
        )

    df_runs = pd.DataFrame([asdict(r) for r in runs])

    key_cols = [
        "sign_ops_per_s", "verify_ops_per_s",
        "sign_dyn_pkg_mj_per_op", "verify_dyn_pkg_mj_per_op",
        "sign_dyn_cores_mj_per_op", "verify_dyn_cores_mj_per_op",
    ]
    df_runs["usable"] = df_runs[key_cols].apply(lambda row: any(math.isfinite(v) for v in row.tolist()), axis=1)
    df_runs = df_runs[df_runs["usable"]].drop(columns=["usable"]).reset_index(drop=True)

    df_runs.to_csv(out_dir / "runs_parsed.csv", index=False)

    df_summary = summarize_by_algorithm(df_runs)
    df_summary.to_csv(out_dir / "summary_by_algorithm_numeric.csv", index=False)

    df_table = build_thesis_table(df_summary)
    df_table.to_csv(out_dir / "summary_by_algorithm_thesis_table.csv", index=False)

    label_values = not args.no_label_values

    make_grouped_pair_scales(
        df_summary,
        "sign_ops_per_s", "Signing",
        "verify_ops_per_s", "Verification",
        "Throughput by algorithm",
        "ops/s",
        out_dir / "fig_throughput_sign_vs_verify",
        label_values,
    )

    make_grouped_pair_scales(
        df_summary,
        "sign_dyn_pkg_mj_per_op", "Signing",
        "verify_dyn_pkg_mj_per_op", "Verification",
        "Dynamic energy/op by algorithm (CPU Package, idle-subtracted)",
        "mJ/op",
        out_dir / "fig_energy_pkg_sign_vs_verify",
        label_values,
    )

    make_grouped_pair_scales(
        df_summary,
        "sign_dyn_cores_mj_per_op", "Signing",
        "verify_dyn_cores_mj_per_op", "Verification",
        "Dynamic energy/op by algorithm (CPU Cores, idle-subtracted)",
        "mJ/op",
        out_dir / "fig_energy_cores_sign_vs_verify",
        label_values,
    )

    print(f"Parsed runs: {len(df_runs)} from {in_dir}")
    print(f"Algorithms: {len(df_summary)}")
    print(f"Wrote outputs to: {out_dir}")
    print("Created thesis-ready grouped Signing vs Verification figures (linear + log) for throughput and energy.")


if __name__ == "__main__":
    main()
