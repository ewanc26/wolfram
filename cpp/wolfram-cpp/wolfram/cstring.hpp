#ifndef WOLFRAM_CPP_CSTRING_HPP
#define WOLFRAM_CPP_CSTRING_HPP

#include <cstdlib>
#include <string>

namespace wolfram {

// RAII owner of a heap `char*` (e.g. JSON emitted by wolfram). The default
// deleter is `std::free`; for cJSON-backed strings pass the specific
// `wf_*_json_free(char*)` as the deleter.
class cstring {
public:
    using deleter_type = void (*)(char *);

    cstring() noexcept : ptr_(nullptr), del_(nullptr) {}
    explicit cstring(char *p, deleter_type del = &cstring::free_char) noexcept
        : ptr_(p), del_(del ? del : &cstring::free_char) {}

    ~cstring() { reset(); }

    cstring(const cstring &) = delete;
    cstring &operator=(const cstring &) = delete;

    cstring(cstring &&o) noexcept : ptr_(o.ptr_), del_(o.del_) {
        o.ptr_ = nullptr;
        o.del_ = nullptr;
    }
    cstring &operator=(cstring &&o) noexcept {
        if (this != &o) {
            reset(o.ptr_, o.del_);
            o.ptr_ = nullptr;
            o.del_ = nullptr;
        }
        return *this;
    }

    const char *get() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    std::string str() const { return ptr_ ? std::string(ptr_) : std::string(); }

    char *release() noexcept {
        char *p = ptr_;
        ptr_ = nullptr;
        return p;
    }

    void reset(char *p = nullptr, deleter_type del = nullptr) noexcept {
        if (ptr_ && del_) {
            del_(ptr_);
        }
        ptr_ = p;
        del_ = del ? del : &cstring::free_char;
    }

private:
    static void free_char(char *p) noexcept { std::free(p); }

    char *ptr_;
    deleter_type del_;
};

} // namespace wolfram

#endif
