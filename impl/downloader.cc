#include "downloader.h"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>

#include <iomanip>

const char* insert_query = 
    "INSERT INTO beatmaps (beatmap_id, beatmapset_id, beatmap_md5, mode, "
    "artist, title, difficulty_name, creator, "
    "count_normal, count_slider, count_spinner, max_combo, "
    "ranked_status, creating_date, bpm, hit_length, "
    "cs, ar, od, hp, "
    "difficulty_std, difficulty_taiko, difficulty_ctb, difficulty_mania) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

std::string osu_key = "";

void hanaru::initialize_client() {
    Json::Value custom_cfg = drogon::app().getCustomConfig();
    if (custom_cfg["osu_api_key"].isString()) {
        osu_key = custom_cfg["osu_api_key"].asString();
    }
}

std::tuple<Json::Value, drogon::HttpStatusCode> hanaru::download_beatmap(int32_t id) {
    std::tuple<Json::Value, drogon::HttpStatusCode> result = { Json::objectValue, drogon::k404NotFound };
    if (osu_key.empty()) {
        return result;
    }

    drogon::HttpClientPtr client = drogon::HttpClient::newHttpClient("https://osu.ppy.sh");
    client->setUserAgent("hanaru/0.1");

    drogon::HttpRequestPtr request = drogon::HttpRequest::newHttpRequest();
    request->setPath("/api/get_beatmaps");
    request->setParameter("k", osu_key);
    request->setParameter("b", std::to_string(id));

    auto db = drogon::app().getDbClient();
    auto [res, response] = client->sendRequest(request);

    if (res == drogon::ReqResult::Ok) {
        Json::CharReaderBuilder builder;
        Json::Value row;
        builder["collectComments"] = false;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());

        std::string_view body = response->getBody();
        std::string errs;
        if (reader->parse(body.data(), body.data() + body.size(), &row, &errs)) {
            std::string approved = "0";
            std::string max_combo = "0";
            if (row["max_combo"].isString()) {
                max_combo = row["max_combo"].asString();
            }
            if (row["approved_date"].isString()) {
                approved = row["approved"].asString();
            }

            const int32_t mode = std::stoi(row["mode"].asString());
            std::string std = "0";
            std::string taiko = "0";
            std::string ctb = "0";
            std::string mania = "0";

            switch (mode) {
                case 0:
                default: {
                    std = row["difficultyrating"].asString();
                }
                case 1: {
                    taiko = row["difficultyrating"].asString();
                }
                case 2: {
                    ctb = row["difficultyrating"].asString();
                }
                case 3: {
                    mania = row["difficultyrating"].asString();
                }
            }

            // This is way safer to firstly apply beatmap to database, so we won't lose it if exception happends when casting strings
            db->execSqlAsync(
                insert_query,
                [](const drogon::orm::Result&) {},
                [](const drogon::orm::DrogonDbException&) {},
                row["beatmap_id"].asString(), row["beatmapset_id"].asString(), row["file_md5"].asString(), row["mode"].asString(),
                row["artist"].asString(), row["title"].asString(), row["version"].asString(), row["creator"].asString(),
                row["count_normal"].asString(), row["count_slider"].asString(), row["count_spinner"].asString(), row["max_combo"].asString(),
                approved, time_to_int(row["last_update"]), row["bpm"].asString(), row["hit_length"].asString(),
                row["diff_size"].asString(), row["diff_approach"].asString(), row["diff_overall"].asString(), row["diff_drain"].asString(),
                std, taiko, ctb, mania
            );

            Json::Value beatmap;

            try {
                beatmap["beatmap_id"]       = std::stoi(row["beatmap_id"].asString());
                beatmap["beatmapset_id"]    = std::stoi(row["beatmapset_id"].asString());
                beatmap["beatmap_md5"]      = row["beatmap_md5"].asString();
                beatmap["artist"]           = row["artist"].asString();
                beatmap["title"]            = row["title"].asString();
                beatmap["version"]          = row["difficulty_name"].asString();
                beatmap["creator"]          = row["creator"].asString();
                beatmap["count_normal"]     = std::stoi(row["count_normal"].asString());
                beatmap["count_slider"]     = std::stoi(row["count_slider"].asString());
                beatmap["count_spinner"]    = std::stoi(row["count_spinner"].asString());
                beatmap["max_combo"]        = std::stoi(row["max_combo"].asString());
                beatmap["ranked_status"]    = std::stoi(row["approved"].asString());
                beatmap["creating_date"]    = row["last_update"].asString();
                beatmap["bpm"]              = std::stoi(row["bpm"].asString());
                beatmap["hit_length"]       = std::stoi(row["hit_length"].asString());

                beatmap["difficulty"] = std::stof(row["difficultyrating"].asString());

                beatmap["cs"] = std::stof(row["diff_size"].asString());
                beatmap["ar"] = std::stof(row["diff_approach"].asString());
                beatmap["od"] = std::stof(row["diff_overall"].asString());
                beatmap["hp"] = std::stof(row["diff_drain"].asString());
                beatmap["mode"] = mode;

                return { beatmap, drogon::k200OK };
            }
            catch (const std::exception& ex) {
                LOG_WARN << ex.what();
            }
        }
    }

    return result;
}

