#pragma once
#include <stdexcept>
#include <string>

namespace campustwin {
class NotImplemented : public std::logic_error {
public:
    using std::logic_error::logic_error;
};
[[noreturn]] inline void not_implemented(const char* owner, const char* feature) {
    throw NotImplemented(std::string("NOT_IMPLEMENTED [") + owner + "]: " + feature);
}
}  // namespace campustwin
