#include "DownloadRoute.h"

#include "../impl/cache_system.h"

#include <drogon/HttpClient.h>
#include <drogon/utils/Utilities.h>

#include <fstream>
#include <filesystem>
#include <mutex>
#include <shared_mutex>

const char* filename_insert_query = "INSERT INTO beatmaps_names (id, name) VALUES (?, ?);";
const char* filename_select_query = "SELECT name FROM beatmaps_names WHERE id = ? LIMIT 1;";

#define SEND_NULL_FILE(CALLBACK, ID_AS_STRING)                                                          \
    {                                                                                                   \
        HttpResponsePtr response = HttpResponse::newHttpResponse();                                     \
        response->setBody("");                                                                          \
        response->setContentTypeCodeAndCustomString(CT_CUSTOM, "application/x-osu-beatmap-archive");    \
        response->addHeader("Content-Disposition", "attachment; filename=" + ID_AS_STRING + ".osz");    \
        CALLBACK(response);                                                                             \
        return;                                                                                         \
    }

namespace fs = std::filesystem;
fs::path beatmap_path = fs::current_path() / "beatmaps";

bool enable_downloading = false;
std::atomic_uint32_t limiter = 0;

HttpClientPtr client = HttpClient::newHttpClient("https://osu.ppy.sh");

std::tuple<std::string, std::string, std::string> split_download_link(const std::string& link) {
    size_t ptr = link.find("ppy.sh");
    std::string query = link.substr(ptr + 6);
    std::string filename = query.substr(query.find("fs=") + 3);
    size_t enc_ptr = filename.find("%20") + 3;
    filename = filename.substr(enc_ptr, filename.find(".osz") + 4 - enc_ptr);
    return { std::string(link.begin(), link.begin() + ptr + 6), query, utils::urlDecode(filename) };
}

void init() {
    if (!fs::exists(beatmap_path)) {
        fs::create_directory(beatmap_path);
    }

#ifdef HANARU_CACHE
    hanaru::initialize();
#endif

    app().getIOLoop(1)->runEvery(10, [&]() {
        if (limiter > 0) {
            limiter--;
        }
    });

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
    client->setUserAgent("hanaru/0.1");

    HttpClientPtr login_client = HttpClient::newHttpClient("https://old.ppy.sh");
    login_client->enableCookies();
    login_client->setUserAgent("hanaru/0.1");

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

    // Initialize user client
    static std::once_flag once;
    std::call_once(once, &init);

    auto db = app().getDbClient();
    const std::string id_as_string = std::to_string(id);

#ifdef HANARU_CACHE
    std::optional<hanaru::cached_beatmap> cached = hanaru::get(id);
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

    // Trying to find on disk
    if (fs::exists(beatmap)) {
        std::ifstream beatmap_file(beatmap, std::ios::binary);

        std::string contents;
        beatmap_file.seekg(0, std::ios::end);
        contents.resize(beatmap_file.tellg());
        beatmap_file.seekg(0, std::ios::beg);
        beatmap_file.read(&contents[0], contents.size());
        beatmap_file.close();

        std::string filename = id_as_string + ".osz";
        const auto& result = db->execSqlSync(filename_select_query, id);
        if (!result.empty()) {
            const auto& row = result.front();
            filename = row["name"].as<std::string>();
        }

#ifdef HANARU_CACHE
    hanaru::insert(id, hanaru::cached_beatmap(filename, contents.data(), contents.size()));
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
        SEND_NULL_FILE(callback, id_as_string);
    }

    if (limiter > 10) {
        SEND_NULL_FILE(callback, id_as_string);
    }

    limiter++;

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
        auto [endpoint, query, filename] = split_download_link(location_response->getHeader("Location"));

        HttpClientPtr dclient = HttpClient::newHttpClient(endpoint);
        HttpRequestPtr drequest = HttpRequest::newHttpRequest();
        drequest->setPath(query);

        auto [__, beatmap_response] = dclient->sendRequest(drequest);
        std::string_view view = beatmap_response->getBody();

        // Verify 'magic' value for response
        if (view.empty() || view.find("PK\x03\x04") != 0) {
            LOG_WARN << "Response was not valid osz file: " << (view.size() > 100 ? view.substr(0, 100) : view);
            SEND_NULL_FILE(callback, id_as_string);
        }

        HttpResponsePtr response = HttpResponse::newFileResponse(
            reinterpret_cast<const unsigned char*>(view.data()),
            view.size(),
            filename,
            CT_CUSTOM,
            "application/x-osu-beatmap-archive"
        );

        db->execSqlAsync(
            filename_insert_query,
            [](const orm::Result&) {},
            [](const orm::DrogonDbException&) {},
            id, filename
        );

#ifdef HANARU_CACHE
    hanaru::insert(id, hanaru::cached_beatmap(filename, view.data(), view.size()));
#endif

        std::ofstream beatmap_file(beatmap, std::ios::binary);
        beatmap_file.write(view.data(), view.size());
        beatmap_file.close();

        callback(response);
    }

    SEND_NULL_FILE(callback, id_as_string);
}
