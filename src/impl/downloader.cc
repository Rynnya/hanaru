#include "downloader.hh"

#include "authorization.hh"
#include "utils.hh"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>

#include <fstream>
#include <iomanip>

#include "../thirdparty/curler.hh"

namespace detail {

    std::string username_ {};
    std::string password_ {};
    std::string apiKey_ {};
    std::string xsrfToken_ {};
    std::string sessionToken_ {};
    std::atomic_bool valid_ { false };

    curl::Factory factory_ {};
    curl::Builder authBuilder_ = factory_.createRequest("https://osu.ppy.sh");
    std::mutex reAuthMutex_ {};

    void auth() {
        if (valid_) {
            return;
        }

        authBuilder_
            .setPath("/home")
            .setUserAgent(HANARU_USER_AGENT)
            .setRequestType(curl::RequestType::GET);
        curl::Response mainPageResponse = factory_.syncRequest(authBuilder_);

        if (mainPageResponse.code != curl::StatusCode::Values::OK) {
            return;
        }

        for (const auto& [_, cookie] : mainPageResponse.cookies) {
            authBuilder_.addCookie(cookie.key, cookie.value);

            if (cookie.key == "XSRF-TOKEN") {
                xsrfToken_ = cookie.value;
            }
        }

        authBuilder_
            .setPath("/session")
            .setReferer("https://osu.ppy.sh/home")
            .addHeader("Origin", "https://osu.ppy.sh")
            .addHeader("Alt-Used", "osu.ppy.sh")
            .addHeader("Content-Type", "application/x-www-form-urlencoded; charset=UTF-8")
            .addHeader("X-CSRF-Token", xsrfToken_)
            .setRequestType(curl::RequestType::POST)
            .setBody(
                "_token=" + curl::Utils::urlEncode(xsrfToken_) +
                "&username=" + curl::Utils::urlEncode(username_) +
                "&password=" + curl::Utils::urlEncode(password_)
            );

        curl::Response authResponse = factory_.syncRequest(authBuilder_);

        if (authResponse.code != curl::StatusCode::Values::OK) {
            return;
        }

        authBuilder_.resetCookies();
        authBuilder_.resetHeaders();

        for (const auto& [_, cookie] : authResponse.cookies) {
            if (cookie.domain == ".ppy.sh") {
                if (cookie.key == "XSRF-TOKEN") {
                    xsrfToken_ = cookie.value;
                }

                if (cookie.key == "osu_session") {
                    sessionToken_ = cookie.value;
                }
            }
        }

        valid_ = true;
    }

    void deAuth() {
        if (!valid_ || sessionToken_.empty() || xsrfToken_.empty()) {
            return;
        }

        authBuilder_
            .setPath("/session")
            .setReferer("https://osu.ppy.sh")
            .addHeader("Origin", "https://osu.ppy.sh")
            .addHeader("Alt-Used", "osu.ppy.sh")
            .addHeader("X-CSRF-Token", xsrfToken_)
            .addCookie("XSRF-TOKEN", xsrfToken_)
            .addCookie("osu_session", sessionToken_)
            .setRequestType(curl::RequestType::DELETE);
        static_cast<void>(factory_.syncRequest(authBuilder_));

        xsrfToken_.clear();
        sessionToken_.clear();
        valid_ = false;
    }

}

namespace hanaru {

    void downloader::initialize(const std::string& apiKey, const std::string& username, const std::string& password) {
        detail::apiKey_ = apiKey;
        detail::username_ = username;
        detail::password_ = password;

        detail::auth();
    }
    
