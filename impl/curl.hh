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

#ifndef CURL_ER_GUARD_
#define CURL_ER_GUARD_

#ifndef NOMINMAX
#   define NOMINMAX
#endif

#include <functional>
#include <string>
#include <unordered_map>
#include <memory>

#include <curl/curl.h>

namespace curl {

    class status_code {
    public:
        enum class values : uint32_t {
            null = 0,
            failed = 1,

            ok = 200,
            created = 201,
            accepted = 202,
            non_authoritative_information = 203,
            no_content = 204,
            reset_content = 205,
            partial_content = 206,

            multiple_chooses = 300,
            moved_permanently = 301,
            found = 302,
            see_other = 303,
            not_modified = 304,
            temporary_redirect = 307,
            permament_redirect = 308,

            bad_request = 400,
            unauthorized = 401,
            payment_required = 402,
            forbidden = 403,
            not_found = 404,
            method_not_allowed = 405,
            not_acceptable = 406,
            proxy_required = 407,
            request_timeout = 408,
            conflict = 409,
            gone = 410,
            length_required = 411,
            precondition_failed = 412,
            payload_too_large = 413,
            url_too_long = 414,
            unsupported_media_type = 415,
            range_not_satisfiable = 416,
            expectation_failed = 417,
            im_a_teapot = 418, // yes you are
            unprocessable_entity = 422,
            locked = 423,
            too_early = 425,
            upgrade_required = 426,
            precondition_required = 428,
            too_many_requests = 429,
            request_header_fields_too_large = 431,
            unavailable_for_legal_reasons = 451,

            internal_server_error = 500,
            not_implemented = 501,
            bad_gateway = 502,
            service_unavailable = 503,
            gateway_timeout = 504,
            version_not_supported = 505,
            variant_also_negotiates = 506,
            insufficient_storage = 507,
            loop_detected = 508,
            not_extended = 510,
            network_auth_required = 511,

            custom_code = 999
        };

        status_code(status_code::values value) noexcept;
        status_code(uint32_t value) noexcept;

        explicit operator std::string();
        explicit operator uint32_t() noexcept;
        operator values() noexcept;

    private:
        status_code::values value_ = values::null;
    };

    enum class request_type : uint8_t {
        get = 0,
        head = 1,
        post = 2,
        put = 3,
        del = 4, // delete is reserved word
        patch = 5
    };

    // forward-declaration
    class client;

    class cookie {
    public:
        cookie() noexcept = default;

        enum class same_site_policy : uint8_t {
            none = 0,
            lax = 1,
            strict = 2
        };

        std::string key {};
        std::string value {};
        int64_t expires = -1;
        int64_t max_age = -1;
        std::string path {};
        std::string domain {};
        bool http_only = false;
        bool secure = false;
        same_site_policy same_site = same_site_policy::none;
    };

    class response {
        friend class curl::client;
    public:
        response() noexcept = default;
        ~response() noexcept = default;

        response(const response&) = default;
        response(response&&) noexcept = default;
        response& operator=(const response&) = default;
        response& operator=(response&&) noexcept = default;

        // Returns true if write was successfully, otherwise false (or if overwrite was false and file already exist)
        bool save_to_file(const std::string& filename, bool overwrite = false);

        status_code::values code = status_code::values::null;
        request_type type = request_type::get;
        std::unordered_multimap<std::string, std::string> headers {};
        std::unordered_multimap<std::string, cookie> cookies {};
        std::string body {};
    };

    class client {
    public:
        client(const std::string& host);
        ~client() noexcept;

        // Cannot be moved or copied
        client(const client&) = delete;
        client(client&&) = delete;
        client& operator=(const client&) = delete;
        client& operator=(client&&) = delete;

        void set_path(const std::string& path);
        void set_parameter(const std::string& key, const std::string& value);

        void add_header(const std::string& key, const std::string& value);
        void add_cookie(const std::string& key, const std::string& value);

        void reset_headers() noexcept;
        void reset_cookies() noexcept;

        // Set's Referer header
        void set_referer(const std::string& referer) noexcept;
        // Set's User-Agent header
        void set_user_agent(const std::string& agent);
        // Should follow redirects? Default - true
        void follow_redirects(bool state = true) noexcept;
        // Will headers in response contains any of Set-Cookie headers? Default - false
        void save_cookies_in_headers(bool state = false) noexcept;

        // Request perform functions

        response get();
        response head();
        response post(const std::string& body = "");
        response put(const std::string& body = "");
        response del(const std::string& body = "");
        response patch(const std::string& body = "");

        typedef std::function<void(const std::string&)> pre_request_handler;
        // Called before request was performed, because of this contains only URL string, everything else can be received through client
        void pre_request(pre_request_handler&& callback) noexcept;

    private:
        void prepare(request_type type, response& resp);
        void on_done(response& resp);
        void on_error(CURLcode code, response& resp);

        std::string host_ {};
        std::string path_ {};
        std::unordered_map<std::string, std::string> query_ {};

        std::unordered_multimap<std::string, std::string> cookies_ {};
        std::unordered_multimap<std::string, std::string> headers_ {};

        std::string user_agent_ {};
        bool follow_redirects_ = true;
        bool save_cookies_in_headers_ = false;

        pre_request_handler pre_request_callback_ = nullptr;

        CURL* handle_ = nullptr;
        struct curl_slist* headers_list_ = nullptr;
    };

    class utils {
    public:
        static std::string url_encode(const std::string& src);
        static std::string url_decode(const std::string& src);

        static std::string char_to_hex(char c);
    };

}

#endif
