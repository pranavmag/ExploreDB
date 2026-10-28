# pragma once

#include "buffer/clock_replacer.h"
#include "storage/disk_manager.h"
#include "buffer/frame.h"
#include "buffer/page_table.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

struct BufferPoolStats {
    std::uint64_t fetch_requests{0};
    std::uint64_t hits{0};
    std::uint64_t misses{0};
    std::uint64_t evictions{0};
    std::uint64_t disk_reads{0};
    std::uint64_t disk_writes{0};
    std::uint64_t dirty_evictions{0};
};

class BufferPoolManager {
private:
    std::vector<std::unique_ptr<Frame>> frame_array_;
    PageTable page_table_;
    ClockReplacer clock_replacer_;
    DiskManager disk_manager_;

    mutable std::mutex bpm_mutex_;

    bool AddPage(int page_id);

    std::optional<int> AcquireFrame();

    BufferPoolStats stats_;

public:
  BufferPoolManager(std::size_t num_frames, const fs::path& file_path)
      : clock_replacer_(num_frames), disk_manager_(file_path) {
        frame_array_.reserve(num_frames);

        for (std::size_t i{}; i < num_frames; ++i) {
            frame_array_.emplace_back(std::make_unique<Frame>(i));
        }
    }

    Page* NewPage(int& page_id);

    Page* FetchPage(int page_id);

    bool UnpinPage(int page_id, bool is_dirty);

    bool FlushPage(int page_id);

    bool FlushAllPages();

    BufferPoolStats GetStats() const;

    void ResetStats();
};