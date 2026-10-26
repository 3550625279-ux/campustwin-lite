#include "campustwin/accounts.hpp"

#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

namespace campustwin::account_detail {
std::string sha256_hex(const std::string& input);
std::string hmac_sha256_hex(const std::string& key_hex, const std::string& message);
std::string pbkdf2_hex(const std::string& password, const std::string& salt_hex,
                       std::uint32_t iterations, std::size_t output_bytes);
}
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Action> void rejected(Action action, const char* message) {
    bool caught = false;
    try { action(); } catch (const campustwin::ValidationError&) { caught = true; }
    require(caught, message);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}
void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    require(static_cast<bool>(output), "test file write failed");
}
}

int main() {
    namespace fs = std::filesystem;
    using campustwin::AccountStore;
    using namespace campustwin::account_detail;
    fs::path temporary;
    try {
        // SHA-256 example "abc", NIST SHA256.pdf, final digest:
        // https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/SHA256.pdf
        require(sha256_hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "NIST SHA-256 known-answer test failed");
        // RFC 4231 section 4.2 test case 1, 20 bytes of 0x0b as the HMAC key.
        // https://www.rfc-editor.org/rfc/rfc4231#section-4.2
        require(hmac_sha256_hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b", "Hi There") ==
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
            "RFC 4231 HMAC-SHA-256 known-answer test failed");
        // RFC 4231 section 4.7 test case 6 also exercises hashing a long HMAC key.
        require(hmac_sha256_hex(std::string(262, 'a'),
            "Test Using Larger Than Block-Size Key - Hash Key First") ==
            "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
            "RFC 4231 long-key HMAC known-answer test failed");
        // RFC 7914 section 11, full 64-byte PBKDF2-HMAC-SHA-256 vector.
        // https://www.rfc-editor.org/rfc/rfc7914#section-11
        require(pbkdf2_hex("passwd", "73616c74", 1, 64) ==
            "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc4"
            "9ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783",
            "RFC 7914 PBKDF2 known-answer test failed");
        // The second RFC vector also exercises the repeated-U XOR iterations.
        require(pbkdf2_hex("Password", "4e61436c", 80000, 64) ==
            "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56"
            "a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d",
            "RFC 7914 repeated-iteration vector failed");
        std::cout << "PASS: SHA256, HMAC, PBKDF2 known-answer vectors\n";

        temporary = fs::temp_directory_path() /
            ("campustwin-account-tests-" + std::to_string(std::random_device{}()));
        fs::create_directories(temporary);
        const auto account_file = temporary / "accounts.csv";
        AccountStore store(account_file);
        require(!store.contains("alice"), "new store must start empty");
        rejected([&] { store.register_user("a", "TestPass123"); }, "short username accepted");
        rejected([&] { store.register_user("alice", ""); }, "empty password accepted");
        rejected([&] { store.register_user("alice", "short"); }, "short password accepted");
        store.register_user("alice", "TestPass123");
        require(store.contains("alice"), "registration missing in memory");
        const auto first_file = read(account_file);
        require(first_file.find("TestPass123") == std::string::npos, "plaintext password stored");
        rejected([&] { store.register_user("alice", "OtherPass123"); }, "duplicate user accepted");
        require(read(account_file) == first_file, "failed registration changed file");
        AccountStore reloaded(account_file);
        require(reloaded.login("alice", "TestPass123"), "persisted login failed");
        require(!reloaded.login("alice", "WrongPass123"), "wrong password accepted");
        require(!reloaded.login("missing", "TestPass123"), "unknown user accepted");
        require(!reloaded.login("alice", ""), "empty password accepted in login");
        store.register_user("bob", "TestPass123");
        const auto after_second = read(account_file);
        const auto first_row = after_second.substr(after_second.find('\n')+1);
        const auto second_start = first_row.find('\n')+1;
        require(first_row.substr(first_row.find(',')+1,32) !=
            first_row.substr(first_row.find(',',second_start)+1,32), "salt reused");
        std::cout << "PASS: registration, reload, rejection, salted storage\n";

        write(temporary / "corrupt.csv", "bad header\n");
        rejected([&] { AccountStore bad(temporary / "corrupt.csv"); }, "corrupt header accepted");
        const auto row_start = first_file.find('\n')+1;
        const auto row = first_file.substr(row_start);
        const auto heading = first_file.substr(0,row_start);
        write(temporary / "duplicates.csv", heading + row + row);
        rejected([&] { AccountStore bad(temporary / "duplicates.csv"); }, "duplicate records accepted");
        auto invalid_hex = first_file;
        invalid_hex[invalid_hex.find(',', row_start)+1] = 'z';
        write(temporary / "invalid_hex.csv", invalid_hex);
        rejected([&] { AccountStore bad(temporary / "invalid_hex.csv"); }, "invalid hash hex accepted");
        write(temporary / "empty.csv", "");
        rejected([&] { AccountStore bad(temporary / "empty.csv"); }, "empty corrupt file accepted");

        // A regular file used as the parent makes saving fail deterministically.
        const auto blocked = temporary / "blocked";
        AccountStore cannot_save(blocked / "accounts.csv");
        write(blocked, "this is a regular file");
        rejected([&] { cannot_save.register_user("carol", "TestPass123"); }, "write failure not reported");
        require(!cannot_save.contains("carol"), "failed save changed memory");
        require(read(blocked) == "this is a regular file", "failed save changed existing file");
        std::cout << "PASS: corrupt/duplicate files and write failure rollback\n";
        fs::remove_all(temporary);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        if (!temporary.empty()) {
            std::error_code ignored;
            fs::remove_all(temporary, ignored);
        }
        return 1;
    }
}
