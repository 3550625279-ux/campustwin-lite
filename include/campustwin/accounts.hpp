#pragma once

#include "campustwin/contracts.hpp"
#include <map>

namespace campustwin {

// Local course-demo accounts only. This does not change the shared simulation API.
class AccountStore {
public:
    explicit AccountStore(const std::filesystem::path& file);
    void register_user(const std::string& username, const std::string& password);
    bool login(const std::string& username, const std::string& password) const;
    bool contains(const std::string& username) const;

private:
    struct Record {
        std::string salt_hex;
        std::uint32_t iterations{200000};
        std::string digest_hex;
    };
    std::filesystem::path file_;
    std::map<std::string, Record> records_;
    void save(const std::map<std::string, Record>& records) const;
};

}  // namespace campustwin
