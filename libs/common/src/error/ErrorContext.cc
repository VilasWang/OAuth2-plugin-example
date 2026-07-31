#include <authforge/common/error/ErrorContext.h>
#include <cstdlib>
#include <optional>
#include <string>

namespace authforge::common::error
{

namespace
{

// Test-injectable override. When set, it takes precedence over the build flag /
// environment variable detection. A function-local static avoids static
// initialization order issues and keeps the override process-wide.
std::optional<bool> &detailedErrorsOverride()
{
    static std::optional<bool> override;
    return override;
}

}  // namespace

bool ErrorContext::defaultDetailedErrorsAllowed()
{
#ifdef DEBUG
    return true;
#else
    // Production builds: only enable detailed errors when the operator opts in
    // via the DETAILED_VALIDATION_ERRORS switch.
    const char *env = std::getenv("DETAILED_VALIDATION_ERRORS");
    return env && (std::string(env) == "1" || std::string(env) == "true");
#endif
}

bool ErrorContext::detailedErrorsAllowed()
{
    const auto &override = detailedErrorsOverride();
    if (override.has_value())
    {
        return *override;
    }
    return defaultDetailedErrorsAllowed();
}

void ErrorContext::setDetailedErrorsOverride(bool allowed)
{
    detailedErrorsOverride() = allowed;
}

void ErrorContext::clearDetailedErrorsOverride()
{
    detailedErrorsOverride().reset();
}

}  // namespace authforge::common::error
