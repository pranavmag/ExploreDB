#include "storage/disk_manager.h"
#include "storage/page.h"

#include <array>
#include <stdexcept>

bool DiskManager::IsValidPageId(int page_id) const {
  if (page_id < 0) {
    return false;
  }

  auto file_size = fs::file_size(file_path_);
  auto page_count = file_size / PAGE_SIZE;

  return static_cast<std::size_t>(page_id) < page_count;
}

DiskManager::DiskManager(const fs::path& file_path): file_path_(file_path) {
  if (!fs::exists(file_path_)) {
    std::ofstream create_file(
      file_path_,
      std::ios::binary
    );

    if (!create_file) {
      throw std::runtime_error("Failed to create database file");
    }
  }

  file_.open(
    file_path_,
    std::ios::in | std::ios::out | std::ios::binary
  );

  if (!file_.is_open()) {
    throw std::runtime_error("Failed to open database file");
  }

  auto file_size = fs::file_size(file_path_);

  if (file_size % PAGE_SIZE != 0) {
    throw std::runtime_error("Database file is not page aligned");
  }
}

int DiskManager::AllocatePage() {
  std::lock_guard<std::mutex> guard(disk_mutex_);

  auto file_size = fs::file_size(file_path_);

  if (file_size % PAGE_SIZE != 0) {
    throw std::runtime_error("Database file is not page aligned");
  }

  int page_id = static_cast<int>(file_size / PAGE_SIZE);

  std::array<std::byte, PAGE_SIZE> zero_page{};

  file_.clear();
  file_.seekp(0, std::ios::end);

  file_.write(reinterpret_cast<const char*>(zero_page.data()), PAGE_SIZE);

  if (!file_) {
    throw std::runtime_error("Failed to allocate page");
  }

  file_.flush();

  if (!file_) {
    throw std::runtime_error("Failed to flush page");
  }

  return page_id;
}

std::size_t DiskManager::ReadPage(int page_id, Page &page) {
  std::lock_guard<std::mutex> guard(disk_mutex_);

  if (!IsValidPageId(page_id)) {
    throw std::out_of_range("Invalid page id");
  }

  std::size_t offset = static_cast<std::size_t>(page_id) * PAGE_SIZE;

  file_.clear();
  file_.seekg(offset, std::ios::beg);

  file_.read(reinterpret_cast<char*>(page.GetData().data()), PAGE_SIZE);

  if (!file_ || file_.gcount() != PAGE_SIZE) {
    throw std::runtime_error("Failed to read full page");
  }

  return PAGE_SIZE;
}

std::size_t DiskManager::WritePage(int page_id, const Page &page) {
  std::lock_guard<std::mutex> guard(disk_mutex_);

  if (!IsValidPageId(page_id)) {
    throw std::out_of_range("Invalid page id");
  }

  std::size_t offset = static_cast<std::size_t>(page_id) * PAGE_SIZE;

  file_.clear();
  file_.seekp(offset, std::ios::beg);

  file_.write(reinterpret_cast<const char*>(page.GetData().data()), PAGE_SIZE);

  if (!file_) {
    throw std::runtime_error("Failed to write full page");
  }

  file_.flush();

  if (!file_) {
    throw std::runtime_error("Failed to flush page");
  }

  return PAGE_SIZE;
}