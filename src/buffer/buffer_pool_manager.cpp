#include "buffer/buffer_pool_manager.h"

#include <cassert>
#include <optional>

// Caller must hold bpm_mutex_.
bool BufferPoolManager::AddPage(int page_id) {
    if (page_table_.GetMapping(page_id)) {
        return true;
    }

    if (!disk_manager_.IsValidPageId(page_id)) {
        return false;
    }

    auto frame_id = AcquireFrame();

    if (!frame_id) {
        return false;
    }

    Frame& frame = *frame_array_[*frame_id];

    frame.AssignPage(page_id);

    Page& page = frame.GetPage();

    {
        auto guard = page.WriteLatch();

        disk_manager_.ReadPage(page_id, page);

        ++stats_.disk_reads;
    }

    page_table_.AddMapping(page_id, *frame_id);

    return true;
}

std::optional<int> BufferPoolManager::AcquireFrame() {
    for (auto& frame_ptr : frame_array_) {
        Frame& frame = *frame_ptr;

        if (frame.IsEmpty()) {
            return frame.GetFrameId();
        }
    }

    // No Empty Frame Found, Needs Eviction
    std::optional<int> victim = clock_replacer_.FindVictim(frame_array_);

    if (victim) {
        ++stats_.evictions;

        Frame& frame = *frame_array_[*victim];

        if (frame.IsDirty()) {
            ++stats_.dirty_evictions;

            Page& page = frame.GetPage();

            auto guard = page.WriteLatch();

            disk_manager_.WritePage(page.GetPageId(), page);

            ++stats_.disk_writes;

            frame.ClearDirty();
        }

        page_table_.RemoveMapping(frame.GetPage().GetPageId());

        return frame.GetFrameId();
    }

    return std::nullopt;
}

Page* BufferPoolManager::NewPage(int& page_id) {
    std::lock_guard<std::mutex> guard(bpm_mutex_);

    auto frame_id = AcquireFrame();

    if (!frame_id) {
        return nullptr;
    }

    page_id = disk_manager_.AllocatePage();

    Frame& frame = *frame_array_[*frame_id];

    frame.AssignPage(page_id);

    // clearing potential stale memory from the victim frame
    {
        Page& page = frame.GetPage();
        auto page_guard = page.WriteLatch();

        page.GetData().fill(std::byte{0});
    }   

    page_table_.AddMapping(page_id, *frame_id);

    frame.IncrementPinCount();

    clock_replacer_.SetReferenceBit(*frame_id);

    return &frame.GetPage();
}

Page* BufferPoolManager::FetchPage(int page_id) {
    std::lock_guard<std::mutex> guard(bpm_mutex_);

    ++stats_.fetch_requests;

    auto frame_id = page_table_.GetMapping(page_id);

    if (!frame_id) {
        ++stats_.misses;

        if (!AddPage(page_id)) {
            return nullptr;
        }

        frame_id = page_table_.GetMapping(page_id);

        assert(frame_id.has_value());
    }
    else {
        ++stats_.hits;
    }

    Frame &frame = *frame_array_[*frame_id];

    frame.IncrementPinCount();
    clock_replacer_.SetReferenceBit(*frame_id);

    return &frame.GetPage();
}

bool BufferPoolManager::UnpinPage(int page_id, bool is_dirty) {
    std::lock_guard<std::mutex> guard(bpm_mutex_);

    std::optional<int> frame_id = page_table_.GetMapping(page_id);

    if (frame_id) {
        Frame& frame = *frame_array_[*frame_id];

        if (is_dirty) {
            frame.MarkDirty();
        }

        return frame.DecrementPinCount();
    }

    return false;
}

bool BufferPoolManager::FlushPage(int page_id) {
    std::lock_guard<std::mutex> guard(bpm_mutex_);

    std::optional<int> frame_id = page_table_.GetMapping(page_id);

    if (frame_id) {
        Frame& frame = *frame_array_[*frame_id];

        if (!frame.IsDirty()) {
            return true;
        }

        const Page& page = frame.GetPage();

        auto page_guard = page.ReadLatch();

        disk_manager_.WritePage(page_id, page);

        ++stats_.disk_writes;

        frame.ClearDirty();

        return true;
    }

    return false;
}

bool BufferPoolManager::FlushAllPages() {
    std::lock_guard<std::mutex> guard(bpm_mutex_);

    for (auto& frame_ptr: frame_array_) {
        Frame& frame = *frame_ptr;

        if (frame.IsEmpty() || !frame.IsDirty()) {
            continue;
        }

        const Page& page = frame.GetPage();

        auto page_guard = page.ReadLatch();

        disk_manager_.WritePage(page.GetPageId(), page);

        ++stats_.disk_writes;

        frame.ClearDirty();
    }

    return true;
}

BufferPoolStats BufferPoolManager::GetStats() const {
    std::lock_guard<std::mutex> guard(bpm_mutex_);
    return stats_;
}

void BufferPoolManager::ResetStats() {
    std::lock_guard<std::mutex> guard(bpm_mutex_);
    stats_ = {};
}
