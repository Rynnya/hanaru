/*
    CurlEr - simple http client, on top of curl (https://curl.se/)

    Created by Rynnya
    Licensed under the MIT License <http://opensource.org/licenses/MIT>.
    Copyright (c) 2021-2022 Rynnya

    Permission is hereby  granted, free of charge, to any  person obtaining a copy
    of this software and associated  documentation files (the "Software"), to deal
    in the Software  without restriction, including without  limitation the rights
    to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
    copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
    IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
    FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
    AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
    LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#include "curl.hh"

#include <algorithm>
#include <array>

#include <time.h>
#include <iomanip>
#include <sstream>

#include <fstream>

#include <curl/curl.h>

namespace detail {

#ifdef _WIN32

    // https://stackoverflow.com/a/33542189
    char* strptime(const char* s, const char* f, struct tm* tm) {
        std::istringstream input(s);
        static const std::locale loc(setlocale(LC_ALL, nullptr));
        input.imbue(loc);
        input >> std::get_time(tm, f);

        if (input.fail()) {
            return nullptr;
        }

        return (char*)(s + input.tellg());
    }

    time_t timegm(struct tm* tm) {
        struct tm my_tm;
        memcpy(&my_tm, tm, sizeof(struct tm));
        return _mkgmtime(&my_tm);
    }

#endif

    int64_t get_http_date(const std::string& http_string_date) {
        static const std::array<const char*, 4> formats = {
            // RFC822 (default)
            "%a, %d %b %Y %H:%M:%S",
            // RFC 850 (deprecated)
            "%a, %d-%b-%y %H:%M:%S",
            // ansi asctime format
            "%a %b %d %H:%M:%S %Y",
            // weird RFC 850-hybrid thing that reddit uses
            "%a, %d-%b-%Y %H:%M:%S",
        };

        struct tm tmp_tm {};
        for (const char* format : formats) {
            if (strptime(http_string_date.c_str(), format, &tmp_tm) != nullptr) {
                return timegm(&tmp_tm);
            }
        }

        return std::numeric_limits<int64_t>::max();
    }

    std::vector<std::string> split_string(const std::string& str, const std::string& delimiter) {
        if (delimiter.empty()) {
            return {};
        }

        std::vector<std::string> values;
        size_t last = 0;
        size_t next = 0;
        while ((next = str.find(delimiter, last)) != std::string::npos)
        {
            if (next > last) {
                values.push_back(str.substr(last, next - last));
            }
            last = next + delimiter.length();
        }

        if (str.length() > last) {
            values.push_back(str.substr(last));
        }

        return values;
    }

    void split_cookie(std::string& cookie, size_t ptr, std::string& name, std::string& value) {
        name = cookie.substr(0, ptr);
        size_t cpos = 0;
        while (cpos < name.length() && isspace(name[cpos])) {
            ++cpos;
        }

        name = name.substr(cpos);
        ++ptr;
        while (ptr < cookie.length() && isspace(cookie[ptr])) {
            ++ptr;
        }

        value = cookie.substr(ptr);
    }

    void split_cookie(std::string& cookie, std::string& name) {
        size_t cpos = 0;
        while (cpos < cookie.length() && isspace(cookie[cpos])) {
            ++cpos;
        }

        name = cookie.substr(cpos);
    }

    size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        const size_t res = size * nmemb;
        static_cast<std::string*>(userdata)->append(ptr, res);
        return res;
    }

    size_t header_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        const size_t res = size * nmemb;
        const std::string header = std::string(ptr, res);

        const auto& it = header.find(':');
        if (it == std::string::npos) {
            return res;
        }

        std::string key = header.substr(0, it);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) noexcept { return std::tolower(c); });

        /*
        * Let me explain a little bit this -> header.substr(it + 2, res - it - 4)
        *
        * Well, offset to get rid of header name and ':' and ' ' after header
        * And then we remove additional '\r\n' from end, cuz no one really needs this
        */
        static_cast<std::unordered_multimap<std::string, std::string>*>(userdata)->insert({ key, header.substr(it + 2, res - it - 4) });
        return res;
    }

    curl::cookie split_cookie(const std::string& cookie) {
        curl::cookie new_cookie {};
        std::vector<std::string> values = split_string(cookie, ";");

        for (size_t i = 0; i < values.size(); i++) {
            std::string& coo = values[i];
            std::string name {};
            std::string value {};
            size_t delim_pos = coo.find('=');

            delim_pos != std::string::npos
                ? split_cookie(coo, delim_pos, name, value)
                : split_cookie(coo, name);
            
            if (i == 0) {
                new_cookie.key = name;
                new_cookie.value = value;
                continue;
            }

            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) noexcept { return std::tolower(c); });
            if (name == "path") {
                new_cookie.path = value;
            }
            else if (name == "domain") {
                new_cookie.domain = value;
            }
            else if (name == "expires") {
                new_cookie.expires = get_http_date(value);
            }
            else if (name == "secure") {
                new_cookie.secure = true;
            }
            else if (name == "httponly") {
                new_cookie.http_only = true;
            }
            else if (name == "samesite") {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) noexcept { return std::tolower(c); });
                if (value == "lax") {
                    new_cookie.same_site = curl::cookie::same_site_policy::lax;
                }
                else if (value == "strict") {
                    new_cookie.same_site = curl::cookie::same_site_policy::strict;
                }
                else if (value == "none") {
                    new_cookie.same_site = curl::cookie::same_site_policy::none;
                }
            }
            else if (name == "max-age") {
                new_cookie.max_age = std::stoll(value);
            }
        }

        return new_cookie;
    }
}

