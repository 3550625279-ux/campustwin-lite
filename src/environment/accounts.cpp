#include "campustwin/accounts.hpp"

#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <random>
#include <system_error>

namespace campustwin {
namespace account_detail {
// Small teaching implementation of SHA-256 (FIPS 180-4), HMAC (RFC 2104),
// and PBKDF2 (RFC 8018). No third-party source implementation is embedded.
// Published known-answer vectors are checked in tests/test_accounts.cpp.
namespace {
using Bytes = std::vector<std::uint8_t>;
using Hash = std::array<std::uint8_t, 32>;
constexpr std::array<std::uint32_t, 64> constants{{
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
}};
std::uint32_t rotr(std::uint32_t value, unsigned shift) {
    return (value >> shift) | (value << (32 - shift));
}
Hash sha256(const Bytes& input) {
    Bytes data = input;
    const auto bits = static_cast<std::uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8) {
        data.push_back(static_cast<std::uint8_t>(bits >> shift));
    }
    std::array<std::uint32_t, 8> state{{
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    }};
    for (std::size_t offset = 0; offset < data.size(); offset += 64) {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                w[i] = (w[i] << 8) | data[offset + 4 * i + j];
            }
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const auto s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
            const auto s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        auto a=state[0], b=state[1], c=state[2], d=state[3];
        auto e=state[4], f=state[5], g=state[6], h=state[7];
        for (std::size_t i = 0; i < 64; ++i) {
            const auto sum1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            const auto choose = (e & f) ^ (~e & g);
            const auto t1 = h + sum1 + choose + constants[i] + w[i];
            const auto sum0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto t2 = sum0 + majority;
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
        state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
    }
    Hash output{};
    for (std::size_t i = 0; i < 8; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            output[4*i+j] = static_cast<std::uint8_t>(state[i] >> (24-8*j));
        }
    }
    return output;
}
struct HmacSha256 {
    Bytes inner_pad = Bytes(64, 0x36);
    Bytes outer_pad = Bytes(64, 0x5c);
    explicit HmacSha256(const std::string& password) {
        Bytes key(password.begin(), password.end());
        if (key.size() > 64) {
            const auto reduced = sha256(key);
            key.assign(reduced.begin(), reduced.end());
        }
        for (std::size_t i = 0; i < key.size(); ++i) {
            inner_pad[i] ^= key[i];
            outer_pad[i] ^= key[i];
        }
    }
    Hash operator()(const Bytes& message) const {
        Bytes inner = inner_pad;
        inner.insert(inner.end(), message.begin(), message.end());
        const auto inner_hash = sha256(inner);
        Bytes outer = outer_pad;
        outer.insert(outer.end(), inner_hash.begin(), inner_hash.end());
        return sha256(outer);
    }
};
std::string hex(const Bytes& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(2 * bytes.size());
    for (auto byte : bytes) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}
Bytes unhex(const std::string& text) {
    if (text.size() % 2 != 0) throw ValidationError("Invalid hexadecimal account data");
    auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        throw ValidationError("Account hexadecimal fields must use lowercase 0-9a-f");
    };
    Bytes result;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        result.push_back(static_cast<std::uint8_t>(16*digit(text[i]) + digit(text[i+1])));
    }
    return result;
}
}  // namespace

