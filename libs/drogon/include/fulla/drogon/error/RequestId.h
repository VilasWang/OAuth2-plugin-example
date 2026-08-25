#pragma once

#include <drogon/HttpRequest.h>
#include <string>

namespace fulla::common::error
{

// RequestId centralizes resolution and generation of the per-request
// correlation identifier (Request_ID) used in error responses and logs.
//
// Resolution rules (design Property 10 / Requirement 6):
//   * If the inbound request carries a valid `X-Request-ID` header (non-empty,
//     length <= 128, only ASCII alphanumerics and `-`/`_`), that value is
//     reused as the Request_ID.
//   * Otherwise (header missing, empty, too long or containing characters
//     outside the agreed set) a fresh Request_ID is generated.
//
// Generation uses fulla::drogon::adapters::OpenSslUuidGenerator (Task 14, design.md
// §5.6: fulla::common::ports::IUuidGenerator's Adapter implementation;
// consistent with observability/AuditLogger) so that ids are unique across
// requests on the same backend instance while always having a length within
// 1..128.
class RequestId
{
  public:
    // Returns the Request_ID for `req`: reuses a valid `X-Request-ID` header
    // value, otherwise returns a freshly generated id. Always returns a valid,
    // non-empty value (never fails on malformed headers).
    static std::string resolve(const ::drogon::HttpRequestPtr &req);

    // Validates a candidate Request_ID: true iff `v` is non-empty, at most 128
    // characters long and composed solely of ASCII alphanumerics and `-`/`_`.
    static bool isValid(const std::string &v);

    // Generates a new non-empty Request_ID, unique across requests on the same
    // instance, with a length in the range 1..128.
    static std::string generate();

  private:
    static constexpr const char *kHeader = "X-Request-ID";
};

}  // namespace fulla::common::error
