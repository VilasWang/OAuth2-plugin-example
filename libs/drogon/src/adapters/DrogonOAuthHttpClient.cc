#include <authforge/drogon/adapters/DrogonOAuthHttpClient.h>

#ifdef WITH_SOCIAL

#include <drogon/HttpClient.h>
#include <drogon/drogon.h>

namespace authforge::drogon::adapters
{

namespace
{

// Split a full URL into (origin, path+query) so it can be passed to
// drogon::HttpClient::newHttpClient(origin) + HttpRequest::setPath(path).
// url must include a scheme (http:// or https://), matching how every
// caller of this port (Google/WeChat/GitHub auth services) constructs
// its URLs.
std::pair<std::string, std::string> splitUrl(const std::string &url)
{
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
        return {url, "/"};
    size_t pathStart = url.find('/', schemeEnd + 3);
    if (pathStart == std::string::npos)
        return {url, "/"};
    return {url.substr(0, pathStart), url.substr(pathStart)};
}

}  // namespace

void DrogonOAuthHttpClient::postForm(
  const std::string &url,
  const std::vector<std::pair<std::string, std::string>> &params,
  ResultCallback &&cb
)
{
    auto [origin, path] = splitUrl(url);
    auto client = ::drogon::HttpClient::newHttpClient(origin);
    auto req = ::drogon::HttpRequest::newHttpRequest();
    req->setMethod(::drogon::Post);
    req->setPath(path);
    for (const auto &[key, value] : params)
        req->setParameter(key, value);

    auto sharedCb = std::make_shared<ResultCallback>(std::move(cb));
    client->sendRequest(
      req,
      [sharedCb, client](::drogon::ReqResult result, const ::drogon::HttpResponsePtr &response) {
          authforge::identity::OAuthHttpResult out;
          out.transportOk = result == ::drogon::ReqResult::Ok && response != nullptr;
          if (response)
          {
              out.statusCode = response->getStatusCode();
              auto json = response->getJsonObject();
              if (json)
                  out.body = *json;
          }
          (*sharedCb)(out);
      }
    );
}

void DrogonOAuthHttpClient::getWithBearerToken(
  const std::string &url,
  const std::string &bearerToken,
  ResultCallback &&cb
)
{
    auto [origin, path] = splitUrl(url);
    auto client = ::drogon::HttpClient::newHttpClient(origin);
    auto req = ::drogon::HttpRequest::newHttpRequest();
    req->setPath(path);
    if (!bearerToken.empty())
        req->addHeader("Authorization", "Bearer " + bearerToken);

    auto sharedCb = std::make_shared<ResultCallback>(std::move(cb));
    client->sendRequest(
      req,
      [sharedCb, client](::drogon::ReqResult result, const ::drogon::HttpResponsePtr &response) {
          authforge::identity::OAuthHttpResult out;
          out.transportOk = result == ::drogon::ReqResult::Ok && response != nullptr;
          if (response)
          {
              out.statusCode = response->getStatusCode();
              auto json = response->getJsonObject();
              if (json)
                  out.body = *json;
          }
          (*sharedCb)(out);
      }
    );
}

}  // namespace authforge::drogon::adapters

#endif  // WITH_SOCIAL
