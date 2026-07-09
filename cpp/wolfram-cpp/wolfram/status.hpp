#ifndef WOLFRAM_CPP_STATUS_HPP
#define WOLFRAM_CPP_STATUS_HPP

#include <string>
#include <system_error>
#include <type_traits>

#include <wolfram/_result.h>

namespace wolfram {

// std::error_code category mapping `wf_status` values to messages.
class status_category : public std::error_category {
public:
    const char *name() const noexcept override { return "wolfram"; }
    std::string message(int ev) const override;
};

inline const status_category &category() {
    static const status_category c;
    return c;
}

// Throws std::system_error carrying the status when `s != WF_OK`.
inline void require(wf_status s) {
    if (s != WF_OK) {
        throw std::system_error(std::error_code(static_cast<int>(s), category()));
    }
}

} // namespace wolfram

inline std::string wolfram::status_category::message(int ev) const {
    switch (ev) {
    case WF_OK: return "ok";
    case WF_ERR_INVALID_ARG: return "invalid argument";
    case WF_ERR_ALLOC: return "allocation failure";
    case WF_ERR_NETWORK: return "network error";
    case WF_ERR_HTTP: return "http error";
    case WF_ERR_PARSE: return "parse error";
    case WF_ERR_NOT_FOUND: return "not found";
    case WF_ERR_WOULD_BLOCK: return "would block";
    case WF_ERR_DID_RESOLVE: return "did resolve error";
    case WF_ERR_DID_DOCUMENT_NOT_FOUND: return "did document not found";
    case WF_ERR_HANDLE_RESOLVE: return "handle resolve error";
    case WF_ERR_HANDLE_DOCUMENT_NOT_FOUND: return "handle document not found";
    case WF_ERR_HANDLE_TTL_EXPIRED: return "handle ttl expired";
    case WF_ERR_HANDLE_CACHE_KEY: return "handle cache key error";
    case WF_ERR_CRYPTO: return "crypto error";
    case WF_ERR_VALIDATION: return "validation error";
    case WF_ERR_STATE: return "invalid state";
    case WF_ERR_CONFIG: return "config error";
    case WF_ERR_TIMEOUT: return "timeout";
    case WF_ERR_UNSUPPORTED: return "unsupported";
    case WF_ERR_PERMISSION: return "permission denied";
    case WF_ERR_RATE_LIMIT: return "rate limited";
    case WF_ERR_DUPLICATE: return "duplicate";
    case WF_ERR_CONFLICT: return "conflict";
    case WF_ERR_NOT_IMPLEMENTED: return "not implemented";
    case WF_ERR_INTERNAL: return "internal error";
    default: return "unknown error";
    }
}

// `wf_status` is in the global namespace, so `make_error_code` and the
// `is_error_code_enum` specialization live there to be found by ADL when
// constructing `std::error_code` from a `wf_status`.
inline std::error_code make_error_code(wf_status s) {
    return std::error_code(static_cast<int>(s), wolfram::category());
}

namespace std {
template <>
struct is_error_code_enum<wf_status> : true_type {};
} // namespace std

#endif
