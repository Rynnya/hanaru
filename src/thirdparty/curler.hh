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

#ifndef CURL_ER_GUARD_08_10_2022_
#define CURL_ER_GUARD_08_10_2022_

#include <atomic> // std::atomic_bool, std::atomic_size_t
#include <condition_variable> // std::condition_variable, std::mutex
#include <functional> // std::function
#include <string> // std::string
#include <thread> // std::thread
#include <unordered_map> // std::unordered_map, std::unordered_multimap

#ifndef NOMINMAX
#   define NOMINMAX
#endif

#ifdef DELETE
#   undef DELETE
#endif

namespace curl {

    // Class that represents all values that declared as standard.
    // Can be moved (noexcept) and copied (noexcept)
    class StatusCode {
    public:
        enum class Values : uint32_t {
            Null = 0,
            Failed = 1,

            OK = 200,
            Created = 201,
            Accepted = 202,
            NonAuthoritativeInformation = 203,
            NoContent = 204,
            ResetContent = 205,
            PartialContent = 206,

            MultipleChooses = 300,
            MovedPermanently = 301,
            Found = 302,
            SeeOther = 303,
            NotModified = 304,
            TemporaryRedirect = 307,
            PermamentRedirect = 308,

            BadRequest = 400,
            Unauthorized = 401,
            PaymentRequired = 402,
            Forbidden = 403,
            NotFound = 404,
            MethodNotAllowed = 405,
            NotAcceptable = 406,
            ProxyRequired = 407,
            RequestTimeout = 408,
            Conflict = 409,
            Gone = 410,
            LengthRequired = 411,
            PreconditionFailed = 412,
            PayloadTooLarge = 413,
            URLTooLong = 414,
            UnsupportedMediaType = 415,
            RangeNotSatisfiable = 416,
            ExpectationFailed = 417,
            ImATeapot = 418, // yes you are
            UnprocessableEntity = 422,
            Locked = 423,
            TooEarly = 425,
            UpgradeRequired = 426,
            PreconditionRequired = 428,
            TooManyRequests = 429,
            RequestHeaderFieldsTooLarge = 431,
            UnavailableForLegalReasons = 451,

            InternalServerError = 500,
            NotImplemented = 501,
            BadGateway = 502,
            ServiceUnavailable = 503,
            GatewayTimeout = 504,
            VersionNotSupported = 505,
            VariantAlsoNegotiates = 506,
            InsufficientStorage = 507,
            LoopDetected = 508,
            NotExtended = 510,
            NetworkAuthRequired = 511,

            CustomCode = 999
        };

        StatusCode(StatusCode::Values value) noexcept;
        StatusCode(uint32_t value) noexcept;

        StatusCode(const StatusCode&) noexcept = default;
        StatusCode(StatusCode&&) noexcept = default;
        StatusCode& operator=(const StatusCode&) noexcept = default;
        StatusCode& operator=(StatusCode&&) noexcept = default;

        explicit operator std::string();
        explicit operator uint32_t() noexcept;
        operator Values() noexcept;

    private:
        StatusCode::Values value_ = Values::Null;
    };

    // Request types that can be used in this library.
    enum class RequestType : uint8_t {
        GET = 0,
        HEAD = 1,
        POST = 2,
        PUT = 3,
        DELETE = 4,
        PATCH = 5
    };

    // Exceptions that can happend when request invoked.
    enum class ExceptionType : uint8_t {
        OnError = 0,
        OnPreRequest = 1,
        OnPostRequest = 2
    };

    // Class that represents a cookie information.
    // Can be moved (noexcept) and copied.
    class Cookie {
    public:
        Cookie() noexcept = default;

        Cookie(const Cookie&) = default;
        Cookie(Cookie&&) noexcept = default;
        Cookie& operator=(const Cookie&) = default;
        Cookie& operator=(Cookie&&) noexcept = default;

        enum class SameSitePolicy : uint8_t {
            None = 0,
            Lax = 1,
            Strict = 2
        };

        std::string key {};
        std::string value {};
        int64_t expires = -1;
        int64_t maxAge = -1;
        std::string path {};
        std::string domain {};
        bool httpOnly = false;
        bool secure = false;
        SameSitePolicy sameSite = SameSitePolicy::None;
    };

    // Class that contains all information about response.
    // Can be moved (noexcept) and copied.
    class Response {
    public:
        Response() noexcept = default;
        ~Response() noexcept = default;

        Response(const Response&) = default;
        Response(Response&&) noexcept = default;
        Response& operator=(const Response&) = default;
        Response& operator=(Response&&) noexcept = default;

        // Writes whole body object into file.
        // Returns:
        //  - True if everything was written correctly.
        //  - False if something went wrong.
        //  - False if overwrite was true and non-empty file was open.
        bool saveToFile(const std::string& filename, bool overwrite = false) const noexcept;

        StatusCode code = StatusCode::Values::Null;
        RequestType type = RequestType::GET;
        std::unordered_multimap<std::string, std::string> headers {};
        std::unordered_multimap<std::string, Cookie> cookies {};
        std::string body {};

        // Will be empty if no error occurred.
        std::string error {};
    };

    // Some useful utilities for encoding and decoding data.
    class Utils {
    public:
        static std::string urlEncode(const std::string& src);
        static std::string urlDecode(const std::string& src);

        static std::string charToHex(char c);
    };

    // Main class of library, handles all incoming requests and outcoming responses.
    // Exception specification 'None' doesn't mean that this function is exception-safe.
    // It's means that library will produce no exceptions by itself.
    // Allocations and internal functions can throw exceptions.
    // Only functions, declared as 'noexcept' cannot throw exceptions.
    // Cannot be moved or copied.
    class Factory {
    public:
        class Builder;