    void downloader::downloadBeatmap(int64_t id, std::function<void(std::tuple<Json::Value, drogon::HttpStatusCode>&&)>&& callback) {
        if (detail::apiKey_.empty()) {
            callback({ Json::objectValue, drogon::k404NotFound });
            return;
        }

        drogon::HttpRequestPtr request = drogon::HttpRequest::newHttpRequest();
        drogon::HttpClientPtr client = drogon::HttpClient::newHttpClient("https://osu.ppy.sh");

        request->setPath("/api/get_beatmaps");
        request->setParameter("k", detail::apiKey_);
        request->setParameter("b", std::to_string(id));

        client->sendRequest(request, [callback = std::move(callback)](drogon::ReqResult result, const drogon::HttpResponsePtr& response) {
            if (result != drogon::ReqResult::Ok) {
                callback({ Json::objectValue, drogon::k404NotFound });
                return;
            }

            const std::shared_ptr<Json::Value>& jsonResponse = response->getJsonObject();
            if (jsonResponse == nullptr) {
                callback({ Json::objectValue, drogon::k404NotFound });
                return;
            }

            Json::Value map = serializeBeatmap((*jsonResponse)[0]);
            if (map["beatmap_id"].isNumeric()) {
                callback({ map, drogon::k200OK });
                return;
            }

            callback({ Json::objectValue, drogon::k404NotFound });
        });
    }

    void downloader::downloadBeatmapset(int64_t id, std::function<void(std::tuple<Json::Value, drogon::HttpStatusCode>&&)>&& callback) {
        if (detail::apiKey_.empty()) {
            callback({ Json::objectValue, drogon::k404NotFound });
            return;
        }

        drogon::HttpRequestPtr request = drogon::HttpRequest::newHttpRequest();
        drogon::HttpClientPtr client = drogon::HttpClient::newHttpClient("https://osu.ppy.sh");

        request->setPath("/api/get_beatmaps");
        request->setParameter("k", detail::apiKey_);
        request->setParameter("s", std::to_string(id));

        client->sendRequest(request, [callback = std::move(callback)](drogon::ReqResult result, const drogon::HttpResponsePtr& response) {
            if (result != drogon::ReqResult::Ok) {
                callback({ Json::objectValue, drogon::k404NotFound });
                return;
            }

            const std::shared_ptr<Json::Value>& jsonResponse = response->getJsonObject();
            if (jsonResponse == nullptr) {
                callback({ Json::objectValue, drogon::k404NotFound });
                return;
            }

            Json::Value beatmaps = Json::arrayValue;
            for (const Json::Value& row : *jsonResponse) {
                Json::Value map = serializeBeatmap(row);

                if (map["beatmap_id"].isNumeric()) {
                    beatmaps.append(map);
                }
            }

            callback({ beatmaps, drogon::k200OK });
        });
    }

