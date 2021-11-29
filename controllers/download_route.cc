#include "download_route.hh"

#include "../impl/globals.hh"
#include "../impl/memory_system.hh"
#include "../impl/rate_limiter.hh"

#include <drogon/HttpClient.h>
#include <drogon/utils/Utilities.h>

#include <fstream>
#include <filesystem>
#include <mutex>
#include <shared_mutex>

const std::string empty_beatmap = "";

const char* filename_insert_query = "INSERT INTO beatmaps_names (id, name) VALUES (?, ?);";
const char* filename_select_query = "SELECT name FROM beatmaps_names WHERE id = ? LIMIT 1;";

namespace fs = std::filesystem;
fs::path beatmap_path = fs::current_path() / "beatmaps";

bool enable_downloading = false;

HttpClientPtr client = HttpClient::newHttpClient("https://osu.ppy.sh");

std::tuple<std::string, std::string, std::string> split_download_link(const std::string& link) {
    size_t ptr = link.find("ppy.sh");
    std::string query = link.substr(ptr + 6);
    std::string filename = query.substr(query.find("fs=") + 3);
    filename = filename.substr(0, filename.find(".osz") + 4);

    return { std::string(link.begin(), link.begin() + ptr + 6), query, utils::urlDecode(filename) };
}

void init() {
    if (!fs::exists(beatmap_path)) {
        fs::create_directory(beatmap_path);
    }

    hanaru::initialize();

    Json::Value custom_cfg = app().getCustomConfig();
    if (!custom_cfg["osu_username"].isString() || !custom_cfg["osu_password"].isString()) {
        return;
    }

    std::string username = custom_cfg["osu_username"].asString();
    std::string password = custom_cfg["osu_password"].asString();

    if (username.empty() || password.empty()) {
        return;
    }

    client->enableCookies();
    client->setUserAgent("hanaru/0.5");

    HttpClientPtr login_client = HttpClient::newHttpClient("https://old.ppy.sh");
    login_client->enableCookies();
    login_client->setUserAgent("hanaru/0.5");

    HttpRequestPtr login_request = HttpRequest::newHttpFormPostRequest();
    login_request->setPath("/forum/ucp.php?mode=login");

    login_request->addHeader("Origin", "https://osu.ppy.sh");
    login_request->addHeader("Referer", "https://osu.ppy.sh");

    login_request->setParameter("redirect", "/");
    login_request->setParameter("sid", "");
    login_request->setParameter("username", username);
    login_request->setParameter("password", password);
    login_request->setParameter("autologin", "on");
    login_request->setParameter("login", "Login");

    auto [result, response] = login_client->sendRequest(login_request);
    if (result != ReqResult::Ok) {
        return;
    }

    const std::string& location = response->getHeader("Location");
    if (location.size() != 1 && location.find("success") == std::string::npos) {
        return;
    }

    for (auto cookie : response->getCookies()) {
        if (cookie.first == "Location") {
            continue;
        }

        client->addCookie(cookie.second);
    }

    enable_downloading = true;
}

