#include <authforge/oauth2/protocol/ClientService.h>

namespace authforge::oauth2::protocol
{

void ClientService::validateClient(
  const std::string &clientId,
  const std::string &clientSecret,
  std::function<void(bool)> &&callback
)
{
    if (!clients_)
    {
        callback(false);
        return;
    }
    clients_->validateClient(clientId, clientSecret, std::move(callback));
}

void ClientService::validateRedirectUri(
  const std::string &clientId,
  const std::string &redirectUri,
  std::function<void(bool)> &&callback
)
{
    if (!clients_)
    {
        callback(false);
        return;
    }

    clients_->getClient(
      clientId,
      [callback = std::move(callback),
       redirectUri](std::optional<authforge::oauth2::model::OAuth2Client> client) {
          if (!client)
          {
              callback(false);
              return;
          }
          authforge::oauth2::model::Client agg(*client);
          callback(agg.isRegisteredRedirectUri(redirectUri));
      }
    );
}

void ClientService::validateClientScopes(
  const std::string &clientId,
  const std::vector<std::string> &requestedScopes,
  std::function<void(bool, std::string)> &&callback
)
{
    if (!clients_)
    {
        callback(false, "Storage not initialized");
        return;
    }

    clients_->getClient(
      clientId,
      [callback = std::move(callback),
       requestedScopes](std::optional<authforge::oauth2::model::OAuth2Client> client) mutable {
          if (!client)
          {
              callback(false, "Client not found");
              return;
          }

          authforge::oauth2::model::Client agg(*client);
          std::vector<std::string> invalidScopes;
          for (const auto &scope : requestedScopes)
          {
              if (!agg.allowsScope(scope))
              {
                  invalidScopes.push_back(scope);
              }
          }

          if (!invalidScopes.empty())
          {
              std::string errorMsg = "Scopes not allowed for this client: " + invalidScopes[0];
              for (size_t i = 1; i < invalidScopes.size(); ++i)
              {
                  errorMsg += ", " + invalidScopes[i];
              }
              callback(false, errorMsg);
              return;
          }

          callback(true, "");
      }
    );
}

}  // namespace authforge::oauth2::protocol
