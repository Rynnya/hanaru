#include "downloader.hh"

#include "globals.hh"
#include "rate_limiter.hh"
#include "storage_manager.hh"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>

#include <fstream>
#include <iomanip>

#include "curl.hh"

namespace hanaru {

    static hanaru::downloader* instance = nullptr;
    static std::mutex auth_lock {};

    // Required to deauthrorize user if needed
    static std::string csrf_token {};
    static std::string osu_session {};

    drogon::Task<curl::response> send_request(const std::string& beatmapset_id) {
        curl::client download_client("https://osu.ppy.sh");

        download_client.set_path("/beatmapsets/" + beatmapset_id + "/download");
        download_client.set_parameter("noVideo", "1");

        download_client.add_header("Alt-Used", "osu.ppy.sh");
        download_client.add_header("Connection", "keep-alive");
        download_client.add_header("X-CSRF-Token", hanaru::csrf_token);
        download_client.set_referer("https://osu.ppy.sh/beatmapsets/" + beatmapset_id);

        download_client.set_user_agent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/89.0.4389.114 Safari/537.36");

        download_client.add_cookie("XSRF-TOKEN", hanaru::csrf_token);
        download_client.add_cookie("osu_session", hanaru::osu_session);

        co_return download_client.get();
    }

}

hanaru::downloader::downloader(const std::string& _username, const std::string& _password, const std::string& _api_key)
    : username(_username)
    , password(_password)
    , api_key(_api_key)
{
    b_client->setUserAgent("hanaru/" HANARU_VERSION);
    s_client->setUserAgent("hanaru/" HANARU_VERSION);
    instance = this;

    if (username.empty() || password.empty()) {
        LOG_WARN << "downloading disabled due empty login and password";
        return;
    }

    std::thread([&]() -> drogon::AsyncTask {
        this->authorize();
        if (!downloading_enabled) {
            co_return;
        }

        while (true) {
            auto [code, body, error] = co_await this->download_map(rand() / 1300000);
            // Request might be denied due to rate limit or beatmap simply doesn't exist on this ID
            if (code != drogon::k429TooManyRequests && code != drogon::k200OK && code != drogon::k404NotFound) {
                LOG_ERROR << "error happend when trying to verify downloader:";
                LOG_ERROR << body;
                LOG_ERROR << '\n';
                LOG_ERROR << error;

                std::this_thread::sleep_for(std::chrono::minutes(1));
                continue;
            }

            std::this_thread::sleep_for(std::chrono::days(7));
        }
    }).detach();
}

drogon::Task<std::tuple<Json::Value, drogon::HttpStatusCode>> hanaru::downloader::download_beatmap(int32_t id) {
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
        Json::Value map = serialize_beatmap(resp[0]);

        if (map["beatmap_id"].isNumeric()) {
            co_return { map, drogon::k200OK };
        }
    }

    co_return not_found;
}

