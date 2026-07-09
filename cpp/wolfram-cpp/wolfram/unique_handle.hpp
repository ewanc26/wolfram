#ifndef WOLFRAM_CPP_UNIQUE_HANDLE_HPP
#define WOLFRAM_CPP_UNIQUE_HANDLE_HPP

#include <utility>

namespace wolfram {

// RAII owner of a pointer released by a free function `Free` (signature
// `void (*)(T*)`), mirroring wolfram's uniform `wf_T` + `wf_T_free` contract.
// The pointed-to type may be incomplete; it is only ever passed to `Free`.
template <typename T, void (*Free)(T *)>
class unique_handle {
public:
    using element_type = T;

    unique_handle() noexcept : ptr_(nullptr) {}
    explicit unique_handle(T *p) noexcept : ptr_(p) {}

    ~unique_handle() { reset(); }

    unique_handle(const unique_handle &) = delete;
    unique_handle &operator=(const unique_handle &) = delete;

    unique_handle(unique_handle &&o) noexcept : ptr_(o.release()) {}
    unique_handle &operator=(unique_handle &&o) noexcept {
        if (this != &o) {
            reset(o.release());
        }
        return *this;
    }

    T *get() const noexcept { return ptr_; }
    T *operator->() const noexcept { return ptr_; }
    T &operator*() const noexcept { return *ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    T *release() noexcept {
        T *p = ptr_;
        ptr_ = nullptr;
        return p;
    }

    void reset(T *p = nullptr) noexcept {
        if (ptr_) {
            Free(ptr_);
        }
        ptr_ = p;
    }

private:
    T *ptr_;
};

} // namespace wolfram

#endif