curl::status_code::status_code(status_code::values value) noexcept
    : value_(value)
{}

curl::status_code::status_code(uint32_t value) noexcept {
    switch (value) {
        case 0:
        case 1:
        case 200:
        case 201:
        case 202:
        case 203:
        case 204:
        case 205:
        case 206:
        case 300:
        case 301:
        case 302:
        case 303:
        case 304:
        case 307:
        case 308:
        case 400:
        case 401:
        case 402:
        case 403:
        case 404:
        case 405:
        case 406:
        case 407:
        case 408:
        case 409:
        case 410:
        case 411:
        case 412:
        case 413:
        case 414:
        case 415:
        case 416:
        case 417:
        case 418:
        case 422:
        case 423:
        case 425:
        case 426:
        case 428:
        case 429:
        case 431:
        case 451:
        case 500:
        case 501:
        case 502:
        case 503:
        case 504:
        case 505:
        case 506:
        case 507:
        case 508:
        case 510:
        case 511: {
            value_ = static_cast<values>(value);
            return;
        }
        default: {
            value_ = values::custom_code;
            return;
        }
    }
}

curl::status_code::operator std::string() {
    switch (this->value_) {
        case values::null: return "null";
        case values::failed: return "failed";

        case values::ok: return "ok";
        case values::created: return "created";
        case values::accepted: return "accepted";
        case values::non_authoritative_information: return "non_authoritative_information";
        case values::no_content: return "no_content";
        case values::reset_content: return "reset_content";
        case values::partial_content: return "partial_content";

        case values::multiple_chooses: return "multiple_chooses";
        case values::moved_permanently: return "moved_permanently";
        case values::found: return "found";
        case values::see_other: return "see_other";
        case values::not_modified: return "not_modified";
        case values::temporary_redirect: return "temporary_redirect";
        case values::permament_redirect: return "permament_redirect";

        case values::bad_request: return "bad_request";
        case values::unauthorized: return "unauthorized";
        case values::payment_required: return "payment_required";
        case values::forbidden: return "forbidden";
        case values::not_found: return "not_found";
        case values::method_not_allowed: return "method_not_allowed";
        case values::not_acceptable: return "not_acceptable";
        case values::proxy_required: return "proxy_required";
        case values::request_timeout: return "request_timeout";
        case values::conflict: return "conflict";
        case values::gone: return "gone";
        case values::length_required: return "length_required";
        case values::precondition_failed: return "precondition_failed";
        case values::payload_too_large: return "payload_too_large";
        case values::url_too_long: return "url_too_long";
        case values::unsupported_media_type: return "unsupported_media_type";
        case values::range_not_satisfiable: return "range_not_satisfiable";
        case values::expectation_failed: return "expectation_failed";
        case values::im_a_teapot: return "im_a_teapot";
        case values::unprocessable_entity: return "unprocessable_entity";
        case values::locked: return "locked";
        case values::too_early: return "too_early";
        case values::upgrade_required: return "upgrade_required";
        case values::precondition_required: return "precondition_required";
        case values::too_many_requests: return "too_many_requests";
        case values::request_header_fields_too_large: return "request_header_fields_too_large";
        case values::unavailable_for_legal_reasons: return "unavailable_for_legal_reasons";

        case values::internal_server_error: return "internal_server_error";
        case values::not_implemented: return "not_implemented";
        case values::bad_gateway: return "bad_gateway";
        case values::service_unavailable: return "service_unavailable";
        case values::gateway_timeout: return "gateway_timeout";
        case values::version_not_supported: return "version_not_supported";
        case values::variant_also_negotiates: return "variant_also_negotiates";
        case values::insufficient_storage: return "insufficient_storage";
        case values::loop_detected: return "loop_detected";
        case values::not_extended: return "not_extended";
        case values::network_auth_required: return "network_auth_required";

        default: return "custom";
    }

    return "custom";
}

