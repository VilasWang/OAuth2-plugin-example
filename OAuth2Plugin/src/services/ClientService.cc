#include <oauth2/services/ClientService.h>

namespace oauth2
{

ClientService::ClientService(std::shared_ptr<IOAuth2Storage> storage) : storage_(std::move(storage))
{
}

void ClientService::validateClient(
  const std::string &clientId,
  const std::string &clientSecret,
  std::function<void(bool)> &&callback
)
{
    if (!storage_)
    {
        callback(false);
        return;
    }
    storage_->validateClient(clientId, clientSecret, std::move(callback));
}

void ClientService::validateRedirectUri(
  const std::string &clientId,
  const std::string &redirectUri,
  std::function<void(bool)> &&callback
)
{
    if (!storage_)
    {
        callback(false);
        return;
    }

    storage_->getClient(
      clientId, [callback = std::move(callback), redirectUri](std::optional<OAuth2Client> client) {
          if (!client)
          {
              callback(false);
              return;
          }
          for (const auto &uri : client->redirectUris)
          {
              if (uri == redirectUri)
              {
                  callback(true);
                  return;
              }
          }
          callback(false);
      }
    );
}

void ClientService::validateClientScopes(
  const std::string &clientId,
  const std::vector<std::string> &requestedScopes,
  std::function<void(bool, std::string)> &&callback
)
{
    if (!storage_)
    {
        callback(false, "Storage not initialized");
        return;
    }

    storage_->getClient(
      clientId,
      [callback = std::move(callback),
       requestedScopes](std::optional<OAuth2Client> client) mutable {
          if (!client)
          {
              callback(false, "Client not found");
              return;
          }

          std::vector<std::string> invalidScopes;
          for (const auto &scope : requestedScopes)
          {
              bool scopeAllowed = false;
              for (const auto &allowedScope : client->allowedScopes)
              {
                  if (scope == allowedScope)
                  {
                      scopeAllowed = true;
                      break;
                  }
              }

              if (!scopeAllowed)
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

}  // namespace oauth2
