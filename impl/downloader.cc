#include "downloader.hh"

#include "utils.hh"
#include "rate_limiter.hh"
#include "storage_manager.hh"

#include <drogon/HttpAppFramework.h>
#include <drogon/HttpClient.h>

#include <fstream>
#include <iomanip>

#include "curl.hh"

namespace detail {

    std::mutex queue_mutex_ {};
    std::condition_variable queue_cond_ {};

    struct awaitable_condition : public drogon::CallbackAwaiter<std::shared_ptr<hanaru::cached_beatmap>> {
    public:
        awaitable_condition(int64_t id, bool should_continue = false) : id_(id), should_continue_(should_continue) {};

        void await_suspend(std::coroutine_handle<> handle) {
            if (should_continue_) {
                setValue({});
                handle.resume();
                return;
            }

            std::thread([handle = std::move(handle), this]() {
                std::shared_ptr<hanaru::cached_beatmap> beatmap {};
                std::unique_lock<std::mutex> lock_ { queue_mutex_ };
                queue_cond_.wait(lock_, [&beatmap, this]() { beatmap = hanaru::storage_manager::get().find(id_); return beatmap; });

                setValue(beatmap);
                handle.resume();
            }).detach();
        }
    private:
        int64_t id_ = 0;
        bool should_continue_ = false;
    };

    class callback_echo {
    public:
        callback_echo() = default;

        awaitable_condition push(int64_t beatmap_id) {
            if (first_callback.exchange(false)) {
                return awaitable_condition(beatmap_id, true);
            }

            return awaitable_condition(beatmap_id);
        }

    private:
        std::atomic_bool first_callback = true;
    };

    bool downloading_enabled_ = false;
    std::unordered_map<int64_t, callback_echo> downloading_queue_ {};

    void unlock(int64_t id) {
        trantor::EventLoop::getEventLoopOfCurrentThread()->runAfter(std::chrono::milliseconds(300), [&] { 
            detail::downloading_queue_.erase(id);
            queue_cond_.notify_all();
        });
    }

    //////////////////////////////////////////////////////////

    std::mutex auth_lock_ {};

    // Required to deauthrorize user if needed
    std::string csrf_token_ {};
    std::string osu_session_ {};

    struct curl_awaitable : public drogon::CallbackAwaiter<curl::response> {
    public:
        curl_awaitable(const std::string& beatmapset_id) : beatmapset_id_(beatmapset_id) {};

        void await_suspend(std::coroutine_handle<> handle) {
            trantor::EventLoop::getEventLoopOfCurrentThread()->runInLoop([handle = std::move(handle), this]() {
                curl::client download_client("https://osu.ppy.sh");

                download_client.set_path("/beatmapsets/" + beatmapset_id_ + "/download");
                download_client.set_parameter("noVideo", "1");

                download_client.add_header("Alt-Used", "osu.ppy.sh");
                download_client.add_header("Connection", "keep-alive");
                download_client.add_header("X-CSRF-Token", detail::csrf_token_);
                download_client.set_referer("https://osu.ppy.sh/beatmapsets/" + beatmapset_id_);

                download_client.set_user_agent(HANARU_USER_AGENT);

                download_client.add_cookie("XSRF-TOKEN", detail::csrf_token_);
                download_client.add_cookie("osu_session", detail::osu_session_);

                setValue(download_client.get());
                handle.resume();
            });
        }

    private:
        std::string beatmapset_id_ {};
    };

    curl_awaitable send_request(const std::string& beatmapset_id) {
        return curl_awaitable(beatmapset_id);
    }

    void update_token(const std::unordered_multimap<std::string, curl::cookie>& cookies) {
        const auto range = cookies.equal_range("xsrf-token");

        for (auto it = range.first; it != range.second; it++) {
            // Tokens always has a same size, which is 40 (04.05.2022)
            if (it->second.value.size() == csrf_token_.size() && it->second.domain == ".ppy.sh") {
                csrf_token_ = it->second.value;
                return;
            }
        }
    }

    //////////////////////////////////////////////////////////

    static hanaru::downloader* instance_ = nullptr;
}

