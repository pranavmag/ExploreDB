# pragma once

#include "page.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace fs = std::filesystem;

class DiskManager {
private:
    fs::path file_path_;
    std::fstream file_;
    std::mutex disk_mutex_;
 
public:
    explicit DiskManager(const fs::path& file_path);

    [[nodiscard]] bool IsValidPageId(int page_id) const;

    [[nodiscard]] int AllocatePage();
    
    std::size_t ReadPage(int page_id, Page& page);

    std::size_t WritePage(int page_id, const Page& page);
};