curl::status_code::operator uint32_t() noexcept {
    return static_cast<uint32_t>(value_);
}

curl::status_code::operator values() noexcept {
    return value_;
}

bool curl::response::save_to_file(const std::string& filename, bool overwrite) {
    if (!overwrite) {
        std::unique_ptr<FILE, std::function<void(FILE*)>> file(
            fopen(filename.c_str(), "r"),
            [](FILE* handle) noexcept { fclose(handle); }
        );
        if (file) {
            return false;
        }
    }

    std::ofstream output(filename, std::ios::binary);
    if (output.bad()) {
        return false;
    }

    output << this->body;
    output.flush();

    return output.good();
}

curl::client::client(const std::string& host)
    : host_(host)
    , path_()
    , query_()
    , user_agent_("curler/1.0")
    , handle_(curl_easy_init())
{
    if (host_.empty()) {
        throw std::runtime_error("Host must be not empty.");
    }

    const size_t header_pos = host_.find("://");
    if (header_pos == std::string::npos) {
        throw std::runtime_error("Host must be valid link (http://example.com)");
    }

    size_t path_pos = host_.find('/', header_pos + 3);
    if (path_pos == std::string::npos) {
        const size_t query_pos = host_.find('?', header_pos + 3);
        if (query_pos != std::string::npos && query_pos < path_pos) {
            path_pos = query_pos;
        }
    }

    host_ = host_.substr(0, path_pos);
}

curl::client::~client() noexcept {
    curl_slist_free_all(headers_list_);
    headers_list_ = nullptr;
    curl_easy_cleanup(handle_);
}

void curl::client::set_path(const std::string& path) {
    if (path.empty()) {
        path_ = path;
        return;
    }

    const bool with_slash = path[0] == '/';
    const size_t query_pos = path.find('?');

    if (query_pos == std::string::npos) {
        path_ = with_slash ? path : '/' + path;
        return;
    }

    const std::string path_without_query = path.substr(0, query_pos);
    path_ = with_slash ? path_without_query : '/' + path_without_query;
}

void curl::client::set_parameter(const std::string& key, const std::string& value) {
    query_.insert({ key, value });
}

void curl::client::add_header(const std::string& key, const std::string& value) {
    headers_.insert({ key, value });
}

void curl::client::add_cookie(const std::string& key, const std::string& value) {
    cookies_.insert({ key, value });
}

void curl::client::reset_headers() noexcept {
    headers_.clear();
}

void curl::client::reset_cookies() noexcept {
    cookies_.clear();
}

void curl::client::set_referer(const std::string& referer) noexcept {
    curl_easy_setopt(handle_, CURLOPT_REFERER, referer.c_str());
}

void curl::client::set_user_agent(const std::string& agent) {
    user_agent_ = agent;
}

void curl::client::follow_redirects(bool state) noexcept {
    follow_redirects_ = state;
}

void curl::client::save_cookies_in_headers(bool state) noexcept {
    save_cookies_in_headers_ = state;
}

curl::response curl::client::get() {
    curl::response resp {};
    prepare(request_type::get, resp);

    const CURLcode result = curl_easy_perform(handle_);
    if (result != CURLE_OK) {
        on_error(result, resp);
        return resp;
    }

    on_done(resp);
    return resp;
}

