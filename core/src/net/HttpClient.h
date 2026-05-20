#pragma once
#include <cpr/cpr.h>
#include <functional>
#include <string>

namespace onekey::net {

// Thin wrapper around cpr for our two needs:
//   - POST binary body + headers, parse response body (Azure REST ASR)
//   - POST JSON, stream SSE deltas (OpenAI Chat Completions stream=true)
// Kept minimal — cpr itself is already a thin layer.

struct PostResult {
    long status = 0;
    std::string body;
    std::string error_msg;
    bool ok() const { return status >= 200 && status < 300 && error_msg.empty(); }
};

PostResult PostBytes(const std::string& url,
                     const cpr::Header& headers,
                     const std::vector<uint8_t>& body,
                     long timeout_ms = 15000);

// on_chunk: receives raw bytes as they arrive. Return false to abort.
// Returns final status / error.
PostResult PostJsonStream(const std::string& url,
                          const cpr::Header& headers,
                          const std::string& json_body,
                          std::function<bool(std::string_view)> on_chunk,
                          long timeout_ms = 60000);

}  // namespace onekey::net
