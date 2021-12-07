#include "downloader.hh"

#include "globals.hh"
#include "rate_limiter.hh"
#include "storage_manager.hh"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>

#include <fstream>
#include <iomanip>

hanaru::downloader::downloader(const std::string& _username, const std::string& _password, const std::string& _api_key)
    : username(_username)
    , password(_password)
    , api_key(_api_key)
{
    b_client->setUserAgent("hanaru/" HANARU_VERSION);
    s_client->setUserAgent("hanaru/" HANARU_VERSION);
    d_client->setUserAgent("hanaru/" HANARU_VERSION);
    d_client->enableCookies();
    instance = this;

    if (username.empty() || password.empty()) {
        LOG_WARN << "downloading disabled due empty login and password";
        return;
    }

    std::thread([&] {
        drogon::HttpClientPtr login_client = drogon::HttpClient::newHttpClient("https://old.ppy.sh");
        login_client->enableCookies();
        login_client->setUserAgent("hanaru/" HANARU_VERSION);

        drogon::HttpRequestPtr login_request = drogon::HttpRequest::newHttpFormPostRequest();
        login_request->setPath("/forum/ucp.php?mode=login");

        login_request->addHeader("Origin", "https://old.ppy.sh");
        login_request->addHeader("Referer", "https://old.ppy.sh/forum/ucp.php?mode=login");
        login_request->addHeader("Alt-Used", "old.ppy.sh");

        login_request->setParameter("redirect", "/");
        login_request->setParameter("sid", "");
        login_request->setParameter("username", username);
        login_request->setParameter("password", password);
        login_request->setParameter("autologin", "on");
        login_request->setParameter("login", "Login");

        auto [__, response] = login_client->sendRequest(login_request);
        const std::string& location = response->getHeader("Location");

        if (location.find("success") == std::string::npos) {
            LOG_WARN << "invalid location header: " << location;
            instance = this;
            return;
        }

        for (auto cookie : response->getCookies()) {
            if (cookie.first == "Location") {
                continue;
            }

            d_client->addCookie(cookie.second);
        }

        downloading_enabled = true;
        instance = this;
    }).detach();
}

drogon::Task<std::tuple<Json::Value, drogon::HttpStatusCode>> hanaru::downloader::download_beatmap(int32_t id) const {
    if (api_key.empty()) {
        co_return not_found;
    }

    drogon::HttpRequestPtr request = drogon::HttpRequest::newHttpRequest();
    request->setPath("/api/get_beatmaps");
    request->setParameter("k", api_key);
    request->setParameter("b", std::to_string(id));

    auto response = co_await b_client->sendRequestCoro(request);
    const std::shared_ptr<Json::Value>& json_response = response->getJsonObject();

    if (json_response != nullptr) {
        Json::Value resp = *json_response;
        Json::Value map = serialize_beatmap(*resp.begin());

        if (map["beatmap_id"].isNumeric()) {
            co_return { map, drogon::k200OK };
        }
    }

    co_return not_found;
}

drogon::Task<std::tuple<Json::Value, drogon::HttpStatusCode>> hanaru::downloader::download_beatmapset(int32_t id) const {
    if (api_key.empty()) {
        co_return not_found;
    }

    drogon::HttpRequestPtr request = drogon::HttpRequest::newHttpRequest();
    request->setPath("/api/get_beatmaps");
    request->setParameter("k", api_key);
    request->setParameter("s", std::to_string(id));

    Json::Value beatmaps = Json::arrayValue;
    auto response = co_await s_client->sendRequestCoro(request);
    const std::shared_ptr<Json::Value>& json_response = response->getJsonObject();

    if (json_response != nullptr) {
        for (auto row : *json_response) {
            Json::Value map = serialize_beatmap(row);

            if (map["beatmap_id"].isNumeric()) {
                beatmaps.append(map);
            }
        }

        co_return { beatmaps, drogon::k200OK };
    }

    co_return not_found;
}

