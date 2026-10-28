import pandas as pd

df = pd.read_csv(
    "benchmarks/results/buffer_pool_results.csv"
)

print(df)
print()
print(df[[
    "buffer_size",
    "hit_rate",
    "evictions",
    "disk_reads",
    "elapsed_us"
]])

print(df.dtypes)