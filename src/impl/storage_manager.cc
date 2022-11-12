#include "storage_manager.hh"

#include <drogon/HttpAppFramework.h>

#include <thread>

#include "../thirdparty/concurrent_cache.hh"

namespace detail {

    // Hard drive information
    std::atomic_size_t currentFreeSpace_ = 0;
    std::size_t requiredFreeSpace_ = 0;
    std::filesystem::path beatmapsPath {};

    cache::LRUCache<int64_t, hanaru::Beatmap> cache_ {};

}

namespace hanaru {

    Beatmap::Beatmap(std::string&& name, std::string&& content)
        : name_ { std::move(name) }
        , content_ { std::move(content) }
    {}

    const std::string& Beatmap::name() const {
        return name_;
    }

    const std::string& Beatmap::content() const {
        return content_;
    }

    size_t Beatmap::size() const {
        return content_.size();
    }

    void storage::initialize(std::string&& beatmapsPath, size_t requiredFreeSpace) {
        detail::requiredFreeSpace_ = requiredFreeSpace;

        std::filesystem::space_info si = std::filesystem::space(".");
        detail::currentFreeSpace_ = si.available - (detail::requiredFreeSpace_ << 20);

        detail::beatmapsPath = beatmapsPath;
    }

    std::shared_ptr<const Beatmap> storage::insert(int64_t id, std::string&& name, std::string&& content) {
        if (name.empty()) {
            return {};
        }

        if (content.empty()) {
            return detail::cache_.insert(id, nullptr);
        }

        return detail::cache_.insert(id, { std::move(name), std::move(content) });
    }

    std::shared_ptr<const Beatmap> storage::find(int64_t id) {
        return detail::cache_.find(id);
    }

    bool storage::canWrite() noexcept {
        return detail::currentFreeSpace_ > 0;
    }

    void storage::decreaseAvailableSpace(size_t memoryInBytes) noexcept {
        size_t previousSpace = detail::currentFreeSpace_.load(std::memory_order_relaxed);
        size_t newSpace = 0;

        do {
            newSpace = previousSpace < memoryInBytes ? 0 : previousSpace - memoryInBytes;
        } while (!detail::currentFreeSpace_.compare_exchange_weak(previousSpace, newSpace, std::memory_order_relaxed, std::memory_order_relaxed));
    }

    const std::filesystem::path& storage::getBeatmapsPath() noexcept {
        return detail::beatmapsPath;
    }

}
