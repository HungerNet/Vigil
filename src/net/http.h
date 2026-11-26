#pragma once
// Vigil — minimal HTTPS client (WinHTTP, a Windows system component).
//
// Used only by the identity tools (denickers), only on a worker thread, and only for the
// endpoints documented in docs/TRUST_AUDIT.md. No cookies, no credentials, no identifying
// headers: the user agent is the product name and version.

#include <string>

namespace vigil::net {

struct Response {
    int status = 0;        // HTTP status, 0 when the request failed before a response
    std::string body;
    std::string error;     // human-readable failure reason
};

// GET with a total timeout. Returns false on transport failure (status 0).
bool get(const std::string& url, Response& out, int timeoutMs = 8000);

}  // namespace vigil::net