std::tuple<Json::Value, drogon::HttpStatusCode> hanaru::download_beatmapset(int32_t id) {
    std::tuple<Json::Value, drogon::HttpStatusCode> result = { Json::arrayValue, drogon::k404NotFound };
    if (osu_key.empty()) {
        return result;
    }

    drogon::HttpClientPtr client = drogon::HttpClient::newHttpClient("https://osu.ppy.sh");
    client->setUserAgent("hanaru/0.1");

    drogon::HttpRequestPtr request = drogon::HttpRequest::newHttpRequest();
    request->setPath("/api/get_beatmaps");
    request->setParameter("k", osu_key);
    request->setParameter("s", std::to_string(id));

    auto db = drogon::app().getDbClient();
    Json::Value beatmaps = Json::arrayValue;

    auto [res, response] = client->sendRequest(request);
    if (res == drogon::ReqResult::Ok) {
        const std::shared_ptr<Json::Value>& json_response = response->getJsonObject();
        if (json_response != nullptr) {
            for (auto row : *json_response) {
                std::string approved = "0";
                std::string max_combo = "0";
                if (row["max_combo"].isString()) {
                    max_combo = row["max_combo"].asString();
                }
                if (row["approved_date"].isString()) {
                    approved = row["approved"].asString();
                }

                const int32_t mode = std::stoi(row["mode"].asString());
                std::string std = "0";
                std::string taiko = "0";
                std::string ctb = "0";
                std::string mania = "0";

                switch (mode) {
                    case 0:
                    default: {
                        std = row["difficultyrating"].asString();
                    }
                    case 1: {
                        taiko = row["difficultyrating"].asString();
                    }
                    case 2: {
                        ctb = row["difficultyrating"].asString();
                    }
                    case 3: {
                        mania = row["difficultyrating"].asString();
                    }
                }

                // This is way safer to firstly apply beatmap to database, so we won't lose it if exception happends when casting strings
                db->execSqlAsync(
                    insert_query,
                    [](const drogon::orm::Result&) {},
                    [](const drogon::orm::DrogonDbException&) {},
                    row["beatmap_id"].asString(), row["beatmapset_id"].asString(), row["file_md5"].asString(), row["mode"].asString(),
                    row["artist"].asString(), row["title"].asString(), row["version"].asString(), row["creator"].asString(),
                    row["count_normal"].asString(), row["count_slider"].asString(), row["count_spinner"].asString(), row["max_combo"].asString(),
                    approved, time_to_int(row["last_update"]), row["bpm"].asString(), row["hit_length"].asString(),
                    row["diff_size"].asString(), row["diff_approach"].asString(), row["diff_overall"].asString(), row["diff_drain"].asString(),
                    std, taiko, ctb, mania
                );

                Json::Value beatmap;

                try {
                    beatmap["beatmap_id"]       = std::stoi(row["beatmap_id"].asString());
                    beatmap["beatmapset_id"]    = std::stoi(row["beatmapset_id"].asString());
                    beatmap["beatmap_md5"]      = row["beatmap_md5"].asString();
                    beatmap["artist"]           = row["artist"].asString();
                    beatmap["title"]            = row["title"].asString();
                    beatmap["version"]          = row["difficulty_name"].asString();
                    beatmap["creator"]          = row["creator"].asString();
                    beatmap["count_normal"]     = std::stoi(row["count_normal"].asString());
                    beatmap["count_slider"]     = std::stoi(row["count_slider"].asString());
                    beatmap["count_spinner"]    = std::stoi(row["count_spinner"].asString());
                    beatmap["max_combo"]        = std::stoi(row["max_combo"].asString());
                    beatmap["ranked_status"]    = std::stoi(row["approved"].asString());
                    beatmap["creating_date"]    = row["last_update"].asString();
                    beatmap["bpm"]              = std::stoi(row["bpm"].asString());
                    beatmap["hit_length"]       = std::stoi(row["hit_length"].asString());

                    beatmap["difficulty"] = std::stof(row["difficultyrating"].asString());

                    beatmap["cs"] = std::stof(row["diff_size"].asString());
                    beatmap["ar"] = std::stof(row["diff_approach"].asString());
                    beatmap["od"] = std::stof(row["diff_overall"].asString());
                    beatmap["hp"] = std::stof(row["diff_drain"].asString());
                    beatmap["mode"] = mode;

                    beatmaps.append(beatmap);
                }
                catch (const std::exception& ex) {
                    LOG_WARN << ex.what();
                }
            }

            return { beatmaps, drogon::k200OK };
        }
        else {
            LOG_WARN << response->getJsonError();
        }
    }

    return result;
}

int64_t hanaru::time_to_int(Json::Value _time) {
    if (!_time.isString()) {
        return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()).time_since_epoch().count();
    }
    std::string __time = _time.asString();
    std::tm time {};
    std::stringstream stream(__time);
    stream >> std::get_time(&time, "%Y-%m-%d %H:%M:%S");
    std::chrono::time_point time_point = std::chrono::system_clock::from_time_t(std::mktime(&time));
    std::chrono::seconds seconds = std::chrono::time_point_cast<std::chrono::seconds>(time_point).time_since_epoch();
    return seconds.count();
}

std::string hanaru::int_to_time(int64_t _time) {
    std::tm time = *std::gmtime(&_time);
    std::stringstream ss;
    ss << std::put_time(&time, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}