        typedef std::function<void(const Builder&)> preRequestHandler;
        typedef std::function<void(Response&)> postRequestHandler;
        typedef std::function<void(Response&)> onErrorHandler;
        typedef std::function<void(ExceptionType, std::exception_ptr)> onExceptionHandler;
        typedef std::function<void()> finalHandler;

        // Class that represents a blueprint for request.
        // Can be moved (noexcept) and cannot be copied.
        // Cannot be constructed outside of Factory.
        class Builder {
            friend class Factory;

            Builder(const std::string& host);

        public:

            ~Builder() noexcept = default;
            Builder& reset() noexcept;

            Builder(const Builder&) = delete;
            Builder(Builder&&) noexcept = default;
            Builder& operator=(const Builder&) = delete;
            Builder& operator=(Builder&&) noexcept = default;

            Builder& setRequestType(RequestType type) noexcept;
            Builder& setPath(const std::string& path);
            Builder& setParameter(const std::string& key, const std::string& value);

            Builder& setBody(const std::string& body);
            Builder& setBody(std::string&& body) noexcept;

            Builder& addHeader(const std::string& key, const std::string& value);
            Builder& addCookie(const std::string& key, const std::string& value);

            // This also resets referer and user-agent headers.
            Builder& resetHeaders() noexcept;
            Builder& resetCookies() noexcept;
            Builder& resetReferer() noexcept;
            Builder& resetUserAgent() noexcept;

            // Have the same effect as calling 'addHeader("Referer", referer);'
            Builder& setReferer(const std::string& referer);
            // Have the same effect as calling 'addHeader("User-Agent", agent);'
            Builder& setUserAgent(const std::string& agent);
            Builder& followRedirects(bool state = true) noexcept;
            Builder& saveCookiesInHeaders(bool state = false) noexcept;

            // Called before adding handle into query.
            // If this callback throws an exception, then whole request will be rejected immediately.
            Builder& preRequest(preRequestHandler&& callback) noexcept;
            // Called when request fully done, contains result of request.
            Builder& onComplete(postRequestHandler&& callback) noexcept;
            // Called when error happend inside curl and request cannot be fully performed.
            Builder& onError(onErrorHandler&& callback) noexcept;
            // Called when any of your callbacks (except final) throws exception.
            Builder& onException(onExceptionHandler&& callback) noexcept;
            // Called when request is destroyed.
            Builder& onDestroy(finalHandler&& callback) noexcept;

            // Resets all callbacks to nullptr.
            Builder& resetCallbacks() noexcept;

        private:
            RequestType type_ = RequestType::GET;
            std::string host_;
            std::string path_ {};
            std::unordered_map<std::string, std::string> query_ {};
            std::string body_ {};

            std::unordered_multimap<std::string, std::string> cookies_ {};
            std::unordered_multimap<std::string, std::string> headers_ {};

            std::string referer_ {};
            std::string userAgent_ {};

            bool followRedirects_ = true;
            bool saveCookiesInHeaders_ = false;

            preRequestHandler preRequestCallback_ = nullptr;
            postRequestHandler postRequestCallback_ = nullptr;
            onErrorHandler onErrorHandler_ = nullptr;
            onExceptionHandler onExceptionHandler_ = nullptr;
            finalHandler finalHandler_ = nullptr;
        };

        // Creates a factory which will create a thread for curl and allows you to create a builders.
        // This constructor is NOT thread-safe (unless curl 7.84.0+ is used and CURL_VERSION_THREADSAFE bit is set)
        // Exceptions:
        //  - std::system_error from std::thread::thread (ctor)
        Factory(long maxAmountOfConcurrentConnections = 0, long maxConnectionTimeoutInMilliseconds = 0);
        // Destructor for Factory. Current requests will be completed and all resources will be freed before destructor is returned.
        // The destructor is NOT thread-safe (unless curl 7.84.0+ is used and CURL_VERSION_THREADSAFE bit is set)
        // Exceptions:
        //  - None (std::system_error from std::thread::join is handled)
        ~Factory() noexcept;

        Factory(const Factory&) = delete;
        Factory(Factory&&) = delete;
        Factory& operator=(const Factory&) = delete;
        Factory& operator=(Factory&&) = delete;

        // Create a builder which can be used as blueprint for requests.
        // Can be used multiple times.
        // This method is thread-safe.
        // Exceptions:
        //  - std::runtime_error if host is empty or invalid
        Builder createRequest(const std::string& host);
        // Performs a request using provided blueprint.
        // This method is thread-safe.
        // Exceptions:
        //  - None
        void pushRequest(const Builder& builder);
        // Performs a synchronous request using provided blueprint.
        // User-defined callbacks WILL BE IGNORED and you will receive raw response.
        // This method is thread-safe.
        // Exceptions:
        //  - None
        Response syncRequest(const Builder& builder);

    private:
        class Client;

        std::unique_ptr<Client> createClient(const Builder& builder);
        void runFactory();

        void* handle_ = nullptr;
        long maxConnectionTimeout_ = 0;

        std::thread thread_;
        std::mutex requestMutex_ {};
        std::condition_variable cv_ {};
        std::atomic_size_t currentAmountOfRequests_ { 0 };
        std::atomic_bool destructorCalled_ { false };
    };

    typedef Factory::Builder Builder;

}

#endif // CURL_ER_GUARD_08_10_2022_
