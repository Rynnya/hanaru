#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace hanaru {

    class Beatmap {
    public:
        Beatmap(std::string&& name, std::string&& content);

        const std::string& name() const;
        const std::string& content() const;

        size_t size() const;

    private:
        std::string name_ {};
        std::string content_ {};
    };

    namespace storage {

        void initialize(std::string&& beatmapsPath, size_t requiredFreeSpace);

        std::shared_ptr<const Beatmap> insert(int64_t id, std::string&& name, std::string&& content);
        std::shared_ptr<const Beatmap> find(int64_t id);

        bool canWrite() noexcept;
        void decreaseAvailableSpace(size_t memoryInBytes) noexcept;

        const std::filesystem::path& getBeatmapsPath() noexcept;

    }

}