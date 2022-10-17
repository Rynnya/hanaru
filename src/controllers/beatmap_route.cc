#include "beatmap_route.hh"

#include "../impl/utils.hh"
#include "../impl/downloader.hh"

void BeatmapRoute::get(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, int64_t id) {
    if (!hanaru::verifyRateLimit(1)) {
        HttpResponsePtr response = HttpResponse::newHttpResponse();
        response->setStatusCode(k429TooManyRequests);
        response->setBody("rate limit, please try again");
        response->setContentTypeCode(drogon::CT_TEXT_PLAIN);
        callback(response);
        return;
    }

    drogon::orm::DbClientPtr db = app().getDbClient();
    db->execSqlAsync(
        "SELECT * FROM beatmaps WHERE beatmap_id = ? LIMIT 1;",
        [id, callback](const drogon::orm::Result& result) mutable {
            Json::Value beatmap = Json::objectValue;

            if (result.empty()) {
                if (!hanaru::verifyRateLimit(10)) {
                    HttpResponsePtr response = HttpResponse::newHttpResponse();
                    response->setStatusCode(k429TooManyRequests);
                    response->setBody("rate limit, please wait 1 second");
                    response->setContentTypeCode(drogon::CT_TEXT_PLAIN);
                    callback(response);
                    return;
                }

                hanaru::downloader::downloadBeatmap(id, [callback = std::move(callback)](const std::tuple<Json::Value, HttpStatusCode>& result) {
                    HttpResponsePtr response = HttpResponse::newHttpJsonResponse(std::get<Json::Value>(result));
                    response->setStatusCode(std::get<HttpStatusCode>(result));
                    callback(response);
                });

                return;
            }

            // Workaround for GCC
            typedef long long int64_t;

            const auto& row = result.front();
            beatmap["beatmap_id"] = row["beatmap_id"].as<int32_t>();
            beatmap["beatmapset_id"] = row["beatmapset_id"].as<int32_t>();
            beatmap["beatmap_md5"] = row["beatmap_md5"].as<std::string>();
            beatmap["artist"] = row["artist"].as<std::string>();
            beatmap["title"] = row["title"].as<std::string>();
            beatmap["version"] = row["difficulty_name"].as<std::string>();
            beatmap["creator"] = row["creator"].as<std::string>();
            beatmap["count_normal"] = row["count_normal"].as<int32_t>();
            beatmap["count_slider"] = row["count_slider"].as<int32_t>();
            beatmap["count_spinner"] = row["count_spinner"].as<int32_t>();
            beatmap["max_combo"] = row["max_combo"].as<int32_t>();
            beatmap["ranked_status"] = row["ranked_status"].as<int32_t>();
            beatmap["latest_update"] = hanaru::timeToString(row["latest_update"].as<int64_t>());
            beatmap["bpm"] = row["bpm"].as<int32_t>();
            beatmap["hit_length"] = row["hit_length"].as<int32_t>();

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

            callback(HttpResponse::newHttpJsonResponse(std::move(beatmap)));
        }, 
        [callback](const drogon::orm::DrogonDbException&) {
            HttpResponsePtr response = HttpResponse::newHttpResponse();
            response->setStatusCode(k500InternalServerError);
            response->setBody("something went wrong, please report me!");
            response->setContentTypeCode(drogon::CT_TEXT_PLAIN);
            callback(response);
        }, id
    );
}