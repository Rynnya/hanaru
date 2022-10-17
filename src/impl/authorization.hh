#pragma once

#include <string>

namespace hanaru {

    class Authorization {
    public:
        Authorization();
        Authorization(const std::string& username, const std::string& password);
        ~Authorization();

        Authorization(const Authorization& other) noexcept = delete;
        Authorization& operator=(const Authorization& other) noexcept = delete;
        Authorization(Authorization&& other) noexcept;
        Authorization& operator=(Authorization&& other) noexcept;

        void reAuth();
        void deAuth();

        std::string xsrf() const;
        std::string session() const;

        bool isValid() const;

    private:
        void auth();

        std::string username_ {};
        std::string password_ {};

        std::string xsrfToken_ {};
        std::string sessionToken_ {};
        bool valid_;
    };
}