// These detail functions support published known-answer tests; not a shared API.
std::string sha256_hex(const std::string& input) {
    const auto result = sha256(Bytes(input.begin(), input.end()));
    return hex(Bytes(result.begin(), result.end()));
}
std::string hmac_sha256_hex(const std::string& key_hex, const std::string& message) {
    const auto key = unhex(key_hex);
    const HmacSha256 hmac(std::string(key.begin(), key.end()));
    const auto result = hmac(Bytes(message.begin(), message.end()));
    return hex(Bytes(result.begin(), result.end()));
}
std::string pbkdf2_hex(const std::string& password, const std::string& salt_hex,
                       std::uint32_t iterations, std::size_t output_bytes = 32) {
    if (iterations == 0 || output_bytes == 0 || output_bytes > 64) {
        throw ValidationError("PBKDF2 parameters outside the local demo limits");
    }
    const auto salt = unhex(salt_hex);
    const HmacSha256 hmac(password);
    Bytes derived;
    for (std::uint32_t block = 1; derived.size() < output_bytes; ++block) {
        auto input = salt;
        for (int shift = 24; shift >= 0; shift -= 8) {
            input.push_back(static_cast<std::uint8_t>(block >> shift));
        }
        auto previous = hmac(input);
        auto result = previous;
        for (std::uint32_t i = 1; i < iterations; ++i) {
            previous = hmac(Bytes(previous.begin(), previous.end()));
            for (std::size_t j = 0; j < result.size(); ++j) result[j] ^= previous[j];
        }
        derived.insert(derived.end(), result.begin(), result.end());
    }
    derived.resize(output_bytes);
    return hex(derived);
}
std::string random_hex(std::size_t byte_count) {
    std::random_device random;
    Bytes bytes(byte_count);
    for (auto& byte : bytes) byte = static_cast<std::uint8_t>(random());
    return hex(bytes);
}
}  // namespace account_detail
namespace {
constexpr char header[] = "username,salt_hex,iterations,pbkdf2_sha256_hex";
constexpr std::uint32_t iterations = 200000;
bool valid_username(const std::string& name) {
    if (name.size() < 3 || name.size() > 32) return false;
    for (unsigned char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}
bool valid_password(const std::string& password) {
    if (password.size() < 8 || password.size() > 128) return false;
    for (unsigned char c : password) if (c < 32 || c == 127) return false;
    return true;
}
std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find(',', start);
        fields.push_back(line.substr(start, end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return fields;
}
bool equal_digest(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        difference |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return difference == 0;
}
}  // namespace

AccountStore::AccountStore(const std::filesystem::path& file) : file_(file) {
    if (file_.empty()) throw ValidationError("Account file path is empty");
    std::error_code error;
    const bool exists = std::filesystem::exists(file_, error);
    if (error) throw ValidationError("Cannot inspect account file: " + error.message());
    if (!exists) return;
    std::ifstream input(file_, std::ios::binary);
    if (!input) throw ValidationError("Cannot open account file");
    std::string line;
    if (!std::getline(input, line)) throw ValidationError("Account file is empty");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != header) throw ValidationError("Unsupported or damaged account file header");
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto fields = split(line);
        if (fields.size() != 4 || !valid_username(fields[0])) {
            throw ValidationError("Invalid account row at line " + std::to_string(line_number));
        }
        std::uint32_t count = 0;
        const auto parsed = std::from_chars(fields[2].data(), fields[2].data()+fields[2].size(), count);
        if (parsed.ec != std::errc{} || parsed.ptr != fields[2].data()+fields[2].size() ||
            count != iterations || fields[1].size() != 32 || fields[3].size() != 64) {
            throw ValidationError("Invalid password record at line " + std::to_string(line_number));
        }
        // Decode only to validate lowercase hex and the exact stored byte lengths.
        account_detail::unhex(fields[1]);
        account_detail::unhex(fields[3]);
        if (!records_.emplace(fields[0], Record{fields[1],count,fields[3]}).second) {
            throw ValidationError("Duplicate account in file: " + fields[0]);
        }
    }
    if (input.bad()) throw ValidationError("Could not finish reading account file");
}

bool AccountStore::contains(const std::string& username) const {
    return records_.find(username) != records_.end();
}

void AccountStore::save(const std::map<std::string, Record>& records) const {
    namespace fs = std::filesystem;
    const auto parent = file_.parent_path();
    std::error_code error;
    if (!parent.empty()) fs::create_directories(parent, error);
    if (error) throw ValidationError("Cannot create account directory: " + error.message());
    auto temporary = file_;
    temporary += ".tmp-" + account_detail::random_hex(8);
    auto backup = file_;
    backup += ".bak-" + account_detail::random_hex(8);
    bool backed_up = false;
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw ValidationError("Cannot create temporary account file");
        output << header << '\n';
        for (const auto& item : records) {
            const auto& record = item.second;
            output << item.first << ',' << record.salt_hex << ',' << record.iterations << ','
                   << record.digest_hex << '\n';
        }
        output.flush();
        if (!output) throw ValidationError("Cannot write account file");
        output.close();
        if (!output) throw ValidationError("Cannot close account file");
        if (fs::exists(file_)) {
            if (!fs::is_regular_file(file_)) throw ValidationError("Account path must be a regular file");
            fs::rename(file_, backup);
            backed_up = true;
        }
        fs::rename(temporary, file_);
    } catch (const std::exception& problem) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        if (backed_up) {
            fs::rename(backup, file_, error);
            if (error) {
                throw ValidationError("Account save failed; original retained in backup: " + backup.string());
            }
        }
        throw ValidationError(std::string("Account save failed: ") + problem.what());
    }
    if (backed_up) fs::remove(backup, error);
}

void AccountStore::register_user(const std::string& username, const std::string& password) {
    if (!valid_username(username)) {
        throw ValidationError("Username must be 3-32 ASCII letters, digits, or underscores");
    }
    if (!valid_password(password)) {
        throw ValidationError("Password must be 8-128 bytes with no control characters");
    }
    if (contains(username)) throw ValidationError("Username already exists");
    Record record;
    record.salt_hex = account_detail::random_hex(16);
    record.iterations = iterations;
    record.digest_hex = account_detail::pbkdf2_hex(password, record.salt_hex, iterations);
    auto candidate = records_;
    candidate.emplace(username, std::move(record));
    save(candidate);  // Commit memory only after the complete file has been saved.
    records_.swap(candidate);
}

bool AccountStore::login(const std::string& username, const std::string& password) const {
    if (!valid_username(username) || !valid_password(password)) return false;
    const auto found = records_.find(username);
    // Keep the expensive KDF for unknown users too; do not reveal existence via a cheap fast path.
    const auto salt = found == records_.end() ? std::string(32, '0') : found->second.salt_hex;
    const auto digest = account_detail::pbkdf2_hex(password, salt, iterations);
    return found != records_.end() && equal_digest(digest, found->second.digest_hex);
}

}  // namespace campustwin
