import glob
import re
import numpy as np
from pathlib import Path

# -------- regex patterns --------
PKG_IDLE_RE = re.compile(r"Idle \(CPU\):\s+([\d.]+)\s+J")
CORE_IDLE_RE = re.compile(r"Idle \(Cores\):\s+([\d.]+)\s+J")


def coefficient_of_variation_percent(values):
    values = np.array(values, dtype=float)
    return (np.std(values, ddof=1) / np.mean(values)) * 100.0


def compute_overall_idle_cv_percent(log_dir="."):
    pkg_idle = []
    core_idle = []

    for txt in glob.glob(str(Path(log_dir) / "*.txt")):
        with open(txt, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                m = PKG_IDLE_RE.search(line)
                if m:
                    pkg_idle.append(float(m.group(1)))

                m = CORE_IDLE_RE.search(line)
                if m:
                    core_idle.append(float(m.group(1)))

    if not pkg_idle or not core_idle:
        raise RuntimeError("No idle energy values found.")

    return {
        "files_processed": len(set(glob.glob(str(Path(log_dir) / "*.txt")))),
        "pkg_idle_mean_J": np.mean(pkg_idle),
        "pkg_idle_cv_percent": coefficient_of_variation_percent(pkg_idle),
        "core_idle_mean_J": np.mean(core_idle),
        "core_idle_cv_percent": coefficient_of_variation_percent(core_idle),
    }


if __name__ == "__main__":
    stats = compute_overall_idle_cv_percent("./logs")  # change path if needed

    print("OVERALL IDLE ENERGY CV (PERCENT)")
    print(f"Files processed: {stats['files_processed']}")
    print(f"CPU Package Idle: mean = {stats['pkg_idle_mean_J']:.2f} J, CV = {stats['pkg_idle_cv_percent']:.2f} %")
    print(f"CPU Cores Idle:   mean = {stats['core_idle_mean_J']:.2f} J, CV = {stats['core_idle_cv_percent']:.2f} %")
