#pragma once

// Task 15 (authforge-sdk-refactor, design.md §6/§8): fake implementations
// of common::ports interfaces. See FakeClock.h for the placement
// rationale.
//
// FakeSubjectResolver: an in-memory ISubjectResolver for oauth2 Domain
// tests (M2b+) that need a working subject-resolution port without a real
// identity implementation. A test populates the mapping directly via
// addMapping(); resolve() looks it up synchronously but still invokes the
// callback asynchronously-shaped (immediately, not deferred to an event
// loop) to match the port's real (identity-backed) implementations, which
// are genuinely async.

#include <authforge/common/ports/ISubjectResolver.h>

#include <unordered_map>

namespace authforge::common::testing
{

class FakeSubjectResolver : public authforge::common::ports::ISubjectResolver
{
  public:
    void resolve(const model::Subject &subject, ResolveCallback &&cb) override
    {
        auto it = mappings_.find(subject.value());
        if (it == mappings_.end())
        {
            cb(std::nullopt);
            return;
        }
        cb(it->second);
    }

    /// Register a Subject -> internal user id mapping resolve() will
    /// return.
    void addMapping(const model::Subject &subject, int32_t internalUserId)
    {
        mappings_[subject.value()] = internalUserId;
    }

    /// Remove all registered mappings.
    void clear()
    {
        mappings_.clear();
    }

  private:
    std::unordered_map<std::string, int32_t> mappings_;
};

}  // namespace authforge::common::testing