    void downloader::downloadMap(int64_t id, std::function<void(std::tuple<drogon::HttpStatusCode, std::string, std::string>&&)>&& callback) {
        if (!verifyRateLimit(1)) {
            callback({ drogon::k429TooManyRequests, "", "rate limit, please try again" });
            return;
        }

        if (const auto sBeatmap = storage::find(id)) {
            callback({ drogon::k200OK, sBeatmap->name(), sBeatmap->content() });
            return;
        }

        if (!verifyRateLimit(20)) {
            callback({ drogon::k429TooManyRequests, "", "rate limit, please wait 2 seconds" });
            return;
        }

        drogon::orm::DbClientPtr db = drogon::app().getDbClient();
        std::string idAsString = std::to_string(id);
        const std::filesystem::path beatmapPath = hanaru::storage::getBeatmapsPath() / idAsString;

        // Trying to find beatmap on disk
        if (std::filesystem::exists(beatmapPath)) {
            std::ifstream beatmapFile { beatmapPath, std::ios::binary };

            std::string contents {};
            beatmapFile.seekg(0, std::ios::end);
            contents.resize(beatmapFile.tellg());
            beatmapFile.seekg(0, std::ios::beg);
            beatmapFile.read(contents.data(), contents.size());
            beatmapFile.close();

            if (contents.empty()) {
                callback({ drogon::k404NotFound, "", "beatmapset doesn't exist on osu! servers or this beatmapset was banned" });
                return;
            }

            db->execSqlAsync("SELECT name FROM beatmaps_names WHERE id = ? LIMIT 1;",
                [id, idAsString_ = std::move(idAsString), contents_ = std::move(contents), callback](const drogon::orm::Result& result) mutable {
                    std::string filename = idAsString_ + ".osz";

                    if (!result.empty()) {
                        const auto& row = result.front();
                        filename = row["name"].as<std::string>();
                    }

                    const auto sBeatmap = storage::insert(id, std::move(filename), std::move(contents_));
                    callback({ drogon::k200OK, sBeatmap->name(), sBeatmap->content() });
                },
                [callback](const drogon::orm::DrogonDbException&) { callback({}); }, id
            );
            return;
        }

        if (!detail::valid_) {
            callback({ drogon::k423Locked, "", "downloading disabled" });
            return;
        }

        if (!verifyRateLimit(40)) {
            callback({ drogon::k429TooManyRequests, "", "rate limit, please wait 6 seconds" });
            return;
        }

        const std::string beatmapsetId = std::to_string(id);
        curl::Builder builder = detail::factory_.createRequest("https://osu.ppy.sh");
        builder
            .setPath("/beatmapsets/" + beatmapsetId + "/download")
            .setParameter("noVideo", "1")
            .addHeader("Alt-Used", "osu.ppy.sh")
            .addHeader("Connection", "keep-alive")
            .addHeader("X-CSRF-Token", detail::xsrfToken_)
            .addCookie("XSRF-TOKEN", detail::xsrfToken_)
            .addCookie("osu_session", detail::sessionToken_)
            .setUserAgent(HANARU_USER_AGENT)
            .setReferer("https://osu.ppy.sh/beatmapsets/" + beatmapsetId)
            .onError([callback](curl::Response& r) {
                callback({ drogon::k500InternalServerError, "", r.error });
            })
            .onComplete([id, beatmapPath, callback](curl::Response& r) {
                switch (r.code) {
                    case curl::StatusCode::Values::Forbidden:
                    case curl::StatusCode::Values::Unauthorized: {
                        callback({ drogon::k401Unauthorized, "", "our downloader become unauthorized, please try again later" });

                        std::unique_lock<std::mutex> lock { detail::reAuthMutex_, std::try_to_lock };

                        if (lock.owns_lock()) {
                            detail::deAuth();
                            detail::auth();
                        }

                        return;
                    }
                    case curl::StatusCode::Values::NotFound: {
                        std::ofstream beatmapFile;
                        beatmapFile.open(beatmapPath);
                        beatmapFile.close();

                        callback({ drogon::k404NotFound, "", "beatmapset doesn't exist on osu! servers or this beatmapset was banned" });
                        return;
                    }
                    case curl::StatusCode::Values::TooManyRequests: {
                        callback({ drogon::k429TooManyRequests, "", "downloader was limited by osu! system, please wait 15 minutes before retrying" });
                        return;
                    }
                    case curl::StatusCode::Values::OK: {
                        if (r.body.empty() || r.body.find("PK\x03\x04") != 0) {
                            LOG_WARN << "Response was not valid osz file: " << (r.body.size() > 100 ? r.body.substr(0, 100) : r.body);
                            callback({ drogon::k422UnprocessableEntity, "", "response from osu! wasn't valid osz file" });
                            return;
                        }

                        const auto range = r.cookies.equal_range("xsrf-token");
                        for (auto it = range.first; it != range.second; it++) {
                            if (it->second.value.size() == detail::xsrfToken_.size() && it->second.domain == ".ppy.sh") {
                                detail::xsrfToken_ = it->second.value;
                                break;
                            }
                        }

                        const auto sBeatmap = storage::insert(id, getFilenameFromLink(r.headers), std::move(r.body));

                        if (storage::canWrite()) {
                            saveBeatmapToDB(id, sBeatmap->name());
                            if (r.saveToFile(beatmapPath.generic_string())) {
                                storage::decreaseAvailableSpace(sBeatmap->size());
                            }
                        }

                        callback({ drogon::k200OK, sBeatmap->name(), sBeatmap->content() });
                        return;
                    }
                    default: {
                        callback({ drogon::k503ServiceUnavailable, "", "response from osu! wasn't valid" });
                        return;
                    }
                }
            });

        detail::factory_.pushRequest(builder);
    }
}