curl::response curl::client::head() {
    curl::response resp {};
    prepare(request_type::head, resp);

    // https://curl.se/mail/lib-2007-10/0025.html
    curl_easy_setopt(handle_, CURLOPT_HEADER, 1L);
    curl_easy_setopt(handle_, CURLOPT_NOBODY, 1L);

    const CURLcode result = curl_easy_perform(handle_);
    if (result != CURLE_OK) {
        on_error(result, resp);
        return resp;
    }

    on_done(resp);
    return resp;
}

curl::response curl::client::post(const std::string& body) {
    curl::response resp {};
    prepare(request_type::post, resp);

    curl_easy_setopt(handle_, CURLOPT_POST, 1L);
    curl_easy_setopt(handle_, CURLOPT_POSTFIELDS, body.c_str());

    const CURLcode result = curl_easy_perform(handle_);
    if (result != CURLE_OK) {
        on_error(result, resp);
        return resp;
    }

    on_done(resp);
    return resp;
}

curl::response curl::client::put(const std::string& body) {
    curl::response resp {};
    prepare(request_type::del, resp);

    // https://stackoverflow.com/a/7570281
    curl_easy_setopt(handle_, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(handle_, CURLOPT_POSTFIELDS, body.c_str());

    const CURLcode result = curl_easy_perform(handle_);
    if (result != CURLE_OK) {
        on_error(result, resp);
        return resp;
    }

    on_done(resp);
    return resp;
}

curl::response curl::client::del(const std::string& body) {
    curl::response resp {};
    prepare(request_type::del, resp);

    // https://stackoverflow.com/a/34751940
    curl_easy_setopt(handle_, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(handle_, CURLOPT_POSTFIELDS, body.c_str());

    const CURLcode result = curl_easy_perform(handle_);
    if (result != CURLE_OK) {
        on_error(result, resp);
        return resp;
    }

    on_done(resp);
    return resp;
}

curl::response curl::client::patch(const std::string& body) {
    curl::response resp {};
    prepare(request_type::patch, resp);

    // https://curl.se/mail/lib-2016-08/0111.html
    curl_easy_setopt(handle_, CURLOPT_CUSTOMREQUEST, "PATCH");
    curl_easy_setopt(handle_, CURLOPT_POSTFIELDS, body.c_str());

    const CURLcode result = curl_easy_perform(handle_);
    if (result != CURLE_OK) {
        on_error(result, resp);
        return resp;
    }

    on_done(resp);
    return resp;
}

void curl::client::pre_request(pre_request_handler&& callback) noexcept {
    pre_request_callback_ = std::move(callback);
}

void curl::client::prepare(request_type type, response& resp) {
    std::string url = host_ + path_;
    
    if (query_.size() > 0) {
        url.push_back('?');

        for (const auto& [key, value] : query_) {
            url.append(curl::utils::url_encode(key)).push_back('=');
            url.append(curl::utils::url_encode(value)).push_back('&');
        }

        url.pop_back();
    }

    if (cookies_.size() > 0) {
        std::string cookie_header {};
        for (const auto& [key, value] : cookies_) {
            cookie_header.append(key).push_back('=');
            cookie_header.append(value).push_back(';');
        }

        cookie_header.pop_back();
        curl_easy_setopt(handle_, CURLOPT_COOKIE, cookie_header.c_str());
    }

    if (headers_.size() > 0) {
        for (const auto& [key, value] : headers_) {
            const std::string head = key + ": " + value;
            headers_list_ = curl_slist_append(headers_list_, head.c_str());
        }

        curl_easy_setopt(handle_, CURLOPT_HTTPHEADER, headers_list_);
    }

    if (follow_redirects_) {
        curl_easy_setopt(handle_, CURLOPT_FOLLOWLOCATION, 1L);
    }

    curl_easy_setopt(handle_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle_, CURLOPT_USERAGENT, user_agent_.c_str());
    curl_easy_setopt(handle_, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, &detail::write_callback);
    curl_easy_setopt(handle_, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(handle_, CURLOPT_HEADERFUNCTION, &detail::header_callback);
    curl_easy_setopt(handle_, CURLOPT_HEADERDATA, &resp.headers);

    resp.type = type;

    if (pre_request_callback_) {
        pre_request_callback_(url);
    }
}

void curl::client::on_done(response& resp) {
    const auto range = resp.headers.equal_range("set-cookie");
    for (auto it = range.first; it != range.second; it++) {
        curl::cookie new_cookie = detail::split_cookie(it->second);
        resp.cookies.insert({ new_cookie.key, new_cookie });
    }

    if (!save_cookies_in_headers_) {
        resp.headers.erase("set-cookie");
    }

    long status_code = 0;
    curl_easy_getinfo(handle_, CURLINFO_RESPONSE_CODE, &status_code);
    resp.code = curl::status_code(status_code);

    curl_slist_free_all(headers_list_);
    headers_list_ = nullptr;

    curl_easy_reset(handle_);
}

void curl::client::on_error(CURLcode code, response& resp) {
    resp.code = status_code::values::failed;
    resp.body = curl_easy_strerror(code);

    curl_slist_free_all(headers_list_);
    headers_list_ = nullptr;

    curl_easy_reset(handle_);
}

std::string curl::utils::url_encode(const std::string& src) {
    std::string result;
    std::string::const_iterator iter;

    for (iter = src.begin(); iter != src.end(); iter++) {
        switch (*iter) {
        case ' ':
            result.push_back('+');
            break;
        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
        case 'G':
        case 'H':
        case 'I':
        case 'J':
        case 'K':
        case 'L':
        case 'M':
        case 'N':
        case 'O':
        case 'P':
        case 'Q':
        case 'R':
        case 'S':
        case 'T':
        case 'U':
        case 'V':
        case 'W':
        case 'X':
        case 'Y':
        case 'Z':
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
        case 'g':
        case 'h':
        case 'i':
        case 'j':
        case 'k':
        case 'l':
        case 'm':
        case 'n':
        case 'o':
        case 'p':
        case 'q':
        case 'r':
        case 's':
        case 't':
        case 'u':
        case 'v':
        case 'w':
        case 'x':
        case 'y':
        case 'z':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '-':
        case '_':
        case '.':
        case '!':
        case '~':
        case '*':
        case '\'':
        case '(':
        case ')':
        case '&':
        case '=':
        case '/':
        case '\\':
        case '?':
            result.push_back(*iter);
            break;
        default:
            result.push_back('%');
            result.append(char_to_hex(*iter));
            break;
        }
    }

    return result;
}

std::string curl::utils::url_decode(const std::string& src) {
    std::string result;
    const size_t len = src.length();
    result.reserve(len * 2);
    int hex = 0;
    for (size_t i = 0; i < len; i++) {
        switch (src[i]) {
        case '+':
            result.push_back(' ');
            break;
        case '%':
            if ((i + 2) < len && isxdigit(src[i + 1]) && isxdigit(src[i + 2])) {
                unsigned int x1 = src[i + 1];
                if (x1 >= '0' && x1 <= '9') {
                    x1 -= '0';
                }
                else if (x1 >= 'a' && x1 <= 'f') {
                    x1 = x1 - 'a' + 10;
                }
                else if (x1 >= 'A' && x1 <= 'F') {
                    x1 = x1 - 'A' + 10;
                }

                unsigned int x2 = src[i + 2];
                if (x2 >= '0' && x2 <= '9') {
                    x2 -= '0';
                }
                else if (x2 >= 'a' && x2 <= 'f') {
                    x2 = x2 - 'a' + 10;
                }
                else if (x2 >= 'A' && x2 <= 'F') {
                    x2 = x2 - 'A' + 10;
                }

                hex = x1 * 16 + x2;
                result.push_back(static_cast<char>(hex));
                i += 2;
            }
            else {
                result.push_back('%');
            }
            break;
        default:
            result.push_back(src[i]);
            break;
        }
    }
    return result;
}

std::string curl::utils::char_to_hex(char c) {
    std::string result;
    result.reserve(2);

    char first = (c & 0xF0) / 16;
    first += first > 9 ? 'A' - 10 : '0';
    char second = c & 0x0F;
    second += second > 9 ? 'A' - 10 : '0';

    result.push_back(first);
    result.push_back(second);

    return result;
}