drogon::Task<std::tuple<drogon::HttpStatusCode, std::string, std::string>> hanaru::downloader::download_map(int32_t id) const {
    if (!hanaru::rate_limit::consume(1)) {
        co_return { drogon::k429TooManyRequests, "", "rate limit, please try again" };
    }

    drogon::orm::DbClientPtr db = drogon::app().getDbClient();
    const std::string id_as_string = std::to_string(id);

    {
        std::optional<hanaru::cached_beatmap> cached = hanaru::storage_manager::get()->find(id);
        if (cached.has_value()) {
            if (cached->content.size() == 0) {
                co_return { drogon::k404NotFound, "", "beatmapset doesn't exist on osu! servers" };
            }

            co_return { drogon::k200OK, cached->name, cached->content };
        }
    }

    const fs::path beatmap = beatmap_path / id_as_string;

    if (!hanaru::rate_limit::consume(20)) {
        co_return { drogon::k429TooManyRequests, "", "rate limit, please wait 2 seconds" };
    }

    // Trying to find on disk
    if (fs::exists(beatmap)) {
        std::ifstream beatmap_file(beatmap, std::ios::binary);

        std::string contents;
        beatmap_file.seekg(0, std::ios::end);
        contents.resize(beatmap_file.tellg());
        beatmap_file.seekg(0, std::ios::beg);
        beatmap_file.read(contents.data(), contents.size());
        beatmap_file.close();

        // Beatmap doesn't exist on server, but we cache it to reduce activity to osu! servers
        if (contents.empty()) {
            hanaru::storage_manager::get()->insert(id, hanaru::cached_beatmap("", empty.data(), empty.size()));
            co_return { drogon::k404NotFound, "", "beatmapset doesn't exist on osu! servers" };
        }

        std::string filename = id_as_string + ".osz";
        const auto& result = co_await db->execSqlCoro("SELECT name FROM beatmaps_names WHERE id = ? LIMIT 1;", id);
        if (!result.empty()) {
            const auto& row = result.front();
            filename = row["name"].as<std::string>();
        }

        hanaru::storage_manager::get()->insert(id, hanaru::cached_beatmap(filename, contents.data(), contents.size()));
        co_return { drogon::k200OK, filename, contents };
    }

    if (!downloading_enabled) {
        co_return { drogon::k423Locked, "", "downloading disabled" };
    }

    while (downloading_queue.find(id) != downloading_queue.end()) {
        std::optional<hanaru::cached_beatmap> cached = hanaru::storage_manager::get()->find(id);
        if (cached.has_value()) {
            if (cached->content.size() == 0) {
                co_return { drogon::k404NotFound, "", "beatmapset doesn't exist on osu! servers" };
            }

            co_return { drogon::k200OK, cached->name, cached->content };
        }

        trantor::EventLoop* loop = trantor::EventLoop::getEventLoopOfCurrentThread();
        if (loop == nullptr) {
            loop = drogon::app().getIOLoop(1);
        }
        
        co_await drogon::sleepCoro(loop, std::chrono::milliseconds(500));
    }

    if (!hanaru::rate_limit::consume(40)) {
        co_return { drogon::k429TooManyRequests, "", "rate limit, please wait 6 seconds" };
    }

    // Should we use lock here? Actually no, until we not under DDOS or something like this
    // Even under DDOS we just hit rate limit, which will stop the whole process
    downloading_queue.insert(id);

    // Beatmapset wasn't found anywhere, so we download it
    drogon::HttpRequestPtr request = drogon::HttpRequest::newHttpRequest();
    request->setPath("/beatmapsets/" + id_as_string + "/download");
    request->setParameter("noVideo", "1");

    // Additional headers to properly redirect us to beatmap file
    request->addHeader("Alt-Used", "osu.ppy.sh");
    request->addHeader("Connection", "keep-alive");
    request->addHeader("Origin", "https://osu.ppy.sh");
    request->addHeader("Referer", "https://osu.ppy.sh/beatmapsets/" + id_as_string);
    request->addHeader("TE", "trailers");

    // Sadly, but HttpClient cannot do redirects, so we do it manually
    try {
        auto location_response = co_await d_client->sendRequestCoro(request, 10);

        // If beatmap doesn't exist, then Location header will be empty
        const std::string& location = location_response->getHeader("Location");
        if (location.empty()) {
            // This will create empty file so we doesn't need to always ask peppy about this map
            // Some new maps might be added, so we should only apply this on old maps
            if (id < 1300000) {
                std::ofstream beatmap_file;
                beatmap_file.open(beatmap);
                beatmap_file.close();

                hanaru::storage_manager::get()->insert(id, hanaru::cached_beatmap("", empty.data(), empty.size()));
            }

            drogon::app().getIOLoop(1)->runAfter(1, [&]{ downloading_queue.erase(id); });
            co_return { drogon::k404NotFound, "", "beatmapset doesn't exist on osu! servers" };
        }

        auto [endpoint, query, filename] = this->split_download_link(location);

        drogon::HttpClientPtr download_client = drogon::HttpClient::newHttpClient(endpoint);
        drogon::HttpRequestPtr download_request = drogon::HttpRequest::newHttpRequest();
        download_request->setPath(query);

        auto beatmap_response = co_await download_client->sendRequestCoro(download_request);
        std::string_view view = beatmap_response->getBody();

        // Verify 'magic' value for response
        if (view.empty() || view.find("PK\x03\x04") != 0) {
            LOG_WARN << "Response was not valid osz file: " << (view.size() > 100 ? view.substr(0, 100) : view);
            drogon::app().getIOLoop(1)->runAfter(1, [&]{ downloading_queue.erase(id); });
            co_return { drogon::k422UnprocessableEntity, "", "response from osu! wasn't valid osz file" };
        }

        hanaru::storage_manager::get()->insert(id, hanaru::cached_beatmap(filename, view.data(), view.size()));

        if (hanaru::storage_manager::get()->can_write()) {
            db->execSqlAsync(
                "INSERT INTO beatmaps_names (id, name) VALUES (?, ?);",
                [](const drogon::orm::Result&) {},
                [](const drogon::orm::DrogonDbException&) {},
                id, filename
            );

            std::ofstream beatmap_file(beatmap, std::ios::binary);
            beatmap_file.write(view.data(), view.size());
            beatmap_file.close();
        }

        drogon::app().getIOLoop(1)->runAfter(1, [&]{ downloading_queue.erase(id); });
        co_return { drogon::k200OK, filename, std::string(view) };
    }
    catch (const std::exception& ex) {
        LOG_WARN << ex.what();
        drogon::app().getIOLoop(1)->runAfter(1, [&]{ downloading_queue.erase(id); });
        co_return { drogon::k503ServiceUnavailable, "", "osu! server didn't respond" };
    }
}

const hanaru::downloader* hanaru::downloader::get() {
    return instance;
}

Json::Value hanaru::downloader::serialize_beatmap(const Json::Value& json) const {
    std::string approved = "0";
    std::string max_combo = "0";
    drogon::orm::DbClientPtr db = drogon::app().getDbClient();

    if (json["max_combo"].isString()) {
        max_combo = json["max_combo"].asString();
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
        "INSERT INTO beatmaps (beatmap_id, beatmapset_id, beatmap_md5, mode, "
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
        json["count_normal"].asString(), json["count_slider"].asString(), json["count_spinner"].asString(), max_combo,
        approved, hanaru::time_to_int(json["last_update"]), json["bpm"].asString(), json["hit_length"].asString(),
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
        beatmap["max_combo"]        = std::stoi(max_combo);
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

std::tuple<std::string, std::string, std::string> hanaru::downloader::split_download_link(const std::string& link) const {
    size_t ptr = link.find("ppy.sh");
    std::string query = link.substr(ptr + 6);
    std::string filename = query.substr(query.find("fs=") + 3);
    filename = filename.substr(0, filename.find(".osz") + 4);

    return { std::string(link.begin(), link.begin() + ptr + 6), query, drogon::utils::urlDecode(filename) };
}