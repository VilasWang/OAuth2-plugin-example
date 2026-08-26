// #71: in-memory ISocialLinkStateStore -- the test double (and the reference
// semantics for the Redis-backed production store): issue mints a unique
// token bound to (user, provider); consume is single-use.

#pragma once

#ifdef WITH_SOCIAL

#include <fulla/identity/ISocialLinkStateStore.h>

#include <map>
#include <mutex>
#include <string>

namespace fulla::identity::testing
{

class MemorySocialLinkStateStore : public ISocialLinkStateStore
{
  public:
    /// Issue counter drives token uniqueness deterministically in tests.
    long long issued = 0;
    /// Number of consume() calls (replay assertions).
    long long consumed = 0;
    /// Consume misses (unknown/expired/replayed tokens).
    long long consumeMisses = 0;

    void issue(int32_t internalUserId, const std::string &provider, IssueCallback &&cb) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++issued;
        std::string token = "linkstate-" + std::to_string(issued) + "-" + std::to_string(internalUserId);
        states_[token] = SocialLinkStateData{internalUserId, provider};
        cb(token);
    }

    void consume(const std::string &state, ConsumeCallback &&cb) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++consumed;
        auto it = states_.find(state);
        if (it == states_.end())
        {
            ++consumeMisses;
            cb(std::nullopt);
            return;
        }
        // Single use: a second consume of the same token must miss.
        SocialLinkStateData data = it->second;
        states_.erase(it);
        cb(data);
    }

  private:
    std::mutex mutex_;
    std::map<std::string, SocialLinkStateData> states_;
};

}  // namespace fulla::identity::testing

#endif  // WITH_SOCIAL