Json::Value hanaru::downloader::serializeBeatmap(const Json::Value& json) {
    if (json.isNull()) {
        return {};
    }

    std::string approved = "0";
    std::string maxCombo = "0";
    drogon::orm::DbClientPtr db = drogon::app().getDbClient();

    if (json["max_combo"].isString()) {
        maxCombo = json["max_combo"].asString();
    }

    if (json["approved_date"].isString()) {
        approved = json["approved"].asString();
    }

    const int32_t mode = std::stoi(json["mode"].asString());

    std::string std = "0";
    std::string taiko = "0";
    std::string ctb = "0";
    std::string mania = "0";

    switch (mode) {
        case 0:
        default: {
            std = json["difficultyrating"].asString();
            break;
        }
        case 1: {
            taiko = json["difficultyrating"].asString();
            break;
        }
        case 2: {
            ctb = json["difficultyrating"].asString();
            break;
        }
        case 3: {
            mania = json["difficultyrating"].asString();
            break;
        }
    }

    // This is way safer to firstly apply beatmap to database, so we won't lose it if exception happends when casting strings
    db->execSqlAsync(
        "INSERT IGNORE INTO beatmaps (beatmap_id, beatmapset_id, beatmap_md5, mode, "
        "artist, title, difficulty_name, creator, "
        "count_normal, count_slider, count_spinner, max_combo, "
        "ranked_status, creating_date, bpm, hit_length, "
        "cs, ar, od, hp, "
        "difficulty_std, difficulty_taiko, difficulty_ctb, difficulty_mania) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
        [](const drogon::orm::Result&) {},
        [](const drogon::orm::DrogonDbException&) {},
        json["beatmap_id"].asString(), json["beatmapset_id"].asString(), json["file_md5"].asString(), json["mode"].asString(),
        json["artist"].asString(), json["title"].asString(), json["version"].asString(), json["creator"].asString(),
        json["count_normal"].asString(), json["count_slider"].asString(), json["count_spinner"].asString(), maxCombo,
        approved, hanaru::stringToTime(json["last_update"]), json["bpm"].asString(), json["hit_length"].asString(),
        json["diff_size"].asString(), json["diff_approach"].asString(), json["diff_overall"].asString(), json["diff_drain"].asString(),
        std, taiko, ctb, mania
    );

    try {
        Json::Value beatmap;

        beatmap["beatmap_id"]       = std::stoi(json["beatmap_id"].asString());
        beatmap["beatmapset_id"]    = std::stoi(json["beatmapset_id"].asString());
        beatmap["beatmap_md5"]      = json["file_md5"].asString();
        beatmap["artist"]           = json["artist"].asString();
        beatmap["title"]            = json["title"].asString();
        beatmap["version"]          = json["version"].asString();
        beatmap["creator"]          = json["creator"].asString();
        beatmap["count_normal"]     = std::stoi(json["count_normal"].asString());
        beatmap["count_slider"]     = std::stoi(json["count_slider"].asString());
        beatmap["count_spinner"]    = std::stoi(json["count_spinner"].asString());
        beatmap["max_combo"]        = std::stoi(maxCombo);
        beatmap["ranked_status"]    = std::stoi(json["approved"].asString());
        beatmap["latest_update"]    = json["last_update"].asString();
        beatmap["bpm"]              = std::stoi(json["bpm"].asString());
        beatmap["hit_length"]       = std::stoi(json["hit_length"].asString());

        beatmap["difficulty"] = std::stof(json["difficultyrating"].asString());

        beatmap["cs"] = std::stof(json["diff_size"].asString());
        beatmap["ar"] = std::stof(json["diff_approach"].asString());
        beatmap["od"] = std::stof(json["diff_overall"].asString());
        beatmap["hp"] = std::stof(json["diff_drain"].asString());
        beatmap["mode"] = mode;

        return beatmap;
    }
    catch (const std::exception& ex) {
        LOG_WARN << ex.what();
        return {};
    }
}

std::string hanaru::downloader::getFilenameFromLink(const std::unordered_multimap<std::string, std::string>& headers) {
    const std::string link = headers.equal_range("location").first->second;

    std::string filename = link.substr(link.find("fs=") + 3);
    filename = filename.substr(0, filename.find(".osz") + 4);

    return drogon::utils::urlDecode(filename);
}

void hanaru::downloader::saveBeatmapToDB(int64_t id, const std::string& filename) {
    drogon::orm::DbClientPtr db = drogon::app().getDbClient();

    db->execSqlAsync("INSERT INTO beatmaps_names (id, name) VALUES (?, ?);",
        [](const drogon::orm::Result&) {},
        [](const drogon::orm::DrogonDbException&) {},
        id, filename
    );
}