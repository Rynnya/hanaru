/*
    curlEr - simple async http client, on top of curl (https://curl.se/)

    Created by Rynnya (https://github.com/Rynnya)
    Licensed under the MIT License <http://opensource.org/licenses/MIT>.
    Copyright (c) 2021-2022 Rynnya (https://github.com/Rynnya)

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

#include "curler.hh"

#include <algorithm>
#include <array>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include <curl/curl.h>

#ifndef NOMINMAX
#   define NOMINMAX
#endif

#ifdef DELETE
#   undef DELETE
#endif

namespace detail {

#ifdef _WIN32

    // https://stackoverflow.com/a/33542189
    const char* strptime(const char* s, const char* f, struct tm* tm) {
        std::istringstream input { s };
        static const std::locale loc { setlocale(LC_ALL, nullptr) };
        input.imbue(loc);
        input >> std::get_time(tm, f);

        if (input.fail()) {
            return nullptr;
        }

        return static_cast<const char*>(s + input.tellg());
    }

    time_t timegm(const struct tm* tm) noexcept {
        struct tm myTm;
        memcpy(&myTm, tm, sizeof(struct tm));
        return _mkgmtime(&myTm);
    }

#endif

    namespace fnv1a32 {

        constexpr uint32_t offsetBasis = 2166136261U;
        constexpr uint32_t primeNumber = 16777619u;

        constexpr uint32_t digest(const char* str) {
            uint32_t hash = offsetBasis;

            for (uint32_t i = 0; str[i] != '\0'; i++) {
                hash = primeNumber * (hash ^ static_cast<unsigned char>(str[i]));
            }

            return hash;
        }

    }

    class Client {
    public:
        Client() noexcept : handle { curl_easy_init() } {}
        ~Client() noexcept {
            curl_slist_free_all(headers);
            curl_easy_cleanup(handle);
        }

        CURL* handle;
        struct curl_slist* headers = nullptr;

        curl::Factory::postRequestHandler postRequestHandler = nullptr;
        curl::Factory::onErrorHandler onErrorHandler = nullptr;
        curl::Factory::onExceptionHandler onExceptionHandler = nullptr;
        curl::Factory::finalHandler finalHandler = nullptr;

        bool saveCookiesInHeaders_ = false;

        curl::Response response {};
    };

    int64_t getHttpDate(const std::string& httpStringDate) {
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

        struct tm tmpTm {};
        for (const char* format : formats) {
            if (strptime(httpStringDate.c_str(), format, &tmpTm) != nullptr) {
                return timegm(&tmpTm);
            }
        }

        return std::numeric_limits<int64_t>::max();
    }

    std::vector<std::string> splitString(const std::string& str, const std::string& delimiter) {
        if (delimiter.empty()) {
            return {};
        }

        std::vector<std::string> values;
        size_t last = 0;
        size_t next = 0;
        while ((next = str.find(delimiter, last)) != std::string::npos) {
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

    void splitCookie(std::string& cookie, size_t ptr, std::string& name, std::string& value) {
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

    void splitCookie(std::string& cookie, std::string& name) {
        size_t cpos = 0;
        while (cpos < cookie.length() && isspace(cookie[cpos])) {
            ++cpos;
        }

        name = cookie.substr(cpos);
    }

    size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        const size_t res = size * nmemb;
        static_cast<std::string*>(userdata)->append(ptr, res);
        return res;
    }

    size_t headerCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
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

    curl::Cookie splitCookie(const std::string& cookie) {
        curl::Cookie newCookie {};
        std::vector<std::string> values = splitString(cookie, ";");

        for (size_t i = 0; i < values.size(); i++) {
            std::string& coo = values[i];
            std::string name {};
            std::string value {};
            size_t delim_pos = coo.find('=');

            delim_pos != std::string::npos
                ? splitCookie(coo, delim_pos, name, value)
                : splitCookie(coo, name);

            if (i == 0) {
                newCookie.key = name;
                newCookie.value = value;
                continue;
            }

            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) noexcept { return std::tolower(c); });
            switch (fnv1a32::digest(name.c_str())) {
                case fnv1a32::digest("path"): {
                    newCookie.path = value;
                    break;
                }
                case fnv1a32::digest("domain"): {
                    newCookie.domain = value;
                    break;
                }
                case fnv1a32::digest("expires"): {
                    newCookie.expires = getHttpDate(value);
                    break;
                }
                case fnv1a32::digest("secure"): {
                    newCookie.secure = true;
                    break;
                }
                case fnv1a32::digest("httponly"): {
                    newCookie.httpOnly = true;
                    break;
                }
                case fnv1a32::digest("samesite"): {
                    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) noexcept { return std::tolower(c); });
                    switch (fnv1a32::digest(value.c_str())) {
                        case fnv1a32::digest("lax"): {
                            newCookie.sameSite = curl::Cookie::SameSitePolicy::Lax;
                            break;
                        }
                        case fnv1a32::digest("strict"): {
                            newCookie.sameSite = curl::Cookie::SameSitePolicy::Strict;
                            break;
                        }
                        case fnv1a32::digest("none"): {
                            newCookie.sameSite = curl::Cookie::SameSitePolicy::None;
                            break;
                        }
                        default: {
                            break;
                        }
                    }
                    break;
                }
                case fnv1a32::digest("max-age"): {
                    newCookie.maxAge = std::stoll(value);
                    break;
                }
                default: {
                    break;
                }
            }
        }

        return newCookie;
    }
}

namespace curl {

    StatusCode::StatusCode(StatusCode::Values value) noexcept
        : value_(value)
    {}

    StatusCode::StatusCode(uint32_t value) noexcept {
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
                value_ = static_cast<Values>(value);
                return;
            }
            default: {
                value_ = Values::CustomCode;
                return;
            }
        }
    }

    StatusCode::operator std::string() {
        switch (this->value_) {
            case Values::Null: return "Null";
            case Values::Failed: return "Failed";

            case Values::OK: return "OK";
            case Values::Created: return "Created";
            case Values::Accepted: return "Accepted";
            case Values::NonAuthoritativeInformation: return "Non Authoritative Information";
            case Values::NoContent: return "No Content";
            case Values::ResetContent: return "Reset Content";
            case Values::PartialContent: return "Partial Content";

            case Values::MultipleChooses: return "Multiple Chooses";
            case Values::MovedPermanently: return "Moved Permanently";
            case Values::Found: return "Found";
            case Values::SeeOther: return "See Other";
            case Values::NotModified: return "Not Modified";
            case Values::TemporaryRedirect: return "Temporary Redirect";
            case Values::PermamentRedirect: return "Permament Redirect";

            case Values::BadRequest: return "Bad Request";
            case Values::Unauthorized: return "Unauthorized";
            case Values::PaymentRequired: return "Payment Required";
            case Values::Forbidden: return "Forbidden";
            case Values::NotFound: return "Not Found";
            case Values::MethodNotAllowed: return "Method Not Allowed";
            case Values::NotAcceptable: return "Not Acceptable";
            case Values::ProxyRequired: return "Proxy Required";
            case Values::RequestTimeout: return "Request Timeout";
            case Values::Conflict: return "Conflict";
            case Values::Gone: return "Gone";
            case Values::LengthRequired: return "LengthRequired";
            case Values::PreconditionFailed: return "Precondition Failed";
            case Values::PayloadTooLarge: return "Payload Too Large";
            case Values::URLTooLong: return "URL Too Long";
            case Values::UnsupportedMediaType: return "Unsupported Media Type";
            case Values::RangeNotSatisfiable: return "Range Not Satisfiable";
            case Values::ExpectationFailed: return "Expectation Failed";
            case Values::ImATeapot: return "Im A Teapot";
            case Values::UnprocessableEntity: return "Unprocessable Entity";
            case Values::Locked: return "Locked";
            case Values::TooEarly: return "Too Early";
            case Values::UpgradeRequired: return "Upgrade Required";
            case Values::PreconditionRequired: return "Precondition Required";
            case Values::TooManyRequests: return "Too Many Requests";
            case Values::RequestHeaderFieldsTooLarge: return "Request Header Fields Too Large";
            case Values::UnavailableForLegalReasons: return "Unavailable For Legal Reasons";

            case Values::InternalServerError: return "Internal Server Error";
            case Values::NotImplemented: return "Not Implemented";
            case Values::BadGateway: return "Bad Gateway";
            case Values::ServiceUnavailable: return "Service Unavailable";
            case Values::GatewayTimeout: return "Gateway Timeout";
            case Values::VersionNotSupported: return "Version Not Supported";
            case Values::VariantAlsoNegotiates: return "Variant Also Negotiates";
            case Values::InsufficientStorage: return "Insufficient Storage";
            case Values::LoopDetected: return "Loop Detected";
            case Values::NotExtended: return "Not Extended";
            case Values::NetworkAuthRequired: return "Network Auth Required";

            default: return "Custom";
        }

        return "Custom";
    }

    StatusCode::operator uint32_t() noexcept {
        return static_cast<uint32_t>(value_);
    }

    StatusCode::operator Values() noexcept {
        return value_;
    }

    bool Response::saveToFile(const std::string& filename, bool overwrite) const noexcept {
        if (!overwrite) {
            std::unique_ptr<FILE, decltype(&fclose)> file { fopen(filename.c_str(), "r"), &fclose };
            if (file) {
                return false;
            }
        }

        std::ofstream output { filename, std::ios::binary };
        if (output.bad()) {
            return false;
        }

        output << this->body;
        output.flush();

        return output.good();
    }

    Factory::Builder::Builder(const std::string& host)
        : host_ { host }
    {
        if (host_.empty()) {
            throw std::runtime_error("Host must be not empty.");
        }

        const size_t headerPos = host_.find("://");
        if (headerPos == std::string::npos) {
            throw std::runtime_error("Host must be valid link (http://example.com)");
        }

        size_t pathPos = host_.find('/', headerPos + 3);
        if (pathPos == std::string::npos) {
            const size_t queryPos = host_.find('?', headerPos + 3);
            if (queryPos != std::string::npos && queryPos < pathPos) {
                pathPos = queryPos;
            }
        }

        host_ = host_.substr(0, pathPos);
    }

    Factory::Builder& Factory::Builder::reset() noexcept {
        type_ = RequestType::GET;

        path_.clear();
        query_.clear();
        body_.clear();

        cookies_.clear();
        headers_.clear();

        referer_.clear();
        userAgent_ = "curler/1.0";

        followRedirects_ = true;
        saveCookiesInHeaders_ = false;

        preRequestCallback_ = nullptr;
        postRequestCallback_ = nullptr;
        onErrorHandler_ = nullptr;
        onExceptionHandler_ = nullptr;
        finalHandler_ = nullptr;

        return *this;
    }

    Factory::Builder& Factory::Builder::setRequestType(RequestType type) noexcept {
        type_ = type;
        return *this;
    }

    Factory::Builder& Factory::Builder::setPath(const std::string& path) {
        if (path.empty()) {
            path_.clear();
            return *this;
        }

        const bool withSlash = path[0] == '/';
        const size_t queryPos = path.find('?');

        if (queryPos == std::string::npos) {
            path_ = withSlash ? path : '/' + path;
            return *this;
        }

        const std::string pathWithoutQuery = path.substr(0, queryPos);
        path_ = withSlash ? pathWithoutQuery : '/' + pathWithoutQuery;

        return *this;
    }

    Factory::Builder& Factory::Builder::setParameter(const std::string& key, const std::string& value) {
        static_cast<void>(query_.insert({ key, value }));
        return *this;
    }

    Factory::Builder& Factory::Builder::setBody(const std::string& body) {
        body_ = body;
        return *this;
    }

    Factory::Builder& Factory::Builder::setBody(std::string&& body) noexcept {
        body_ = std::move(body);
        return *this;
    }

    Factory::Builder& Factory::Builder::addHeader(const std::string& key, const std::string& value) {
        static_cast<void>(headers_.insert({ key, value }));
        return *this;
    }

    Factory::Builder& Factory::Builder::addCookie(const std::string& key, const std::string& value) {
        static_cast<void>(cookies_.insert({ key, value }));
        return *this;
    }

    Factory::Builder& Factory::Builder::resetHeaders() noexcept {
        headers_.clear();
        return *this;
    }

    Factory::Builder& Factory::Builder::resetCookies() noexcept {
        cookies_.clear();
        return *this;
    }

    Factory::Builder& Factory::Builder::setReferer(const std::string& referer) {
        referer_ = referer;
        return *this;
    }

    Factory::Builder& Factory::Builder::setUserAgent(const std::string& agent) {
        userAgent_ = agent;
        return *this;
    }

    Factory::Builder& Factory::Builder::followRedirects(bool state) noexcept {
        followRedirects_ = state;
        return *this;
    }

    Factory::Builder& Factory::Builder::saveCookiesInHeaders(bool state) noexcept {
        saveCookiesInHeaders_ = state;
        return *this;
    }

    Factory::Builder& Factory::Builder::preRequest(preRequestHandler&& callback) noexcept {
        preRequestCallback_ = std::move(callback);
        return *this;
    }

    Factory::Builder& Factory::Builder::onComplete(postRequestHandler&& callback) noexcept {
        postRequestCallback_ = std::move(callback);
        return *this;
    }

    Factory::Builder& Factory::Builder::onError(onErrorHandler&& callback) noexcept {
        onErrorHandler_ = std::move(callback);
        return *this;
    }

    Factory::Builder& Factory::Builder::onException(onExceptionHandler&& callback) noexcept {
        onExceptionHandler_ = std::move(callback);
        return *this;
    }

    Factory::Builder& Factory::Builder::onDestroy(finalHandler&& callback) noexcept {
        finalHandler_ = std::move(callback);
        return *this;
    }

    Factory::Builder& Factory::Builder::resetCallbacks() noexcept {
        preRequestCallback_ = nullptr;
        postRequestCallback_ = nullptr;
        onErrorHandler_ = nullptr;
        onExceptionHandler_ = nullptr;
        finalHandler_ = nullptr;

        return *this;
    }

    std::string utils::urlEncode(const std::string& src) {
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
                result.append(charToHex(*iter));
                break;
            }
        }

        return result;
    }

    std::string utils::urlDecode(const std::string& src) {
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

    std::string utils::charToHex(char c) {
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

    Factory::Factory(long maxAmountOfConcurrentConnections, long maxConnectionTimeoutInMilliseconds)
        : maxConnectionTimeout_ { maxConnectionTimeoutInMilliseconds > 0 ? maxConnectionTimeoutInMilliseconds : 0 }
    {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        handle_ = static_cast<void*>(curl_multi_init());
        curl_multi_setopt(static_cast<CURLM*>(handle_), CURLMOPT_MAX_TOTAL_CONNECTIONS, maxAmountOfConcurrentConnections > 0 ? maxAmountOfConcurrentConnections : 0);

        thread_ = std::thread(&Factory::runFactory, this);
    }

    Factory::~Factory() noexcept {
        destructorCalled_ = true;
        cv_.notify_one();

        try {
            thread_.join();
        }
        catch (const std::system_error&) {
            /* ignored, thread already exited */
        }

        curl_multi_cleanup(static_cast<CURLM*>(handle_));
        curl_global_cleanup();
    }

    Factory::Builder Factory::createRequest(const std::string& host) {
        return { host };
    }

    void Factory::pushRequest(const Builder& builder) {
        detail::Client* client = new detail::Client();
        std::string url = builder.host_ + builder.path_;

        if (builder.query_.size() > 0) {
            url.push_back('?');

            for (const std::pair<std::string, std::string>& pair : builder.query_) {
                url.append(utils::urlEncode(pair.first)).push_back('=');
                url.append(utils::urlEncode(pair.second)).push_back('&');
            }

            url.pop_back();
        }

        if (builder.cookies_.size() > 0) {
            std::string cookie_header {};
            for (const std::pair<std::string, std::string>& pair : builder.cookies_) {
                cookie_header.append(pair.first).push_back('=');
                cookie_header.append(pair.second).push_back(';');
            }

            cookie_header.pop_back();
            curl_easy_setopt(client->handle, CURLOPT_COOKIE, cookie_header.c_str());
        }

        if (builder.headers_.size() > 0) {
            for (const std::pair<std::string, std::string>& pair : builder.headers_) {
                const std::string head = pair.first + ": " + pair.second;
                client->headers = curl_slist_append(client->headers, head.c_str());
            }

            curl_easy_setopt(client->handle, CURLOPT_HTTPHEADER, client->headers);
        }

        curl_easy_setopt(client->handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(client->handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

        if (!builder.referer_.empty()) {
            curl_easy_setopt(client->handle, CURLOPT_REFERER, builder.referer_.c_str());
        }

        curl_easy_setopt(client->handle, CURLOPT_USERAGENT, builder.userAgent_.empty() ? "curler/1.0" : builder.userAgent_.c_str());

        if (builder.followRedirects_) {
            curl_easy_setopt(client->handle, CURLOPT_FOLLOWLOCATION, 1L);
        }

        curl_easy_setopt(client->handle, CURLOPT_WRITEFUNCTION, &detail::writeCallback);
        curl_easy_setopt(client->handle, CURLOPT_WRITEDATA, &client->response.body);
        curl_easy_setopt(client->handle, CURLOPT_HEADERFUNCTION, &detail::headerCallback);
        curl_easy_setopt(client->handle, CURLOPT_HEADERDATA, &client->response.headers);
        curl_easy_setopt(client->handle, CURLOPT_TIMEOUT_MS, maxConnectionTimeout_);

        switch (builder.type_) {
            default:
            case RequestType::GET: {
                break;
            }
            case RequestType::HEAD: {
                // https://curl.se/mail/lib-2007-10/0025.html
                curl_easy_setopt(client->handle, CURLOPT_HEADER, 1L);
                curl_easy_setopt(client->handle, CURLOPT_NOBODY, 1L);
                break;
            }
            case RequestType::POST: {
                curl_easy_setopt(client->handle, CURLOPT_POST, 1L);
                curl_easy_setopt(client->handle, CURLOPT_POSTFIELDSIZE_LARGE, builder.body_.size());
                curl_easy_setopt(client->handle, CURLOPT_COPYPOSTFIELDS, builder.body_.data());
                break;
            }
            case RequestType::PUT: {
                // https://stackoverflow.com/a/7570281
                curl_easy_setopt(client->handle, CURLOPT_CUSTOMREQUEST, "PUT");
                curl_easy_setopt(client->handle, CURLOPT_POSTFIELDSIZE_LARGE, builder.body_.size());
                curl_easy_setopt(client->handle, CURLOPT_COPYPOSTFIELDS, builder.body_.data());
                break;
            }
            case RequestType::DELETE: {
                // https://stackoverflow.com/a/34751940
                curl_easy_setopt(client->handle, CURLOPT_CUSTOMREQUEST, "DELETE");
                curl_easy_setopt(client->handle, CURLOPT_POSTFIELDSIZE_LARGE, builder.body_.size());
                curl_easy_setopt(client->handle, CURLOPT_COPYPOSTFIELDS, builder.body_.data());
                break;
            }
            case RequestType::PATCH: {
                // https://curl.se/mail/lib-2016-08/0111.html
                curl_easy_setopt(client->handle, CURLOPT_CUSTOMREQUEST, "PATCH");
                curl_easy_setopt(client->handle, CURLOPT_POSTFIELDSIZE_LARGE, builder.body_.size());
                curl_easy_setopt(client->handle, CURLOPT_COPYPOSTFIELDS, builder.body_.data());
                break;
            }
        }

        if (builder.preRequestCallback_) {
            try {
                builder.preRequestCallback_(builder, url);
            }
            catch (...) {
                try {
                    if (builder.onExceptionHandler_) {
                        builder.onExceptionHandler_(ExceptionType::OnPreRequest, std::current_exception());
                    }
                }
                catch (...) { /* ignored */ }
            }
        }

        client->postRequestHandler = builder.postRequestCallback_;
        client->onErrorHandler = builder.onErrorHandler_;
        client->onExceptionHandler = builder.onExceptionHandler_;
        client->finalHandler = builder.finalHandler_;

        client->response.type = builder.type_;
        client->saveCookiesInHeaders_ = builder.saveCookiesInHeaders_;

        curl_easy_setopt(client->handle, CURLOPT_PRIVATE, client);
        curl_multi_add_handle(static_cast<CURLM*>(handle_), client->handle);

        currentAmountOfRequests_++;
        cv_.notify_one();
    }

    void Factory::runFactory() {
        int runningHandles = 0;
        int messagesLeft = 0;

        while (!destructorCalled_ || currentAmountOfRequests_.load() > 0) {
            std::unique_lock<std::mutex> lock { requestMutex_ };
            cv_.wait(lock, [&]() noexcept { return destructorCalled_ || currentAmountOfRequests_.load() > 0; });

            static_cast<void>(curl_multi_perform(static_cast<CURLM*>(handle_), &runningHandles));

            CURLMsg* message = nullptr;
            while ((message = curl_multi_info_read(static_cast<CURLM*>(handle_), &messagesLeft))) {
                detail::Client* client = nullptr;
                curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &client);

                if (message->msg == CURLMSG_DONE) {
                    if (message->data.result == CURLE_OK) {
                        if (client->postRequestHandler) {
                            const auto range = client->response.headers.equal_range("set-cookie");
                            for (auto it = range.first; it != range.second; it++) {
                                Cookie newCookie = detail::splitCookie(it->second);
                                client->response.cookies.insert({ newCookie.key, newCookie });
                            }

                            if (!client->saveCookiesInHeaders_) {
                                client->response.headers.erase("set-cookie");
                            }

                            long sc = 0;
                            curl_easy_getinfo(client->handle, CURLINFO_RESPONSE_CODE, &sc);
                            client->response.code = sc;

                            try {
                                client->postRequestHandler(client->response);
                            }
                            catch (...) {
                                try {
                                    if (client->onExceptionHandler) {
                                        client->onExceptionHandler(ExceptionType::OnPostRequest, std::current_exception());
                                    }
                                }
                                catch (...) { /* ignored */ }
                            }
                        }
                    }
                    else if (client->onErrorHandler) {
                        client->response.code = StatusCode::Values::Failed;
                        client->response.error = curl_easy_strerror(message->data.result);

                        try {
                            client->onErrorHandler(client->response);
                        }
                        catch (...) {
                            try {
                                if (client->onExceptionHandler) {
                                    client->onExceptionHandler(ExceptionType::OnError, std::current_exception());
                                }
                            }
                            catch (...) { /* ignored */ }
                        }
                    }
                }
                else if (client->onErrorHandler) {
                    client->response.code = StatusCode::Values::Failed;
                    client->response.error = "Something went wrong inside curl_multi_perform so it's doesn't returned CURLMSG_DONE.";

                    try {
                        client->onErrorHandler(client->response);
                    }
                    catch (...) {
                        try {
                            if (client->onExceptionHandler) {
                                client->onExceptionHandler(ExceptionType::OnError, std::current_exception());
                            }
                        }
                        catch (...) { /* ignored */ }
                    }
                }

                try {
                    if (client->finalHandler) {
                        client->finalHandler();
                    }
                }
                catch (...) { /* ignored */ }

                curl_multi_remove_handle(static_cast<CURLM*>(handle_), client->handle);
                delete client;
                currentAmountOfRequests_--;
            }

            if (runningHandles > 0) {
                static_cast<void>(curl_multi_wait(static_cast<CURLM*>(handle_), nullptr, 0, 1000, nullptr));
            }
        }
    }

}