#include <fulla/identity/MfaService.h>
#include <fulla/identity/IMfaRepository.h>
#include <fulla/identity/TotpUtils.h>

namespace fulla::identity
{

MfaService::MfaService(
  std::shared_ptr<IMfaRepository> mfaRepo,
  std::shared_ptr<fulla::common::ports::ICryptoProvider> crypto,
  std::shared_ptr<fulla::common::ports::IClock> clock,
  std::string issuerName
)
    : mfaRepo_(std::move(mfaRepo)),
      crypto_(std::move(crypto)),
      clock_(std::move(clock)),
      issuerName_(std::move(issuerName))
{
}

void MfaService::setupSecret(
  int32_t userId,
  const std::string &accountLabel,
  std::function<void(std::optional<MfaSetupResult>)> &&callback
)
{
    if (!mfaRepo_ || !crypto_)
    {
        callback(std::nullopt);
        return;
    }

    std::string secret = totp::generateSecret(*crypto_);
    std::string otpUri = totp::generateOtpAuthUri(secret, accountLabel, issuerName_);

    mfaRepo_->setSecret(userId, secret, [secret, otpUri, callback = std::move(callback)](bool ok) {
        if (!ok)
        {
            callback(std::nullopt);
            return;
        }
        MfaSetupResult result;
        result.secret = secret;
        result.otpAuthUri = otpUri;
        callback(result);
    });
}

void MfaService::verifyAndEnable(
  int32_t userId,
  const std::string &code,
  std::function<void(std::optional<MfaEnableResult>)> &&callback
)
{
    if (!mfaRepo_ || !crypto_ || !clock_)
    {
        callback(std::nullopt);
        return;
    }

    auto mfaRepo = mfaRepo_;
    auto crypto = crypto_;
    auto clock = clock_;

    mfaRepo_->getMfaData(
      userId,
      [mfaRepo, crypto, clock, userId, code, callback = std::move(callback)](
        std::optional<MfaData> data
      ) {
          if (!data || data->secret.empty())
          {
              callback(std::nullopt);
              return;
          }

          if (!totp::verifyCode(data->secret, code, clock->nowSeconds()))
          {
              callback(std::nullopt);
              return;
          }

          auto backupCodes = totp::generateBackupCodes(*crypto, 10);
          std::vector<std::string> hashedCodes;
          hashedCodes.reserve(backupCodes.size());
          for (const auto &bc : backupCodes)
          {
              hashedCodes.push_back(crypto->sha256Hex(bc));
          }

          mfaRepo
            ->enable(userId, hashedCodes, [backupCodes, callback = std::move(callback)](bool ok) {
                if (!ok)
                {
                    callback(std::nullopt);
                    return;
                }
                MfaEnableResult result;
                result.backupCodes = backupCodes;
                callback(result);
            });
      }
    );
}

void MfaService::disable(int32_t userId, std::function<void(bool)> &&callback)
{
    if (!mfaRepo_)
    {
        callback(false);
        return;
    }
    mfaRepo_->disable(userId, std::move(callback));
}

void MfaService::verifyLoginCode(
  int32_t userId,
  const std::string &code,
  std::function<void(bool)> &&callback
)
{
    if (!mfaRepo_ || !clock_)
    {
        callback(false);
        return;
    }

    auto clock = clock_;
    mfaRepo_->getMfaData(
      userId, [clock, code, callback = std::move(callback)](std::optional<MfaData> data) {
          if (!data || !data->enabled || data->secret.empty())
          {
              callback(false);
              return;
          }
          callback(totp::verifyCode(data->secret, code, clock->nowSeconds()));
      }
    );
}

void MfaService::setPendingBinding(
  int32_t userId,
  const std::string &clientId,
  const std::string &redirectUri,
  std::function<void(bool)> &&callback
)
{
    if (!mfaRepo_)
    {
        callback(false);
        return;
    }
    mfaRepo_->setPendingBinding(userId, clientId, redirectUri, std::move(callback));
}

void MfaService::getPendingBinding(
  int32_t userId,
  std::function<void(std::optional<std::pair<std::string, std::string>>)> &&callback
)
{
    if (!mfaRepo_)
    {
        callback(std::nullopt);
        return;
    }
    mfaRepo_->getMfaData(userId, [callback = std::move(callback)](std::optional<MfaData> data) {
        if (!data)
        {
            callback(std::nullopt);
            return;
        }
        callback(std::make_pair(data->pendingClientId, data->pendingRedirectUri));
    });
}

void MfaService::clearPendingBinding(int32_t userId, std::function<void(bool)> &&callback)
{
    if (!mfaRepo_)
    {
        callback(false);
        return;
    }
    mfaRepo_->clearPendingBinding(userId, std::move(callback));
}

}  // namespace fulla::identity
