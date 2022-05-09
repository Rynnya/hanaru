#include "storage_manager.hh"

#include <drogon/HttpAppFramework.h>

#include <thread>
#include <filesystem>

#include "concurrent_cache.hh"

namespace detail {

    static hanaru::storage_manager* instance_ = nullptr;

    // Hard drive information
    std::atomic_int64_t current_free_space_ = 0;

    // RAM information
    std::atomic_int64_t current_ram_usage_ = 0;
}

hanaru::cached_beatmap::cached_beatmap(
    const std::string& name_, 
    const std::string& content_,
    bool retry_,
    std::chrono::system_clock::time_point time_
)
    : name(name_)
    , content(content_)
    , retry(retry_)
    , timestamp(time_)
{};

hanaru::storage_manager::storage_manager(
    size_t maximum_cache_size,
    int64_t required_free_space
)
    : maximum_cache_size_(std::max(static_cast<size_t>(32), maximum_cache_size))
    , required_free_space_(std::max(static_cast<int64_t>(1024), required_free_space))
{
    std::filesystem::space_info si = std::filesystem::space(".");
    detail::current_free_space_ = si.available - (required_free_space << 20);

    detail::instance_ = this;
}

int64_t hanaru::storage_manager::memory_usage() const {
    // Convert raw bytes to megabytes
    return detail::current_ram_usage_ >> 20;
}

void hanaru::storage_manager::insert(const int64_t id, hanaru::cached_beatmap&& btm) {
    detail::current_free_space_ -= btm.content.size();
    detail::current_ram_usage_ += btm.content.size();

    cache_.insert(id, std::move(btm));
}

std::shared_ptr<hanaru::cached_beatmap> hanaru::storage_manager::find(int64_t id) {
    return cache_.get(id);
}

bool hanaru::storage_manager::can_write() const {
    return detail::current_free_space_ > 0;
}

hanaru::storage_manager& hanaru::storage_manager::get() {
    return *detail::instance_;
}
