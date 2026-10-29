#include "buffer/buffer_pool_manager.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct BenchmarkConfig {
    std::size_t buffer_size = 4;
    int num_pages = 16;
    int num_accesses = 1000;
    std::string workload = "sequential";
    int run = 0;

    std::filesystem::path output = "benchmarks/results/buffer_pool_results.csv";
};

BenchmarkConfig ParseArguments(int argc, char* argv[]) {
    BenchmarkConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (arg == "--buffer-size") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for --buffer-size");
            }

            config.buffer_size =
                static_cast<std::size_t>(std::stoull(argv[++i]));
        }
        else if (arg == "--num-pages") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for --num-pages");
            }

            config.num_pages = std::stoi(argv[++i]);
        }
        else if (arg == "--num-accesses") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for --num-accesses");
            }

            config.num_accesses = std::stoi(argv[++i]);
        }
        else if (arg == "--workload") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for --workload");
            }

            config.workload = argv[++i];
        }
        else if (arg == "--run") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for --run");
            }

            config.run = std::stoi(argv[++i]);
        }
        else if (arg == "--output") {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for --output");
            }

            config.output = argv[++i];
        }
        else {
            throw std::runtime_error("Unknown argument: " + std::string(arg));
        }
    }

    return config;
}

std::vector<int> SetupPages(std::size_t buffer_size, int num_pages, const std::filesystem::path& db_path) {
    std::vector<int> page_ids;
    page_ids.reserve(num_pages);

    {
        BufferPoolManager setup_bpm (
            buffer_size,
            db_path
        );

        for (int i = 0; i < num_pages; ++i) {
          int page_id = -1;

          Page *page = setup_bpm.NewPage(page_id);

          if (page == nullptr) {
            throw std::runtime_error("Failed to allocate benchmark page");
          }

          page_ids.push_back(page_id);

          setup_bpm.UnpinPage(page_id, false);
        }

        setup_bpm.FlushAllPages();
    }

    return page_ids;
}

BufferPoolStats RunSequentialWorkload(
    BufferPoolManager& bpm,
    const std::vector<int>& page_ids,
    int num_accesses,
    long long& elapsed_us
) {
    bpm.ResetStats();

    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_accesses; ++i) {
        const int page_id =
            page_ids[
                static_cast<std::size_t>(i)
                % page_ids.size()
            ];

        Page* page = bpm.FetchPage(page_id);

        if (page == nullptr) {
            throw std::runtime_error("Failed to fetch benchmark page");
        }

        bpm.UnpinPage(page_id, false);
    }

    const auto end = std::chrono::steady_clock::now();

    elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    return bpm.GetStats();
}

void PrintResults(
    const BufferPoolStats& stats,
    std::size_t buffer_size,
    int num_pages,
    int num_accesses,
    long long elapsed_us
) {
    const double hit_rate =
        stats.fetch_requests == 0
            ? 0.0
            : static_cast<double>(stats.hits) / stats.fetch_requests;

    std::cout
        << "policy: clock\n"
        << "workload: sequential\n"
        << "buffer_size: " << buffer_size << '\n'
        << "num_pages: " << num_pages << '\n'
        << "num_accesses: " << num_accesses << '\n'
        << "hits: " << stats.hits << '\n'
        << "misses: " << stats.misses << '\n'
        << "evictions: " << stats.evictions << '\n'
        << "disk_reads: " << stats.disk_reads << '\n'
        << "disk_writes: " << stats.disk_writes << '\n'
        << "dirty_evictions: " << stats.dirty_evictions << '\n'
        << "hit_rate: " << hit_rate << '\n'
        << "elapsed_us: " << elapsed_us << '\n';
}

void WriteResultCSV(
    const std::filesystem::path& csv_path,
    const BufferPoolStats& stats,
    std::string_view policy,
    std::string_view workload,
    std::size_t buffer_size,
    int num_pages,
    int num_accesses,
    int run,
    long long elapsed_us
) {
    const bool file_exists = std::filesystem::exists(csv_path);

    std::ofstream out(
        csv_path,
        std::ios::app
    );

    if (!out) {
        throw std::runtime_error("Failed to open benchmark CSV");
    }

    if (!file_exists) {
        out
            << "policy,workload,buffer_size,num_pages,"
               "num_accesses,run,hits,misses,evictions,"
               "disk_reads,disk_writes,dirty_evictions,"
               "hit_rate,elapsed_us\n";
    }

    const double hit_rate =
        stats.fetch_requests == 0
            ? 0.0
            : static_cast<double>(stats.hits)
                / stats.fetch_requests;

    out
        << policy << ','
        << workload << ','
        << buffer_size << ','
        << num_pages << ','
        << num_accesses << ','
        << run << ','
        << stats.hits << ','
        << stats.misses << ','
        << stats.evictions << ','
        << stats.disk_reads << ','
        << stats.disk_writes << ','
        << stats.dirty_evictions << ','
        << hit_rate << ','
        << elapsed_us << '\n';
}

int main(int argc, char* argv[]) {    
    try {
        const BenchmarkConfig config = ParseArguments(argc, argv);

        const std::filesystem::path db_path = "buffer_pool_benchmark.db";

        std::filesystem::remove(db_path);

        const auto page_ids =
            SetupPages(
                config.buffer_size,
                config.num_pages,
                db_path
            );

        BufferPoolManager bpm(
            config.buffer_size,
            db_path
        );

        long long elapsed_us = 0;

        BufferPoolStats stats;

        if (config.workload == "sequential") {
            stats =
                RunSequentialWorkload(
                    bpm,
                    page_ids,
                    config.num_accesses,
                    elapsed_us
                );
        }
        else {
            throw std::runtime_error(
                "Unsupported workload: "
                + config.workload
            );
        }

        WriteResultCSV(
            config.output,
            stats,
            "clock",
            config.workload,
            config.buffer_size,
            config.num_pages,
            config.num_accesses,
            config.run,
            elapsed_us
        );

        std::filesystem::remove(db_path);
    }
    catch (const std::exception& e) {
        std::cerr
            << "Benchmark error: "
            << e.what()
            << '\n';

        return 1;
    }

    return 0;
}