#pragma once

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>

namespace hanaru {

    class downloader {
    public:
        downloader(const std::string& _username, const std::string& _password, const std::string& _api_key);

        drogon::Task<std::tuple<Json::Value, drogon::HttpStatusCode>> download_beatmap(int64_t id);
        drogon::Task<std::tuple<Json::Value, drogon::HttpStatusCode>> download_beatmapset(int64_t id);
        drogon::Task<std::tuple<drogon::HttpStatusCode, std::string, std::string>> download_map(int64_t id);

        static downloader& get();
    private:
        Json::Value serialize_beatmap(const Json::Value& json) const;
        std::string get_filename_from_link(const std::unordered_multimap<std::string, std::string>& headers) const;

        void authorize();
        void deauthorize();

        const std::string username = "";
        const std::string password = "";
        const std::string api_key  = "";

        drogon::HttpClientPtr b_client = drogon::HttpClient::newHttpClient("https://osu.ppy.sh");
        drogon::HttpClientPtr s_client = drogon::HttpClient::newHttpClient("https://osu.ppy.sh");

        const std::tuple<Json::Value, drogon::HttpStatusCode> not_found = { Json::objectValue, drogon::k404NotFound };
    };
}
