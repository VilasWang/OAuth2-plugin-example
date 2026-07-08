#include <oauth2/error/RequestId.h>
#include <oauth2/adapters/OpenSslUuidGenerator.h>

namespace common::error
{

bool RequestId::isValid(const std::string &v)
{
    // Must be non-empty and at most 128 characters (Requirement 6.3 / 6.5).
    if (v.empty() || v.size() > 128)
    {
        return false;
    }

    // Only ASCII alphanumerics and `-`/`_` are permitted.
    for (const unsigned char c : v)
    {
        const bool isAlnum =
          (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (!isAlnum && c != '-' && c != '_')
        {
            return false;
        }
    }
    return true;
}

std::string RequestId::generate()
{
    // Task 14 (design.md §5.6): migrated off drogon::utils::getUuid() onto
    // the authforge::common::ports::IUuidGenerator Adapter implementation
    // (OpenSslUuidGenerator), same convention as observability/AuditLogger.
    // generate() yields a 36-char hyphenated UUID, which is non-empty,
    // within 1..128 and unique across requests on the same instance --
    // identical contract to the drogon::utils::getUuid() call it replaces.
    static oauth2::adapters::OpenSslUuidGenerator uuidGenerator;
    return uuidGenerator.generate();
}

std::string RequestId::resolve(const drogon::HttpRequestPtr &req)
{
    if (req)
    {
        const std::string header = req->getHeader(kHeader);
        if (isValid(header))
        {
            return header;
        }
    }
    return generate();
}

}  // namespace common::error
