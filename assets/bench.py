import subprocess
import sys
import re
import statistics


def run_bench(target, count=5):
    data = {
        "branches": [],
        "misses": [],
        "cycles": [],
        "instructions": [],
        "bad_speculation": [],
        "backend_bound": [],
        "frontend_bound": [],
        "retiring": [],
        "page_faults": [],
        "ipc": [],
        "real": [],
        "user": [],
        "sys": [],
    }

    for i in range(count):
        print("Running: " + str(i + 1))
        # Explicitly asking for events to ensure they appear in the output
        cmd = [
            "sudo",
            "taskset",
            "-c",
            "0,1",
            "chrt",
            "-f",
            "99",
            "perf",
            "stat",
            "-e",
            "branches,branch-misses,cycles,instructions,page-faults",
            "-M",
            "TopdownL1",
            target,
            "-r",
            "../linux",
        ]
        result = subprocess.run(
            cmd, stderr=subprocess.PIPE, stdout=subprocess.DEVNULL, text=True
        )
        output = result.stderr

        # Helper to extract value by pattern
        def get_val(pattern, group=1):
            match = re.search(pattern, output)
            return float(match.group(group).replace(",", "")) if match else 0.0

        data["branches"].append(get_val(r"([\d,]+)\s+branches"))
        data["misses"].append(get_val(r"([\d,]+)\s+branch-misses"))
        data["cycles"].append(get_val(r"([\d,]+)\s+cycles"))
        data["instructions"].append(get_val(r"([\d,]+)\s+instructions"))
        data["page_faults"].append(get_val(r"([\d,]+)\s+page-faults"))

        # Calculate IPC from instructions/cycles
        ins = data["instructions"][-1]
        cyc = data["cycles"][-1]
        data["ipc"].append(ins / cyc if cyc > 0 else 0.0)

        # Extraction for Topdown
        # Use the perf-calculated percentages directly from the output
        data["bad_speculation"].append(get_val(r"([\d.]+)\s+%\s+tma_bad_speculation"))
        data["backend_bound"].append(get_val(r"([\d.]+)\s+%\s+tma_backend_bound"))
        data["frontend_bound"].append(get_val(r"([\d.]+)\s+%\s+tma_frontend_bound"))
        data["retiring"].append(get_val(r"([\d.]+)\s+%\s+tma_retiring"))

        # Time extraction
        data["real"].append(get_val(r"([\d.]+) seconds time elapsed"))
        data["user"].append(get_val(r"([\d.]+) seconds user"))
        data["sys"].append(get_val(r"([\d.]+) seconds sys"))

    print(f"--- Results for {target} ---")
    metrics = {
        "Branches": data["branches"],
        "Misses": data["misses"],
        "Cycles": data["cycles"],
        "Instructions": data["instructions"],
        "Page Faults": data["page_faults"],
        "IPC": data["ipc"],
        "Bad Speculation (%)": data["bad_speculation"],
        "Backend Bound (%)": data["backend_bound"],
        "Frontend Bound (%)": data["frontend_bound"],
        "Retiring (%)": data["retiring"],
        "Real Time (s)": data["real"],
        "User Time (s)": data["user"],
        "Sys Time (s)": data["sys"],
    }

    for name, vals in metrics.items():
        avg = statistics.mean(vals)
        print(f"{name:20}: {avg:.5f}")

    avg_br = statistics.mean(data["branches"])
    avg_ms = statistics.mean(data["misses"])
    print(f"{'Branch Miss Rate':20}: {(avg_ms/avg_br)*100:.5f}%")


if __name__ == "__main__":
    if (len(sys.argv) < 2) or (len(sys.argv) > 3):
        print("Usage: python3 bench.py <executable> <amount_of_runs default=5>")
        sys.exit(1)
    runs = 5
    if len(sys.argv) == 3:
        runs = int(sys.argv[2])
    run_bench(sys.argv[1], runs)
