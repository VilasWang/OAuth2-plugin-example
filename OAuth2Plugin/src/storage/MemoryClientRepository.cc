#include <oauth2/storage/MemoryClientRepository.h>
#include <drogon/drogon.h>
#include <oauth2/types/OAuth2Types.h>

namespace
{
/**
 * @brief Constant-time memory comparison to prevent timing attacks
 * Returns 0 if buffers are equal, non-zero otherwise.
 * Verbatim copy from MemoryOAuth2Storage.cc's anonymous-namespace helper.
 */
inline int constantTimeMemcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = static_cast<const unsigned char *>(s1);
    const unsigned char *p2 = static_cast<const unsigned char *>(s2);
    int result = 0;
    size_t i;

    for (i = 0; i < n; ++i)
    {
        result |= p1[i] ^ p2[i];
    }

    return result;
}
}  // namespace

namespace oauth2
{

void MemoryClientRepository::initFromConfig(const Json::Value &clientsConfig)
{
    if (clientsConfig.isNull() || !clientsConfig.isObject())
    {
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const auto &clientId : clientsConfig.getMemberNames())
    {
        const auto &clientData = clientsConfig[clientId];
        OAuth2Client client;
        client.clientId = clientId;

        // Parse client type (default to CONFIDENTIAL for backward
        // compatibility)
        std::string clientTypeStr = clientData.get("type", "CONFIDENTIAL").asString();
        try
        {
            client.clientType = stringToClientType(clientTypeStr);
        }
        catch (const std::exception &)
        {
            LOG_WARN << "MemoryClientRepository: Invalid client type '" << clientTypeStr
                     << "' for " << clientId << ", defaulting to CONFIDENTIAL";
            client.clientType = ClientType::CONFIDENTIAL;
        }

        // In memory mode, we store plain text or whatever provided as "secret"
        // Ideally we should hash it here too if we want parity, but for memory
        // it's fine.
        client.clientSecretHash = clientData.get("secret", "").asString();

        // Handle redirect_uri (single or array)
        if (clientData["redirect_uri"].isArray())
        {
            for (const auto &uri : clientData["redirect_uri"])
            {
                client.redirectUris.push_back(uri.asString());
            }
        }
        else if (clientData["redirect_uri"].isString())
        {
            client.redirectUris.push_back(clientData["redirect_uri"].asString());
        }

        // Handle allowed_scopes (single or array)
        if (clientData["allowed_scopes"].isArray())
        {
            for (const auto &scope : clientData["allowed_scopes"])
            {
                client.allowedScopes.push_back(scope.asString());
            }
        }
        else if (clientData["allowed_scopes"].isString())
        {
            client.allowedScopes.push_back(clientData["allowed_scopes"].asString());
        }
        // If no allowed_scopes specified, add default scopes for backward compatibility
        else if (clientId == "vue-client")
        {
            client.allowedScopes.push_back("openid");
            client.allowedScopes.push_back("profile");
            client.allowedScopes.push_back("email");
            LOG_DEBUG << "MemoryClientRepository: Added default scopes for vue-client";
        }

        LOG_DEBUG << "MemoryClientRepository: Loaded client " << clientId << " with "
                  << client.allowedScopes.size() << " allowed scopes";

        clients_[clientId] = client;
    }
}

void MemoryClientRepository::getClient(const std::string &clientId, ClientCallback &&cb)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = clients_.find(clientId);
    if (it != clients_.end())
    {
        cb(it->second);
    }
    else
    {
        cb(std::nullopt);
    }
}

void MemoryClientRepository::validateClient(
  const std::string &clientId,
  const std::string &clientSecret,
  BoolCallback &&cb
)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = clients_.find(clientId);
    if (it == clients_.end())
    {
        LOG_DEBUG << "MemoryClientRepository validateClient: Client not found - " << clientId;
        cb(false);
        return;
    }

    const auto &client = it->second;

    // PUBLIC clients skip secret validation
    if (client.clientType == ClientType::PUBLIC)
    {
        LOG_DEBUG << "MemoryClientRepository validateClient: PUBLIC client " << clientId
                  << " accepted without secret";
        cb(true);
        return;
    }

    // CONFIDENTIAL clients MUST validate secret
    if (clientSecret.empty())
    {
        LOG_WARN << "MemoryClientRepository validateClient: CONFIDENTIAL client " << clientId
                 << " missing secret";
        cb(false);
        return;
    }

    // Constant-time comparison to prevent timing attacks
    const std::string &storedHash = client.clientSecretHash;
    size_t cmpLen =
      (clientSecret.length() < storedHash.length()) ? clientSecret.length() : storedHash.length();
    bool valid = (constantTimeMemcmp(clientSecret.c_str(), storedHash.c_str(), cmpLen) == 0) &&
                 clientSecret.length() == storedHash.length();

    LOG_DEBUG << "MemoryClientRepository validateClient: Secret validation "
              << (valid ? "PASSED" : "FAILED");
    cb(valid);
}

}  // namespace oauth2
