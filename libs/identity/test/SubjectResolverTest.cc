// Task 19 (authforge-sdk-refactor, design.md §6): unit tests for
// authforge::identity::SubjectResolver (implements
// authforge::common::ports::ISubjectResolver).

#include <authforge/identity/SubjectResolver.h>
#include <authforge/common/model/Subject.h>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <optional>

using authforge::common::model::Subject;
using authforge::identity::ISubjectMappingRepository;
using authforge::identity::SubjectResolver;

namespace
{

class FakeSubjectMappingRepository : public ISubjectMappingRepository
{
  public:
    // key: "provider:subject" -> internalUserId
    std::map<std::string, int32_t> mappings;

    void getInternalUserId(
      const std::string &subject,
      const std::string &provider,
      OptionalIntCallback &&cb
    ) override
    {
        auto it = mappings.find(provider + ":" + subject);
        cb(it == mappings.end() ? std::nullopt : std::optional<int32_t>(it->second));
    }
};

}  // namespace

TEST(SubjectResolverTest, SplitSubjectHandlesProviderPrefix)
{
    auto [provider, localId] = authforge::identity::splitSubject("google:abc123");
    EXPECT_EQ(provider, "google");
    EXPECT_EQ(localId, "abc123");
}

TEST(SubjectResolverTest, SplitSubjectDefaultsToLocalWithoutColon)
{
    auto [provider, localId] = authforge::identity::splitSubject("alice");
    EXPECT_EQ(provider, "local");
    EXPECT_EQ(localId, "alice");
}

TEST(SubjectResolverTest, ResolvesKnownSubjectToInternalUserId)
{
    auto repo = std::make_shared<FakeSubjectMappingRepository>();
    repo->mappings["local:alice"] = 7;
    SubjectResolver resolver(repo);

    std::optional<int32_t> result;
    resolver.resolve(Subject("local:alice"), [&](std::optional<int32_t> r) { result = r; });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 7);
}

TEST(SubjectResolverTest, UnknownSubjectResolvesToNullopt)
{
    auto repo = std::make_shared<FakeSubjectMappingRepository>();
    SubjectResolver resolver(repo);

    std::optional<int32_t> result{42};
    resolver.resolve(Subject("google:unknown"), [&](std::optional<int32_t> r) { result = r; });
    EXPECT_FALSE(result.has_value());
}

TEST(SubjectResolverTest, NullRepositoryResolvesToNulloptInsteadOfCrashing)
{
    SubjectResolver resolver(nullptr);

    std::optional<int32_t> result{42};
    resolver.resolve(Subject("local:alice"), [&](std::optional<int32_t> r) { result = r; });
    EXPECT_FALSE(result.has_value());
}
