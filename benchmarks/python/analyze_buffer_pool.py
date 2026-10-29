import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv(
    "benchmarks/results/buffer_pool_results.csv"
)

summary = (
    df.groupby([
        "num_pages",
        "buffer_size"
    ])
    .agg(
        hit_rate=("hit_rate", "mean"),
        evictions=("evictions", "mean"),
        median_elapsed_us=("elapsed_us", "median"),
    )
    .reset_index()
)

print(summary)

hit_rate_matrix = summary.pivot(
    index="buffer_size",
    columns="num_pages",
    values="hit_rate",
)

print(hit_rate_matrix)

image = plt.imshow(
    hit_rate_matrix.to_numpy(dtype=float),
    aspect="auto",
    origin="lower",
    vmin=0.0,
    vmax=1.0,
    cmap="viridis",
    interpolation="nearest",
)

plt.colorbar(
    image,
    label="Hit Rate",
)

plt.xticks(
    range(len(hit_rate_matrix.columns)),
    hit_rate_matrix.columns.tolist(),
)

plt.yticks(
    range(len(hit_rate_matrix.index)),
    hit_rate_matrix.index.tolist(),
)

# Draw boundaries between measured configurations.
plt.gca().set_xticks(
    [x - 0.5 for x in range(1, len(hit_rate_matrix.columns))],
    minor=True,
)

plt.gca().set_yticks(
    [y - 0.5 for y in range(1, len(hit_rate_matrix.index))],
    minor=True,
)

plt.grid(
    which="minor",
    linewidth=1,
)

plt.tick_params(
    which="minor",
    bottom=False,
    left=False,
)

for row in range(len(hit_rate_matrix.index)):
    for col in range(len(hit_rate_matrix.columns)):
        value = hit_rate_matrix.iloc[row, col]

        plt.text(
            col,
            row,
            f"{value:.2f}",
            ha="center",
            va="center",
        )

plt.xlabel("Working Set Size (pages)")
plt.ylabel("Buffer Size (frames)")
plt.title("CLOCK — Sequential Workload Hit Rate")

plt.show()
