#pragma once

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>

#include "storage_manager.hh"

namespace hanaru {

    namespace downloader {

        void initialize(const std::string& apiKey, const std::string& username, const std::string& password);

        void downloadBeatmap(int64_t id, std::function<void(std::tuple<Json::Value, drogon::HttpStatusCode>&&)>&& callback);
        void downloadBeatmapset(int64_t id, std::function<void(std::tuple<Json::Value, drogon::HttpStatusCode>&&)>&& callback);
        void downloadMap(int64_t id, std::function<void(std::tuple<drogon::HttpStatusCode, std::string, std::string>&&)>&& callback);

        Json::Value serializeBeatmap(const Json::Value& json);
        std::string getFilenameFromLink(const std::unordered_multimap<std::string, std::string>& headers);
        void saveBeatmapToDB(int64_t id, const std::string& filename);

    }

}
