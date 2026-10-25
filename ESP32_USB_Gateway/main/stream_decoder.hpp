#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace rig {
// USB packet boundaries have no relationship to serial line boundaries.
class StreamDecoder {
public:
    explicit StreamDecoder(size_t limit = 768) : limit_(limit) { line_.reserve(limit); }
    void resync() { line_.clear(); discard_ = true; }
    template<typename Consumer>
    size_t feed(const uint8_t* data, size_t length, Consumer consume) {
        size_t errors = 0;
        for (size_t i = 0; i < length; ++i) {
            const char c = static_cast<char>(data[i]);
            if (c == '\n') {
                if (!discard_) consume(line_);
                line_.clear(); discard_ = false;
            } else if (c != '\r' && !discard_) {
                if (c == '\0' || line_.size() >= limit_ - 1) { line_.clear(); discard_ = true; ++errors; }
                else line_.push_back(c);
            }
        }
        return errors;
    }
private:
    size_t limit_;
    std::string line_;
    bool discard_ = false;
};
}
