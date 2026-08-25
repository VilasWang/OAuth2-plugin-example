#include <fulla/drogon/authz/ResourceScopeRegistry.h>

#include <drogon/drogon.h>

#include <algorithm>
#include <cstdlib>  // std::abort() -- M2: ensure LOG_FATAL loud-fail on all platforms
#include <cstdio>   // std::fprintf/fflush -- stderr output before abort
#include <set>
#include <sstream>
#include <unordered_map>

namespace fulla::drogon::authz
{
namespace
{

// NOTE on namespace qualification: this TU lives in fulla::drogon::authz.
// The top-level Drogon namespace (::drogon) is shadowed by fulla::drogon,
// so every Drogon type/macro below is spelled with a leading :: to resolve
// against the global ::drogon rather than fulla::drogon. (Same convention
// as the controllers, e.g. ::drogon::Get in ADD_METHOD_TO.)

// ---------------------------------------------------------------------------
// Path segmentation + template matching
// ---------------------------------------------------------------------------

/// Split a path into non-empty segments: "/api/admin/users/{userId}" ->
/// {"api", "admin", "users", "{userId}"}. Leading/trailing slashes collapse
/// to no segment so "/api/me" and "/api/me/" are equivalent.
std::vector<std::string> splitPath(std::string_view path)
{
    std::vector<std::string> segs;
    size_t start = 0;
    while (start < path.size())
    {
        size_t end = path.find('/', start);
        std::string_view tok =
          (end == std::string_view::npos) ? path.substr(start) : path.substr(start, end - start);
        if (!tok.empty())
        {
            segs.emplace_back(tok);
        }
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return segs;
}

/// True iff `templateSegs` matches `concreteSegs` segment-by-segment. A
/// template segment is a wildcard when it is enclosed in braces (e.g.
/// "{userId}"); otherwise it must equal the concrete segment exactly. The
/// segment counts must be equal (no multi-segment wildcards).
bool templateMatches(
  const std::vector<std::string> &templateSegs,
  const std::vector<std::string> &concreteSegs)
{
    if (templateSegs.size() != concreteSegs.size())
        return false;
    for (size_t i = 0; i < templateSegs.size(); ++i)
    {
        const auto &t = templateSegs[i];
        const bool isParam = !t.empty() && t.front() == '{' && t.back() == '}';
        if (!isParam && t != concreteSegs[i])
            return false;
    }
    return true;
}

/// Convert an EndpointInfo method string ("GET", "POST", ...) to a Drogon
/// HttpMethod. Returns ::drogon::Invalid for an unrecognized verb (which
/// then simply never matches a lookup).
::drogon::HttpMethod methodFromString(std::string_view s)
{
    if (s == "GET")
        return ::drogon::Get;
    if (s == "POST")
        return ::drogon::Post;
    if (s == "PUT")
        return ::drogon::Put;
    if (s == "DELETE")
        return ::drogon::Delete;
    if (s == "PATCH")
        return ::drogon::Patch;
    if (s == "OPTIONS")
        return ::drogon::Options;
    if (s == "HEAD")
        return ::drogon::Head;
    return ::drogon::Invalid;
}

/// Human-readable method name for snapshot / discovery output.
std::string methodToString(::drogon::HttpMethod m)
{
    switch (m)
    {
        case ::drogon::Get:
            return "GET";
        case ::drogon::Post:
            return "POST";
        case ::drogon::Put:
            return "PUT";
        case ::drogon::Delete:
            return "DELETE";
        case ::drogon::Patch:
            return "PATCH";
        case ::drogon::Options:
            return "OPTIONS";
        case ::drogon::Head:
            return "HEAD";
        default:
            return "UNKNOWN";
    }
}

/// A stored registry entry (template pre-split for O(segments) matching).
struct StoredEntry
{
    std::string templatePath;  // original, e.g. "/api/admin/users/{userId}"
    std::vector<std::string> templateSegs;  // pre-split
    ::drogon::HttpMethod method;
    ResourceScopeRequirement requirement;
};

/// The global registry. Populated once by buildFromEndpoints(), read
/// lock-free thereafter (the vector is never mutated after the build
/// completes). Grouped by method so lookup scans only same-method entries.
std::unordered_map<int, std::vector<StoredEntry>> &registry()
{
    static std::unordered_map<int, std::vector<StoredEntry>> reg;
    return reg;
}

/// Catch-all prefix requirements (method-agnostic). Checked after exact
/// template entries. e.g. prefix "/api/me" matches "/api/me" and
/// "/api/me/mfa/setup" (any subpath with a '/' boundary).
struct PrefixEntry
{
    std::string prefix;
    std::vector<std::string> prefixSegs;  // pre-split
    ResourceScopeRequirement requirement;
};
std::vector<PrefixEntry> &prefixEntries()
{
    static std::vector<PrefixEntry> entries;
    return entries;
}

bool &built()
{
    static bool b = false;
    return b;
}

/// Path prefixes whose routes are auth-gated by a scope requirement. Used by
/// the consistency check's coverage direction: every registered route under
/// one of these MUST have a registry entry, else the scope gate is silently
/// absent (a security gap). Matched with an explicit '/' boundary so that
/// e.g. "/api/metrics" is not swept up by "/api/me".
bool isAuthGatedPath(std::string_view path)
{
    auto startsWithBoundary = [](std::string_view p, std::string_view prefix) {
        return p == prefix ||
               (p.size() > prefix.size() && p.compare(0, prefix.size(), prefix) == 0 &&
                p[prefix.size()] == '/');
    };
    if (startsWithBoundary(path, "/api/admin"))
        return true;
    if (startsWithBoundary(path, "/api/me"))
        return true;
    // #43 M1: /oauth2/userinfo is intentionally NOT in the registry -- its
    // openid-scope + M2M-subject checks are handler-exclusive (OIDC Core
    // §5.3 401 vs 403 split). Excluding it here avoids a false LOG_FATAL.
    return false;
}

}  // namespace

void ResourceScopeRegistry::buildFromEndpoints()
{
    auto &reg = registry();
    reg.clear();
    prefixEntries().clear();

    for (const auto &ep : observability::openapi::OpenApiGenerator::endpoints())
    {
        // Only endpoints that declare a real scope requirement belong in the
        // registry. An endpoint with requiresAuth but empty requiredScopes is
        // token-gated but not scope-gated (e.g. introspect/revoke do their
        // own client-auth, no user scope).
        if (ep.requiredScopes.empty())
            continue;

        auto method = methodFromString(ep.method);
        if (method == ::drogon::Invalid)
        {
            LOG_WARN << "ResourceScopeRegistry: ignoring endpoint with "
                        "unrecognized method '"
                     << ep.method << "' (path=" << ep.path << ")";
            continue;
        }

        StoredEntry entry;
        entry.templatePath = ep.path;
        entry.templateSegs = splitPath(ep.path);
        entry.method = method;
        entry.requirement.scopes = ep.requiredScopes;
        entry.requirement.match = ScopeMatch::All;  // EndpointInfo is All by default
        entry.requirement.impliedBy = ep.impliedBy;
        reg[static_cast<int>(method)].push_back(std::move(entry));
    }

    built() = true;
    LOG_INFO << "ResourceScopeRegistry: built " << [&] {
        size_t n = 0;
        for (const auto &[_, v] : reg)
            n += v.size();
        return n;
    }() << " scope-gated route entries from OpenAPI endpoints";
}

void ResourceScopeRegistry::registerPrefix(
  const std::string &prefix, const ResourceScopeRequirement &req)
{
    PrefixEntry entry;
    entry.prefix = prefix;
    entry.prefixSegs = splitPath(prefix);
    entry.requirement = req;
    prefixEntries().push_back(std::move(entry));
    LOG_INFO << "ResourceScopeRegistry: registered prefix '" << prefix << "' (catch-all, "
             << req.scopes.size() << " required scopes)";
}

const ResourceScopeRequirement *ResourceScopeRegistry::lookup(
  std::string_view path, ::drogon::HttpMethod method)
{
    const auto concreteSegs = splitPath(path);

    // 1. Exact template match (per-method). Takes priority.
    auto it = registry().find(static_cast<int>(method));
    if (it != registry().end())
    {
        for (const auto &entry : it->second)
        {
            if (templateMatches(entry.templateSegs, concreteSegs))
            {
                return &entry.requirement;
            }
        }
    }

    // 2. Prefix fallback (method-agnostic). Longest matching prefix wins.
    //    Used for path families gated as a whole (e.g. /api/me -> `profile`
    //    covers /api/me/mfa/*, /api/me/webauthn/* without enumerating each).
    const ResourceScopeRequirement *bestPrefix = nullptr;
    size_t bestLen = 0;
    for (const auto &pe : prefixEntries())
    {
        if (concreteSegs.size() >= pe.prefixSegs.size() &&
            std::equal(pe.prefixSegs.begin(), pe.prefixSegs.end(), concreteSegs.begin()))
        {
            if (pe.prefixSegs.size() > bestLen)
            {
                bestPrefix = &pe.requirement;
                bestLen = pe.prefixSegs.size();
            }
        }
    }
    return bestPrefix;
}

std::vector<ResourceScopeRegistry::Entry> ResourceScopeRegistry::snapshot()
{
    std::vector<Entry> result;
    for (const auto &[_, entries] : registry())
    {
        for (const auto &e : entries)
        {
            Entry out;
            out.path = e.templatePath;
            out.method = methodToString(e.method);
            out.requirement = e.requirement;
            result.push_back(std::move(out));
        }
    }
    // M5: also include catch-all prefix entries (e.g. /api/me -> profile) so
    // the discovery endpoint does not under-report the real scope matrix.
    for (const auto &pe : prefixEntries())
    {
        Entry out;
        out.path = pe.prefix + "/*";
        out.method = "ANY";
        out.requirement = pe.requirement;
        result.push_back(std::move(out));
    }
    // Stable ordering for deterministic discovery output / snapshot tests.
    std::sort(result.begin(), result.end(), [](const Entry &a, const Entry &b) {
        if (a.method != b.method)
            return a.method < b.method;
        return a.path < b.path;
    });
    return result;
}

void ResourceScopeRegistry::runConsistencyCheck()
{
    const auto handlers = ::drogon::app().getHandlersInfo();

    // (a) Coverage direction: every auth-gated route must have a registry
    //     entry. A missing entry means the scope gate is silently absent --
    //     a security gap that must fail loudly.
    std::set<std::string> missingEntries;
    for (const auto &info : handlers)
    {
        const std::string &hPath = std::get<0>(info);
        const auto hMethod = std::get<1>(info);
        if (!isAuthGatedPath(hPath))
            continue;

        // Does the registry have an entry for this (path, method)?
        if (lookup(hPath, hMethod) == nullptr)
        {
            missingEntries.insert(methodToString(hMethod) + " " + hPath);
        }
    }
    if (!missingEntries.empty())
    {
        std::ostringstream oss;
        for (const auto &m : missingEntries)
            oss << "\n  - " << m;
        LOG_FATAL << "ResourceScopeRegistry consistency check FAILED: "
                     "auth-gated routes with no scope requirement declared:"
                  << oss.str()
                  << "\nAdd a requiredScopes declaration to the matching "
                     "controller's initApiDocsImpl().";
        // Use stderr (unbuffered) so the message is visible even though
        // spdlog may not flush before exit.
        std::fprintf(stderr, "FATAL: consistency check FAILED (missing):%s\n", oss.str().c_str());
        std::fflush(stderr);
        // M2: std::abort() on Windows MSVC triggers STATUS_STACK_BUFFER_OVERRUN
        // (0xC0000409) interfering with spdlog destructors; quick_exit avoids
        // that. macOS lacks quick_exit, so use abort there (works on Linux/macOS).
#ifdef _WIN32
        std::quick_exit(1);
#else
        std::abort();
#endif
    }

    // (b) Orphan direction: every registry entry must correspond to a real
    //     registered route. An orphan entry is dead config that silently
    //     gates nothing (the path never matches a real request) -- a drift
    //     risk that must also fail loudly.
    const auto snap = snapshot();
    std::set<std::string> orphanEntries;
    for (const auto &e : snap)
    {
        // Prefix entries (method "ANY", synthetic path ending /*) are
        // catch-all gates, not real ADD_METHOD_TO routes -- skip them in
        // the orphan check.
        if (e.method == "ANY")
            continue;
        bool found = false;
        for (const auto &info : handlers)
        {
            if (methodFromString(e.method) == std::get<1>(info) &&
                std::get<0>(info) == e.path)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            orphanEntries.insert(e.method + " " + e.path);
        }
    }
    if (!orphanEntries.empty())
    {
        std::ostringstream oss;
        for (const auto &m : orphanEntries)
            oss << "\n  - " + m;
        LOG_FATAL << "ResourceScopeRegistry consistency check FAILED: "
                     "registry entries with no backing route (orphan config):"
                  << oss.str()
                  << "\nRemove the declaration or fix the path/method to "
                     "match an ADD_METHOD_TO route.";
        std::fprintf(stderr, "FATAL: consistency check FAILED (orphan):%s\n", oss.str().c_str());
        std::fflush(stderr);
#ifdef _WIN32
        std::quick_exit(1);
#else
        std::abort();
#endif
    }

    LOG_INFO << "ResourceScopeRegistry consistency check passed ("
             << snap.size() << " entries verified).";
}

void ResourceScopeRegistry::clear()
{
    registry().clear();
    prefixEntries().clear();
    built() = false;
}

void ResourceScopeRegistry::clearPrefixes()
{
    prefixEntries().clear();
}

bool ResourceScopeRegistry::isBuilt()
{
    return built();
}

}  // namespace fulla::drogon::authz