hanaru::downloader::downloader(const std::string& username_, const std::string& password_, const std::string& api_key_)
    : username(username_)
    , password(password_)
    , api_key(api_key_)
{
    b_client->setUserAgent("hanaru/" HANARU_VERSION);
    s_client->setUserAgent("hanaru/" HANARU_VERSION);
    detail::instance_ = this;

    if (username.empty() || password.empty()) {
        LOG_WARN << "downloading disabled due empty login and password";
        return;
    }

    drogon::app().setTermSignalHandler([&]() {
        this->deauthorize();
        drogon::app().quit();
    });

    // This thread will keep our current session online, because peppy's tokens expires every 30 days if not used
    std::thread([&]() -> drogon::AsyncTask {
        this->authorize();
        if (!detail::downloading_enabled_) {
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

drogon::Task<std::tuple<Json::Value, drogon::HttpStatusCode>> hanaru::downloader::download_beatmap(int64_t id) {
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

drogon::Task<std::tuple<Json::Value, drogon::HttpStatusCode>> hanaru::downloader::download_beatmapset(int64_t id) {
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

drogon::Task<std::tuple<drogon::HttpStatusCode, std::string, std::string>> hanaru::downloader::download_map(int64_t id) {
    if (!hanaru::rate_limit::consume(1)) {
        co_return { drogon::k429TooManyRequests, "", "rate limit, please try again" };
    }

    // Firstly we must check if beatmap in cache
    {
        std::shared_ptr<hanaru::cached_beatmap> cached = hanaru::storage_manager::get().find(id);
        if (cached) {
            // Retry every 15 minutes because this might be ratelimit
            if (cached->content.size() == 0 && (!cached->retry || (cached->timestamp + std::chrono::minutes(15) < std::chrono::system_clock::now()))) {
                co_return { drogon::k404NotFound, "", "beatmapset doesn't exist on osu! servers or this beatmapset was banned" };
            }

            co_return { drogon::k200OK, cached->name, cached->content };
        }
    }

    if (!hanaru::rate_limit::consume(20)) {
        co_return { drogon::k429TooManyRequests, "", "rate limit, please wait 2 seconds" };
    }

    drogon::orm::DbClientPtr db = drogon::app().getDbClient();
    const std::string id_as_string = std::to_string(id);
    const fs::path beatmap_path = beatmaps_folder_path / id_as_string;

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
            hanaru::storage_manager::get().insert(id, { "", "" });
            co_return { drogon::k404NotFound, "", "beatmapset doesn't exist on osu! servers or this beatmapset was banned" };
        }

        std::string filename = id_as_string + ".osz";
        const auto& result = co_await db->execSqlCoro("SELECT name FROM beatmaps_names WHERE id = ? LIMIT 1;", id);

        if (!result.empty()) {
            const auto& row = result.front();
            filename = row["name"].as<std::string>();
        }

        hanaru::storage_manager::get().insert(id, { filename, contents });
        co_return { drogon::k200OK, filename, contents };
    }

    if (!detail::downloading_enabled_) {
        co_return { drogon::k423Locked, "", "downloading disabled" };
    }

    if (!hanaru::rate_limit::consume(40)) {
        co_return { drogon::k429TooManyRequests, "", "rate limit, please wait 6 seconds" };
    }

    {
        // If value didn't exist - it will be created and automatically grabbed by one thread because of atomic_bool
        // Other threads will wait until job is done
        // This still a race condition, but at least it's not that terrable
        std::shared_ptr<hanaru::cached_beatmap> cached = co_await detail::downloading_queue_[id].push(id);
        if (cached) { // We should not verify `retry` here, otherwise this will ratelimit our downloader
            if (cached->content.size() == 0) {
                co_return { drogon::k404NotFound, "", "beatmapset doesn't exist on osu! servers or this beatmapset was banned" };
            }

            co_return { drogon::k200OK, cached->name, cached->content };
        }
    }

    // Beatmapset wasn't found anywhere, so we download it
    curl::response beatmap_response = co_await detail::send_request(id_as_string);

    switch (beatmap_response.code) {
        // Something went wrong with our token (did the user deleted it from account settings?)
        case curl::status_code::values::forbidden:
        case curl::status_code::values::unauthorized: {
            std::unique_lock<std::mutex> lock(detail::auth_lock_, std::try_to_lock);

            if (lock.owns_lock()) {
                this->deauthorize();
                this->authorize();
            }

            hanaru::storage_manager::get().insert(id, { "", "", true });
            detail::unlock(id);

            co_return { drogon::k401Unauthorized, "", "our downloader become unauthorized, please try again later" };
        }

        // Something went wrong with beatmap, probably doesn't exist or simply banned due to DMCA
        case curl::status_code::values::not_found: {
            std::ofstream beatmap_file;
            beatmap_file.open(beatmap_path);
            beatmap_file.close();

            hanaru::storage_manager::get().insert(id, { "", "", true });
            detail::unlock(id);

            co_return { drogon::k404NotFound, "", "beatmapset doesn't exist on osu! servers or this beatmapset was banned" };
        }

        // Peppy's mirror's have rate limit's too!
        case curl::status_code::values::too_many_requests: {
            hanaru::storage_manager::get().insert(id, { "", "", true });
            detail::unlock(id);

            co_return { drogon::k429TooManyRequests, "", "downloader was limited by osu! system, please wait 15 minutes before retrying" };
        }

        // Everything went fine \o/
        case curl::status_code::values::ok: {
            if (beatmap_response.body.empty() || beatmap_response.body.find("PK\x03\x04") != 0) {
                LOG_WARN << "Response was not valid osz file: " << (beatmap_response.body.size() > 100 ? beatmap_response.body.substr(0, 100) : beatmap_response.body);

                hanaru::storage_manager::get().insert(id, { "", "", true });
                detail::unlock(id);

                co_return { drogon::k422UnprocessableEntity, "", "response from osu! wasn't valid osz file" };
            }

            detail::update_token(beatmap_response.cookies);
            const std::string filename = this->get_filename_from_link(beatmap_response.headers);

            hanaru::storage_manager::get().insert(id, { filename, beatmap_response.body });

            if (hanaru::storage_manager::get().can_write()) {
                db->execSqlAsync(
                    "INSERT INTO beatmaps_names (id, name) VALUES (?, ?);",
                    [](const drogon::orm::Result&) {},
                    [](const drogon::orm::DrogonDbException&) {},
                    id, filename
                );

                beatmap_response.save_to_file(beatmap_path.generic_string());
            }

            detail::unlock(id);
            co_return { drogon::k200OK, filename, beatmap_response.body };
        }

        default: {
            hanaru::storage_manager::get().insert(id, { "", "", true });
            detail::unlock(id);

            co_return { drogon::k503ServiceUnavailable, "", "response from osu! wasn't valid" };
        }
    }

    hanaru::storage_manager::get().insert(id, { "", "", true });
    detail::unlock(id);

    co_return { drogon::k503ServiceUnavailable, "", "response from osu! wasn't valid" };
}

hanaru::downloader& hanaru::downloader::get() {
    return *detail::instance_;
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
    client.set_user_agent(HANARU_USER_AGENT);

    {
        client.set_path("/home");
        curl::response session = client.get();

        if (session.code != curl::status_code::values::ok) {
            LOG_WARN << "invalid response from osu! website";
            detail::downloading_enabled_ = false;
            detail::instance_ = this;
            return;
        }

        for (const auto [_, cookie] : session.cookies) {
            client.add_cookie(cookie.key, cookie.value);

            if (cookie.key == "XSRF-TOKEN") {
                detail::csrf_token_ = cookie.value;
            }
        }
    }

    client.set_path("/session");
    client.set_referer("https://osu.ppy.sh/home");

    client.add_header("Origin", "https://osu.ppy.sh");
    client.add_header("Alt-Used", "osu.ppy.sh");
    client.add_header("Content-Type", "application/x-www-form-urlencoded; charset=UTF-8");
    client.add_header("X-CSRF-Token", detail::csrf_token_);

    curl::response login = client.post(
        "_token=" + curl::utils::url_encode(detail::csrf_token_) +
        "&username=" + curl::utils::url_encode(this->username) +
        "&password=" + curl::utils::url_encode(this->password)
    );

    if (login.code != curl::status_code::values::ok) {
        LOG_WARN << "invalid response from osu! website";
        detail::downloading_enabled_ = false;
        detail::instance_ = this;
        return;
    }

    client.reset_cookies();
    client.reset_headers();

    for (const auto [_, cookie] : login.cookies) {
        if (cookie.domain == ".ppy.sh") {
            if (cookie.key == "XSRF-TOKEN") {
                detail::csrf_token_ = cookie.value;
            }

            if (cookie.key == "osu_session") {
                detail::osu_session_ = cookie.value;
            }
        }
    }

    detail::downloading_enabled_ = true;
    detail::instance_ = this;
}

void hanaru::downloader::deauthorize() {
    if (detail::csrf_token_.empty() || detail::osu_session_.empty()) {
        LOG_WARN << "Trying to deauthorize user with empty csrf token or osu session";
        return;
    }

    curl::client client("https://osu.ppy.sh");

    client.set_path("/session");
    client.set_referer("https://osu.ppy.sh/home");

    client.add_header("Origin", "https://osu.ppy.sh");
    client.add_header("Alt-Used", "osu.ppy.sh");
    client.add_header("X-CSRF-Token", detail::csrf_token_);

    client.add_cookie("XSRF-TOKEN", detail::csrf_token_);
    client.add_cookie("osu_session", detail::osu_session_);

    client.del();
    detail::csrf_token_.clear();
    detail::osu_session_.clear();
}