drogon::Task<std::tuple<Json::Value, drogon::HttpStatusCode>> hanaru::downloader::download_beatmapset(int32_t id) {
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

drogon::Task<std::tuple<drogon::HttpStatusCode, std::string, std::string>> hanaru::downloader::download_map(int32_t id) {
    if (!hanaru::rate_limit::consume(1)) {
        co_return { drogon::k429TooManyRequests, "", "rate limit, please try again" };
    }

    drogon::orm::DbClientPtr db = drogon::app().getDbClient();
    const std::string id_as_string = std::to_string(id);

    {
        std::optional<hanaru::cached_beatmap> cached = hanaru::storage_manager::get()->find(id);
        if (cached.has_value()) {
            if (cached->content.size() == 0) {
                co_return { drogon::k404NotFound, "", "beatmapset doesn't exist on osu! servers or this beatmapset was banned" };
            }

            co_return { drogon::k200OK, cached->name, cached->content };
        }
    }

    const fs::path beatmap_path = beatmaps_folder_path / id_as_string;

    if (!hanaru::rate_limit::consume(20)) {
        co_return { drogon::k429TooManyRequests, "", "rate limit, please wait 2 seconds" };
    }

    // Trying to find on disk
    if (fs::exists(beatmap_path)) {
        std::ifstream beatmap_file(beatmap_path, std::ios::binary);

        std::string contents;
        beatmap_file.seekg(0, std::ios::end);
        contents.resize(beatmap_file.tellg());
        beatmap_file.seekg(0, std::ios::beg);
        beatmap_file.read(contents.data(), contents.size());
        beatmap_file.close();

        // Beatmap doesn't exist on server, but we cache it to reduce activity to osu! servers
        if (contents.empty()) {
            hanaru::storage_manager::get()->insert(id, { "", "" });
            co_return { drogon::k404NotFound, "", "beatmapset doesn't exist on osu! servers or this beatmapset was banned" };
        }

        std::string filename = id_as_string + ".osz";
        const auto& result = co_await db->execSqlCoro("SELECT name FROM beatmaps_names WHERE id = ? LIMIT 1;", id);
        if (!result.empty()) {
            const auto& row = result.front();
            filename = row["name"].as<std::string>();
        }

        hanaru::storage_manager::get()->insert(id, { filename, contents });
        co_return { drogon::k200OK, filename, contents };
    }

    if (!downloading_enabled) {
        co_return { drogon::k423Locked, "", "downloading disabled" };
    }

    while (downloading_queue.find(id) != downloading_queue.end()) {
        std::optional<hanaru::cached_beatmap> cached = hanaru::storage_manager::get()->find(id);
        if (cached.has_value()) {
            if (cached->content.size() == 0) {
                co_return { drogon::k404NotFound, "", "beatmapset doesn't exist on osu! servers or this beatmapset was banned" };
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
    curl::response beatmap_response = co_await send_request(id_as_string);

    if (beatmap_response.code == curl::status_code::values::not_found) {
        std::ofstream beatmap_file;
        beatmap_file.open(beatmap_path);
        beatmap_file.close();

        hanaru::storage_manager::get()->insert(id, { "", "" });

        drogon::app().getIOLoop(1)->runAfter(1, [&]{ downloading_queue.erase(id); });
        co_return { drogon::k404NotFound, "", "beatmapset doesn't exist on osu! servers or this beatmapset was banned" };
    }

    if (beatmap_response.code == curl::status_code::values::forbidden || beatmap_response.code == curl::status_code::values::unauthorized) {
        std::unique_lock<std::mutex> lock(hanaru::auth_lock, std::try_to_lock);

        if (lock.owns_lock()) {
            this->deauthorize();
            this->authorize();
        }

        co_return { drogon::k401Unauthorized, "", "our downloader become unauthorized, please try again later" };
    }

    if (beatmap_response.code == curl::status_code::values::ok) {
        if (beatmap_response.body.empty() || beatmap_response.body.find("PK\x03\x04") != 0) {
            LOG_WARN << "Response was not valid osz file: " << (beatmap_response.body.size() > 100 ? beatmap_response.body.substr(0, 100) : beatmap_response.body);

            drogon::app().getIOLoop(1)->runAfter(1, [&] { downloading_queue.erase(id); });
            co_return { drogon::k422UnprocessableEntity, "", "response from osu! wasn't valid osz file" };
        }

        const std::string filename = this->get_filename_from_link(beatmap_response.headers);
        hanaru::storage_manager::get()->insert(id, { filename, beatmap_response.body });

        if (hanaru::storage_manager::get()->can_write()) {
            db->execSqlAsync(
                "INSERT INTO beatmaps_names (id, name) VALUES (?, ?);",
                [](const drogon::orm::Result&) {},
                [](const drogon::orm::DrogonDbException&) {},
                id, filename
            );

            beatmap_response.save_to_file(beatmap_path.generic_string());
        }

        drogon::app().getIOLoop(1)->runAfter(1, [&] { downloading_queue.erase(id); });
        co_return { drogon::k200OK, filename, beatmap_response.body };
    }

    co_return { drogon::k503ServiceUnavailable, "", "response from osu! wasn't valid" };
}

hanaru::downloader* const hanaru::downloader::get() {
    return instance;
}

Json::Value hanaru::downloader::serialize_beatmap(const Json::Value& json) const {
    if (json.isNull()) {
        return {};
    }

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
        json["count_normal"].asString(), json["count_slider"].asString(), json["count_spinner"].asString(), max_combo,
        approved, hanaru::string_to_time(json["last_update"]), json["bpm"].asString(), json["hit_length"].asString(),
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

std::string hanaru::downloader::get_filename_from_link(const std::unordered_multimap<std::string, std::string>& headers) const {
    const std::string link = headers.equal_range("location").first->second;

    std::string filename = link.substr(link.find("fs=") + 3);
    filename = filename.substr(0, filename.find(".osz") + 4);

    return drogon::utils::urlDecode(filename);
}

void hanaru::downloader::authorize() {
    curl::client client("https://osu.ppy.sh");
    client.set_user_agent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/89.0.4389.114 Safari/537.36");

    {
        client.set_path("/home");
        curl::response session = client.get();

        if (session.code != curl::status_code::values::ok) {
            LOG_WARN << "invalid response from osu! website";
            downloading_enabled = false;
            instance = this;
            return;
        }

        for (const auto [_, cookie] : session.cookies) {
            client.add_cookie(cookie.key, cookie.value);

            if (cookie.key == "XSRF-TOKEN") {
                csrf_token = cookie.value;
            }
        }
    }

    client.set_path("/session");
    client.set_referer("https://osu.ppy.sh/home");

    client.add_header("Origin", "https://osu.ppy.sh");
    client.add_header("Alt-Used", "osu.ppy.sh");
    client.add_header("Content-Type", "application/x-www-form-urlencoded; charset=UTF-8");
    client.add_header("X-CSRF-Token", csrf_token);

    curl::response login = client.post(
        "_token=" + curl::utils::url_encode(csrf_token) + 
        "&username=" + curl::utils::url_encode(this->username) +
        "&password=" + curl::utils::url_encode(this->password)
    );

    if (login.code != curl::status_code::values::ok) {
        LOG_WARN << "invalid response from osu! website";
        downloading_enabled = false;
        instance = this;
        return;
    }

    client.reset_cookies();
    client.reset_headers();

    for (const auto [_, cookie] : login.cookies) {
        if (cookie.domain == ".ppy.sh") {
            if (cookie.key == "XSRF-TOKEN") {
                csrf_token = cookie.value;
            }

            if (cookie.key == "osu_session") {
                osu_session = cookie.value;
            }
        }
    }

    downloading_enabled = true;
    instance = this;
}

void hanaru::downloader::deauthorize() {
    if (csrf_token.empty() || osu_session.empty()) {
        LOG_WARN << "Trying to deauthorize user with empty csrf token or osu session";
        return;
    }

    curl::client client("https://osu.ppy.sh");

    client.set_path("/session");
    client.set_referer("https://osu.ppy.sh/home");

    client.add_header("Origin", "https://osu.ppy.sh");
    client.add_header("Alt-Used", "osu.ppy.sh");
    client.add_header("X-CSRF-Token", csrf_token);

    client.add_cookie("XSRF-TOKEN", csrf_token);
    client.add_cookie("osu_session", osu_session);

    client.del();
    csrf_token.clear();
    osu_session.clear();
}