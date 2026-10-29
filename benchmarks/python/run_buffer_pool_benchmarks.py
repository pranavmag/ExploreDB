import subprocess
from pathlib import Path

BENCHMARK_EXE = Path("./build/buffer_pool_benchmark")

RESULTS_FILE = Path("benchmarks/results/buffer_pool_results.csv")

BUFFER_SIZES = [2, 4, 8, 16, 32]
NUM_PAGES_VALUES = [4, 8, 16, 32, 64]

NUM_ACCESSES = 10_000
REPETITIONS = 10

WORKLOAD = "sequential"


def run_benchmark(
    buffer_size: int,
    num_pages: int,
    num_accesses: int,
    workload: str,
    run: int,
) -> None:
    command = [
        str(BENCHMARK_EXE),
        "--buffer-size",
        str(buffer_size),
        "--num-pages",
        str(num_pages),
        "--num-accesses",
        str(num_accesses),
        "--workload",
        workload,
        "--run",
        str(run),
        "--output",
        str(RESULTS_FILE),
    ]

    subprocess.run(command, check=True,)


def main() -> None:
    RESULTS_FILE.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    # Start each experiment with a fresh CSV.
    RESULTS_FILE.unlink(missing_ok=True)

    total_runs = (
        len(NUM_PAGES_VALUES)
        * len(BUFFER_SIZES)
        * REPETITIONS
    )

    current_run = 0

    for num_pages in NUM_PAGES_VALUES:
        for buffer_size in BUFFER_SIZES:
            for run in range(REPETITIONS):
                current_run += 1

                print(
                    f"[{current_run}/{total_runs}] "
                    f"pages={num_pages}, "
                    f"buffer={buffer_size}, "
                    f"run={run}"
                )

                run_benchmark(
                    buffer_size=buffer_size,
                    num_pages=num_pages,
                    num_accesses=NUM_ACCESSES,
                    workload=WORKLOAD,
                    run=run,
                )

    print()
    print(
        f"Benchmark complete: {RESULTS_FILE}"
    )


if __name__ == "__main__":
    main()