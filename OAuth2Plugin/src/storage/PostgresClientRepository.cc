#include <oauth2/storage/PostgresClientRepository.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <oauth2/types/OAuth2Types.h>

namespace
{
/**
 * @brief Constant-time memory comparison to prevent timing attacks.
 * Returns 0 if buffers are equal, non-zero otherwise.
 * Verbatim copy from the original PostgresOAuth2Storage.cc anonymous
 * namespace helper (validateClient is the only caller in this file, same as
 * in the original).
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

#include <authforge/storage/postgres/models/Oauth2Clients.h>
#include <authforge/storage/postgres/models/Oauth2Scopes.h>
#include <authforge/storage/postgres/models/Oauth2ClientScopes.h>

namespace oauth2
{

using namespace drogon::orm;
using namespace drogon_model::oauth2_db;

void PostgresClientRepository::getClient(const std::string &clientId, ClientCallback &&cb)
{
    LOG_DEBUG << "Postgres getClient: " << clientId;

    // Lazy initialization of DB clients if they are null
    if (!dbClientReader_)
    {
        try
        {
            dbClientMaster_ = drogon::app().getDbClient(dbClientName_);
            dbClientReader_ = drogon::app().getDbClient(dbClientReaderName_);
            LOG_INFO << "Postgres DB Clients initialized lazily for getClient";
        }
        catch (...)
        {
            LOG_ERROR << "Postgres getClient: Failed to get DB clients lazily. Name="
                      << dbClientReaderName_;
            cb(std::nullopt);
            return;
        }
    }

    if (!dbClientReader_)
    {
        LOG_ERROR << "Postgres getClient: dbClientReader_ is STILL NULL!";
        cb(std::nullopt);
        return;
    }

    auto sharedCb = std::make_shared<ClientCallback>(std::move(cb));
    try
    {
        Mapper<Oauth2Clients> mapper(dbClientReader_);
        mapper.findOne(
          Criteria(Oauth2Clients::Cols::_client_id, CompareOperator::EQ, clientId),
          [sharedCb, clientId, self = shared_from_this(), this](const Oauth2Clients &row) {
              OAuth2Client client;
              client.clientId = row.getValueOfClientId();
              LOG_DEBUG << "Postgres getClient: Found -> " << client.clientId;

              std::string clientTypeStr = row.getValueOfClientType();
              try
              {
                  client.clientType = stringToClientType(clientTypeStr);
                  LOG_DEBUG << "Postgres getClient: Type -> " << clientTypeStr;
              }
              catch (const std::exception &)
              {
                  LOG_WARN << "Postgres getClient: Invalid client type '" << clientTypeStr
                           << "' for " << client.clientId << ", defaulting to CONFIDENTIAL";
                  client.clientType = ClientType::CONFIDENTIAL;
              }

              client.clientSecretHash = row.getValueOfClientSecret();
              client.salt = row.getValueOfSalt();

              std::string uris = row.getValueOfRedirectUris();
              LOG_DEBUG << "Postgres getClient: Redirect URIs -> " << uris;
              std::stringstream ss(uris);
              std::string uri;
              while (std::getline(ss, uri, ','))
              {
                  client.redirectUris.push_back(uri);
              }

              // Fetch allowed scopes from oauth2_client_scopes table
              LOG_DEBUG << "Postgres getClient: Fetching allowed scopes for " << client.clientId;
              row.getScope(
                dbClientReader_,
                [client, sharedCb](
                  const std::vector<std::pair<Oauth2Scopes, Oauth2ClientScopes>> &scopes
                ) mutable {
                    for (const auto &scopePair : scopes)
                    {
                        const Oauth2Scopes &scope = scopePair.first;
                        client.allowedScopes.push_back(scope.getValueOfName());
                        LOG_DEBUG << "Postgres getClient: Allowed scope -> "
                                  << scope.getValueOfName();
                    }

                    LOG_DEBUG << "Postgres getClient: Total allowed scopes -> "
                              << client.allowedScopes.size();
                    (*sharedCb)(client);
                },
                [sharedCb, clientId](const DrogonDbException &e) {
                    LOG_WARN << "Postgres getClient: Failed to fetch scopes for " << clientId
                             << ", returning client with empty scopes: " << e.base().what();
                    // Even if scope fetch fails, return the client with empty scopes
                    // This maintains backward compatibility
                    (*sharedCb)(std::nullopt);
                }
              );
          },
          [sharedCb, clientId](const DrogonDbException &e) {
              LOG_DEBUG << "Postgres getClient: Not found or Error -> " << clientId << " ("
                        << e.base().what() << ")";
              (*sharedCb)(std::nullopt);
          }
        );
    }
    catch (...)
    {
        LOG_ERROR << "Postgres getClient Exception";
        (*sharedCb)(std::nullopt);
    }
}

void PostgresClientRepository::validateClient(
  const std::string &clientId,
  const std::string &clientSecret,
  BoolCallback &&cb
)
{
    LOG_DEBUG << "Postgres validateClient: " << clientId;

    // Lazy initialization of DB clients if they are null
    if (!dbClientReader_)
    {
        try
        {
            dbClientMaster_ = drogon::app().getDbClient(dbClientName_);
            dbClientReader_ = drogon::app().getDbClient(dbClientReaderName_);
            LOG_INFO << "Postgres DB Clients initialized lazily for validateClient";
        }
        catch (...)
        {
            LOG_ERROR << "Postgres validateClient: Failed to get DB clients lazily. Name="
                      << dbClientReaderName_;
            cb(false);
            return;
        }
    }

    if (!dbClientReader_)
    {
        LOG_ERROR << "Postgres validateClient: dbClientReader_ is STILL NULL!";
        cb(false);
        return;
    }

    auto sharedCb = std::make_shared<BoolCallback>(std::move(cb));
    try
    {
        Mapper<Oauth2Clients> mapper(dbClientReader_);

        // First, get client information including type
        mapper.findOne(
          Criteria(Oauth2Clients::Cols::_client_id, CompareOperator::EQ, clientId),
          [sharedCb, clientId, clientSecret](const Oauth2Clients &row) {
              // Get client type
              std::string clientTypeStr = row.getValueOfClientType();
              ClientType clientType = ClientType::CONFIDENTIAL;  // Default fallback
              try
              {
                  clientType = stringToClientType(clientTypeStr);
              }
              catch (const std::exception &)
              {
                  LOG_WARN << "Postgres validateClient: Invalid client type '" << clientTypeStr
                           << "' for " << clientId << ", defaulting to CONFIDENTIAL";
              }

              // PUBLIC clients skip secret validation
              if (clientType == ClientType::PUBLIC)
              {
                  LOG_DEBUG << "Postgres validateClient: PUBLIC client " << clientId
                            << " accepted without secret";
                  (*sharedCb)(true);
                  return;
              }

              // CONFIDENTIAL clients MUST validate secret
              if (clientSecret.empty())
              {
                  LOG_WARN << "Postgres validateClient: CONFIDENTIAL client " << clientId
                           << " missing secret";
                  (*sharedCb)(false);
                  return;
              }

              // Constant-time secret comparison to prevent timing attacks
              std::string storedHash = row.getValueOfClientSecret();
              std::string salt = row.getValueOfSalt();
              std::string computedHash = drogon::utils::getSha256(clientSecret + salt);

              LOG_DEBUG << "Postgres validateClient: Verifying secret for " << clientId;

              // Normalize both to lowercase for case-insensitive hex comparison
              std::transform(
                computedHash.begin(), computedHash.end(), computedHash.begin(), ::tolower
              );
              std::string storedLower = storedHash;
              std::transform(
                storedLower.begin(), storedLower.end(), storedLower.begin(), ::tolower
              );

              // Use constant-time comparison to prevent timing attacks
              size_t cmpLen = (computedHash.length() < storedLower.length()) ? computedHash.length()
                                                                             : storedLower.length();
              bool match =
                (constantTimeMemcmp(computedHash.c_str(), storedLower.c_str(), cmpLen) == 0) &&
                computedHash.length() == storedLower.length();

              if (!match)
              {
                  LOG_WARN << "Postgres validateClient: Secret MISMATCH for client " << clientId;
              }

              LOG_DEBUG << "Postgres validateClient: Secret validation "
                        << (match ? "PASSED" : "FAILED");
              (*sharedCb)(match);
          },
          [sharedCb, clientId](const DrogonDbException &e) {
              LOG_ERROR << "Postgres validateClient Error (Database Exception) for " << clientId
                        << ": " << e.base().what();
              (*sharedCb)(false);
          }
        );
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "Postgres validateClient Exception: " << e.what();
        (*sharedCb)(false);
    }
    catch (...)
    {
        LOG_ERROR << "Postgres validateClient Unknown Exception";
        (*sharedCb)(false);
    }
}

}  // namespace oauth2
