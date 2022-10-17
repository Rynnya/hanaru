#include "authorization.hh"

#include "../thirdparty/curler.hh"
#include "utils.hh"

hanaru::Authorization::Authorization() : valid_(false) {}

hanaru::Authorization::Authorization(const std::string& username, const std::string& password)
    : username_ { username }
    , password_ { password }
    , valid_ { false }
{
    auth();
}

hanaru::Authorization::~Authorization() {
    deAuth();
}

hanaru::Authorization::Authorization(Authorization&& other) noexcept
    : username_ { std::move(other.username_) }
    , password_ { std::move(other.password_) }
    , xsrfToken_ { std::move(other.xsrfToken_) }
    , sessionToken_ { std::move(other.sessionToken_) }
    , valid_ { other.valid_ }
{
    other.valid_ = false;
}

hanaru::Authorization& hanaru::Authorization::operator=(Authorization&& other) noexcept {
    std::swap(this->username_, other.username_);
    std::swap(this->password_, other.password_);
    std::swap(this->xsrfToken_, other.xsrfToken_);
    std::swap(this->sessionToken_, other.sessionToken_);
    
    this->valid_ = other.valid_;
    other.valid_ = false;

    return *this;
}

void hanaru::Authorization::reAuth() {
    deAuth();
    auth();
}

void hanaru::Authorization::deAuth() {
    if (!valid_ || sessionToken_.empty() || xsrfToken_.empty()) {
        return;
    }

    curl::client client("https://osu.ppy.sh");

    client.set_path("/session");
    client.set_referer("https://osu.ppy.sh/home");

    client.add_header("Origin", "https://osu.ppy.sh");
    client.add_header("Alt-Used", "osu.ppy.sh");
    client.add_header("X-CSRF-Token", xsrfToken_);

    client.add_cookie("XSRF-TOKEN", xsrfToken_);
    client.add_cookie("osu_session", sessionToken_);

    client.del();
    xsrfToken_.clear();
    sessionToken_.clear();
}

std::string hanaru::Authorization::xsrf() const {
    return xsrfToken_;
}

std::string hanaru::Authorization::session() const {
    return sessionToken_;
}

bool hanaru::Authorization::isValid() const {
    return valid_;
}

void hanaru::Authorization::auth() {
    curl::client client("https://osu.ppy.sh");
    client.set_user_agent(HANARU_USER_AGENT);
    client.set_path("/home");

    curl::response session_request = client.get();

    if (session_request.code != curl::status_code::values::ok) {
        return;
    }

    for (const auto [_, cookie] : session_request.cookies) {
        client.add_cookie(cookie.key, cookie.value);

        if (cookie.key == "XSRF-TOKEN") {
            xsrf_token_ = cookie.value;
        }
    }

    client.set_path("/session");
    client.set_referer("https://osu.ppy.sh/home");

    client.add_header("Origin", "https://osu.ppy.sh");
    client.add_header("Alt-Used", "osu.ppy.sh");
    client.add_header("Content-Type", "application/x-www-form-urlencoded; charset=UTF-8");
    client.add_header("X-CSRF-Token", xsrf_token_);

    curl::response login_request = client.post(
        "_token=" + curl::utils::url_encode(xsrf_token_) +
        "&username=" + curl::utils::url_encode(username_) +
        "&password=" + curl::utils::url_encode(password_)
    );

    if (login_request.code != curl::status_code::values::ok) {
        return;
    }

    client.reset_cookies();
    client.reset_headers();

    for (const auto [_, cookie] : login_request.cookies) {
        if (cookie.domain == ".ppy.sh") {
            if (cookie.key == "XSRF-TOKEN") {
                xsrf_token_ = cookie.value;
            }

            if (cookie.key == "osu_session") {
                session_token_ = cookie.value;
            }
        }
    }

    valid_ = true;
}
