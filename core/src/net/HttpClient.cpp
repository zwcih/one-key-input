#include "HttpClient.h"

namespace onekey::net {

PostResult PostBytes(const std::string& url,
                     const cpr::Header& headers,
                     const std::vector<uint8_t>& body,
                     long timeout_ms) {
    cpr::Body cb(reinterpret_cast<const char*>(body.data()), body.size());
    auto r = cpr::Post(cpr::Url{url},
                       headers,
                       cb,
                       cpr::Timeout{timeout_ms});
    PostResult out;
    out.status = r.status_code;
    out.body   = std::move(r.text);
    if (r.error) out.error_msg = r.error.message;
    return out;
}

PostResult PostJsonStream(const std::string& url,
                          const cpr::Header& headers,
                          const std::string& json_body,
                          std::function<bool(std::string_view)> on_chunk,
                          long timeout_ms) {
    cpr::WriteCallback wcb{[on_chunk](std::string_view data, intptr_t /*userdata*/) {
        return on_chunk(data);
    }, 0};

    auto r = cpr::Post(cpr::Url{url},
                       headers,
                       cpr::Body{json_body},
                       cpr::Timeout{timeout_ms},
                       wcb);

    PostResult out;
    out.status = r.status_code;
    out.body   = std::move(r.text); // typically empty when streaming
    if (r.error) out.error_msg = r.error.message;
    return out;
}

}  // namespace onekey::net
