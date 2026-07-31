#include <authforge/storage/redis/CachedClientRepository.h>
#include <drogon/drogon.h>

namespace authforge::storage::redis
{

// Task 27.5 phase 4.4: callback + DTO aliases now live on the new base
// interface (authforge::oauth2::repository::IClientRepository); bring them into
// scope for the out-of-class method definitions below.
using ClientCallback = CachedClientRepositoryBase::ClientCallback;
using BoolCallback = CachedClientRepositoryBase::BoolCallback;
using OAuth2Client = ::authforge::oauth2::model::OAuth2Client;

CachedClientRepository::CachedClientRepository(std::shared_ptr<CachedClientRepositoryBase> impl)
    : impl_(std::move(impl)),
      clientCache_(::drogon::app().getLoop(), 1.0, 4, 60)  // Clean up expired every 60s
{
}

void CachedClientRepository::getClient(const std::string &clientId, ClientCallback &&cb)
{
    OAuth2Client cachedClient;
    if (clientCache_.findAndFetch(clientId, cachedClient))
    {
        cb(cachedClient);
        return;
    }

    impl_->getClient(
      clientId,
      [self = shared_from_this(), this, clientId, cb = std::move(cb)](
        const std::optional<OAuth2Client> &client
      ) mutable {
          if (client)
          {
              clientCache_.insert(clientId, *client, 60);  // Cache for 60 seconds
          }
          cb(client);
      }
    );
}

void CachedClientRepository::validateClient(
  const std::string &clientId,
  const std::string &clientSecret,
  BoolCallback &&cb
)
{
    impl_->validateClient(clientId, clientSecret, std::move(cb));
}

}  // namespace authforge::storage::redis
