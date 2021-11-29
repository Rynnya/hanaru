#include "beatmap_set_route.hh"

#include "../impl/globals.hh"
#include "../impl/downloader.hh"
#include "../impl/rate_limiter.hh"

void BeatmapSetRoute::get(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, int32_t id) {
    if (!hanaru::rate_limit::consume(1)) {
        SEND_ERROR(callback, k429TooManyRequests, "rate limit, please try again");
    }

    auto db = app().getDbClient();

    const orm::Result& result = db->execSqlSync("SELECT * FROM beatmaps WHERE beatmapset_id = ?", id);
    if (result.empty()) {
        if (!hanaru::rate_limit::consume(10)) {
            SEND_ERROR(callback, k429TooManyRequests, "rate limit, please wait 1 second");
        }

        auto [beatmaps, status] = hanaru::download_beatmapset(id);
        auto response = HttpResponse::newHttpJsonResponse(beatmaps);
        response->setStatusCode(status);
        callback(response);
        return;
    }

    // Workaround for GCC
    typedef long long int64_t;
    Json::Value beatmaps = Json::arrayValue;

    for (auto row : result) {
        Json::Value beatmap;

        beatmap["beatmap_id"]       = row["beatmap_id"].as<int32_t>();
        beatmap["beatmapset_id"]    = row["beatmapset_id"].as<int32_t>();
        beatmap["beatmap_md5"]      = row["beatmap_md5"].as<std::string>();
        beatmap["artist"]           = row["artist"].as<std::string>();
        beatmap["title"]            = row["title"].as<std::string>();
        beatmap["version"]          = row["difficulty_name"].as<std::string>();
        beatmap["creator"]          = row["creator"].as<std::string>();
        beatmap["count_normal"]     = row["count_normal"].as<int32_t>();
        beatmap["count_slider"]     = row["count_slider"].as<int32_t>();
        beatmap["count_spinner"]    = row["count_spinner"].as<int32_t>();
        beatmap["max_combo"]        = row["max_combo"].as<int32_t>();
        beatmap["ranked_status"]    = row["ranked_status"].as<int32_t>();
        beatmap["creating_date"]    = hanaru::int_to_time(row["creating_date"].as<int64_t>());
        beatmap["bpm"]              = row["bpm"].as<int32_t>();
        beatmap["hit_length"]       = row["hit_length"].as<int32_t>();

        const int32_t mode = row["mode"].as<int32_t>();
        switch (mode) {
            default:
            case 0: {
                beatmap["difficulty"] = row["difficulty_std"].as<float>();
                break;
            }
            case 1: {
                beatmap["difficulty"] = row["difficulty_taiko"].as<float>();
                break;
            }
            case 2: {
                beatmap["difficulty"] = row["difficulty_ctb"].as<float>();
                break;
            }
            case 3: {
                beatmap["difficulty"] = row["difficulty_mania"].as<float>();
                break;
            }
        }

        beatmap["cs"] = row["cs"].as<float>();
        beatmap["ar"] = row["ar"].as<float>();
        beatmap["od"] = row["od"].as<float>();
        beatmap["hp"] = row["hp"].as<float>();
        beatmap["mode"] = mode;

        beatmaps.append(beatmap);
    }
    
    auto response = HttpResponse::newHttpJsonResponse(beatmaps);
    callback(response);
}
