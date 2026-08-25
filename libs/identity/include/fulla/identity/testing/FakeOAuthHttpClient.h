// libs/identity/include/fulla/identity/testing/FakeOAuthHttpClient.h
//
// Shared test double for fulla::identity::IOAuthHttpClient. Promoted
// (verbatim behavior) from the anonymous-namespace fake that lived inside
// libs/identity/test/SocialAuthServiceTest.cc:32-82 so that HTTP integration
// tests under tests/integration/controllers/ can reuse the same scripted-
// queue + call-recording fake to mock outbound social-provider HTTP (token
// exchange + userinfo) without any real network.
//
// Why here (not libs/common/testing/): FakeOAuthHttpClient implements
// IOAuthHttpClient, which lives in libs/identity. libs/common/testing is
// architected to NEVER depend on identity (its CMakeLists header forbids it
// -- it stays Drogon/identity-free so standalone Domain unit tests don't pull
// those in). Header-only under libs/identity/include/.../testing/ needs no new
// library -- every test target that links fulla::identity gets these
// headers via the transitive include path.
//
// Behavior (preserved exactly from the original local fake):
//   - Two scripted queues: postFormResponses / getResponses (std::deque of
//     OAuthHttpResult). Each call pops the front and synchronously invokes the
//     callback with it.
//   - Throws std::runtime_error if a queue is exhausted -- this is intentional,
//     it catches under-queueing bugs (a test that forgot to queue a response).
//     Do NOT silently change this to a default response.
//   - Records every call into postFormCalls / getCalls for assertions.
//   - Construct a fresh instance per test case (state is not auto-reset).

#pragma once

#include <fulla/identity/IOAuthHttpClient.h>

#include <deque>
#include <json/json.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fulla::identity::testing
{

class FakeOAuthHttpClient : public IOAuthHttpClient
{
  public:
    std::deque<OAuthHttpResult> postFormResponses;
    std::deque<OAuthHttpResult> getResponses;

    struct PostFormCall
    {
        std::string url;
        std::vector<std::pair<std::string, std::string>> params;
    };

    struct GetCall
    {
        std::string url;
        std::string bearerToken;
    };

    std::vector<PostFormCall> postFormCalls;
    std::vector<GetCall> getCalls;

    void postForm(
      const std::string &url,
      const std::vector<std::pair<std::string, std::string>> &params,
      ResultCallback &&cb) override
    {
        postFormCalls.push_back({url, params});
        if (postFormResponses.empty())
            throw std::runtime_error("FakeOAuthHttpClient: postForm response queue exhausted");
        OAuthHttpResult result = postFormResponses.front();
        postFormResponses.pop_front();
        cb(std::move(result));
    }

    void getWithBearerToken(
      const std::string &url,
      const std::string &bearerToken,
      ResultCallback &&cb) override
    {
        getCalls.push_back({url, bearerToken});
        if (getResponses.empty())
            throw std::runtime_error(
              "FakeOAuthHttpClient: getWithBearerToken response queue exhausted");
        OAuthHttpResult result = getResponses.front();
        getResponses.pop_front();
        cb(std::move(result));
    }
};

// ---------------------------------------------------------------------------
// Canned-response builders (preserved verbatim from SocialAuthServiceTest.cc).
// inline so multiple TUs can include this header without ODR violations.
// ---------------------------------------------------------------------------

inline OAuthHttpResult okJson(const Json::Value &body, int statusCode = 200)
{
    OAuthHttpResult result;
    result.transportOk = true;
    result.statusCode = statusCode;
    result.body = body;
    return result;
}

inline OAuthHttpResult transportFailure()
{
    OAuthHttpResult result;
    result.transportOk = false;
    return result;
}

}  // namespace fulla::identity::testing