void DownloadRoute::get(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, int32_t id) {
    if (!hanaru::rate_limit::consume(1)) {
        SEND_ERROR(callback, k429TooManyRequests, "rate limit, please try again");
    }

    // Initialize user client
    static std::once_flag once;
    std::call_once(once, &init);

    auto db = app().getDbClient();
    const std::string id_as_string = std::to_string(id);

    #ifdef HANARU_CACHE
        std::optional<hanaru::cache::cached_beatmap> cached = hanaru::cache::get(id);
        if (cached.has_value()) {
            HttpResponsePtr response = HttpResponse::newFileResponse(
                reinterpret_cast<const unsigned char*>(cached->content.data()),
                cached->content.size(),
                cached->name,
                CT_CUSTOM,
                "application/x-osu-beatmap-archive"
            );
            callback(response);
            return;
        }
    #endif

    const fs::path beatmap = beatmap_path / id_as_string;

    if (!hanaru::rate_limit::consume(20)) {
        SEND_ERROR(callback, k429TooManyRequests, "rate limit, please wait 3 seconds");
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

            #ifdef HANARU_CACHE
                hanaru::cache::insert(id, hanaru::cache::cached_beatmap("", empty_beatmap.data(), empty_beatmap.size()));
            #endif

            SEND_ERROR(callback, k404NotFound, "beatmapset doesn't exist on osu! servers");
        }

        std::string filename = id_as_string + ".osz";
        const auto& result = db->execSqlSync(filename_select_query, id);
        if (!result.empty()) {
            const auto& row = result.front();
            filename = row["name"].as<std::string>();
        }

        #ifdef HANARU_CACHE
            hanaru::cache::insert(id, hanaru::cache::cached_beatmap(filename, contents.data(), contents.size()));
        #endif

        HttpResponsePtr response = HttpResponse::newFileResponse(
            reinterpret_cast<const unsigned char*>(contents.data()),
            contents.size(),
            filename,
            CT_CUSTOM,
            "application/x-osu-beatmap-archive"
        );

        callback(response);
        return;
    }

    if (!enable_downloading) {
        SEND_ERROR(callback, k423Locked, "downloading disabled");
    }

    if (!hanaru::rate_limit::consume(40)) {
        SEND_ERROR(callback, k429TooManyRequests, "rate limit, please wait 6 seconds");
    }

    // Beatmapset wasn't found anywhere, so we download it
    HttpRequestPtr request = HttpRequest::newHttpRequest();
    request->setPath("/beatmapsets/" + id_as_string + "/download");
    request->setParameter("noVideo", "1");

    // Additional headers to properly redirect us to beatmap file
    request->addHeader("Alt-Used", "osu.ppy.sh");
    request->addHeader("Connection", "keep-alive");
    request->addHeader("Origin", "https://osu.ppy.sh");
    request->addHeader("Referer", "https://osu.ppy.sh/beatmapsets/" + id_as_string);
    request->addHeader("TE", "trailers");

    // Sadly, but HttpClient, provided by drogon, cannot do redirects, so we do it manually
    auto [location_result, location_response] = client->sendRequest(request);
    if (location_result == ReqResult::Ok) {
        // If beatmap doesn't exist, then Location header will be empty
        const std::string& location = location_response->getHeader("Location");
        if (location.empty()) {
            // This will create empty file so we doesn't need to always ask peppy about this map
            // Some new maps might be added, so we should only apply this on old maps
            if (id < 1300000) {
                std::ofstream beatmap_file;
                beatmap_file.open(beatmap);
                beatmap_file.close();

                #ifdef HANARU_CACHE
                    hanaru::cache::insert(id, hanaru::cache::cached_beatmap("", empty_beatmap.data(), empty_beatmap.size()));
                #endif
            }

            SEND_ERROR(callback, k404NotFound, "beatmapset doesn't exist on osu! servers");
        }

        auto [endpoint, query, filename] = split_download_link(location);

        HttpClientPtr dclient = HttpClient::newHttpClient(endpoint);
        HttpRequestPtr drequest = HttpRequest::newHttpRequest();
        drequest->setPath(query);

        auto [__, beatmap_response] = dclient->sendRequest(drequest);
        std::string_view view = beatmap_response->getBody();

        // Verify 'magic' value for response
        if (view.empty() || view.find("PK\x03\x04") != 0) {
            LOG_WARN << "Response was not valid osz file: " << (view.size() > 100 ? view.substr(0, 100) : view);
            SEND_ERROR(callback, k422UnprocessableEntity, "response from osu! wasn't valid osz file");
        }

        HttpResponsePtr response = HttpResponse::newFileResponse(
            reinterpret_cast<const unsigned char*>(view.data()),
            view.size(),
            filename,
            CT_CUSTOM,
            "application/x-osu-beatmap-archive"
        );

        #ifdef HANARU_CACHE
            hanaru::cache::insert(id, hanaru::cache::cached_beatmap(filename, view.data(), view.size()));
        #endif

        if (hanaru::storage::can_write()) {
            db->execSqlAsync(
                filename_insert_query,
                [](const orm::Result&) {},
                [](const orm::DrogonDbException&) {},
                id, filename
            );

            std::ofstream beatmap_file(beatmap, std::ios::binary);
            beatmap_file.write(view.data(), view.size());
            beatmap_file.close();
        }

        callback(response);
    }

    SEND_ERROR(callback, k503ServiceUnavailable, "osu! server didn't respond");
}
