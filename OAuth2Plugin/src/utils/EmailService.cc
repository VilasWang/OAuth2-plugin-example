#include <oauth2/utils/EmailService.h>
#include <oauth2/config/ConfigManager.h>
#include <drogon/drogon.h>
#include <curl/curl.h>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

namespace oauth2
{

// ============================================================
// ConsoleEmailService (development)
// ============================================================

void ConsoleEmailService::sendEmail(
  const std::string &to,
  const std::string &subject,
  const std::string &body,
  std::function<void(bool)> &&callback
)
{
    LOG_INFO << "========== EMAIL (Console) ==========";
    LOG_INFO << "To: " << to;
    LOG_INFO << "Subject: " << subject;
    LOG_INFO << "Body: " << body.substr(0, 200) << (body.length() > 200 ? "..." : "");
    LOG_INFO << "=====================================";

    if (callback)
        callback(true);
}

// ============================================================
// SmtpEmailService (production) — libcurl SMTP/SMTPS
//
// SECURITY (PR #2 review, P1):
//   The previous implementation built a `python3 -c "..."` program by
//   string-concatenating the recipient/subject/body AND the SMTP
//   username/password directly into a command run through popen() (a shell).
//   That allowed shell command injection via attacker-controlled email fields
//   and leaked SMTP credentials into the process argument list / logs.
//
//   This implementation sends mail through libcurl's native SMTP/SMTPS support:
//     * No shell, no string-concatenated command — injection vector removed.
//     * Credentials are passed via CURLOPT_USERNAME / CURLOPT_PASSWORD, never
//       on a command line, never logged.
//     * Header fields (To/From/Subject) are sanitized to strip CR/LF so they
//       cannot inject additional SMTP headers (header-injection defense).
//   libcurl is built against the same OpenSSL as the rest of the project, so no
//   second TLS implementation is introduced.
// ============================================================

namespace
{
// Strip CR/LF (and other control chars) from an email HEADER value so a
// malicious recipient/subject cannot inject extra headers or SMTP commands.
// Header values are line-oriented; the message body (after the blank line) is
// NOT passed through this and may contain arbitrary text safely.
std::string sanitizeHeaderValue(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value)
    {
        // Drop CR, LF, and other C0 control characters (except none are kept).
        if (static_cast<unsigned char>(c) < 0x20)
            continue;
        out.push_back(c);
    }
    return out;
}

// RFC 5322 Date header, formatted with fixed English day/month names so the
// output is locale-independent and deterministic (also nice for tests).
std::string rfc5322Date()
{
    static const char *kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *kMonths[] =
      {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::time_t now = std::time(nullptr);
    std::tm tmUtc{};
#ifdef _WIN32
    gmtime_s(&tmUtc, &now);
#else
    gmtime_r(&now, &tmUtc);
#endif
    char buf[64];
    std::snprintf(
      buf,
      sizeof(buf),
      "%s, %02d %s %04d %02d:%02d:%02d +0000",
      kDays[tmUtc.tm_wday % 7],
      tmUtc.tm_mday,
      kMonths[tmUtc.tm_mon % 12],
      tmUtc.tm_year + 1900,
      tmUtc.tm_hour,
      tmUtc.tm_min,
      tmUtc.tm_sec
    );
    return std::string(buf);
}

// Streaming read context for CURLOPT_READFUNCTION: libcurl pulls the message
// payload from here in chunks.
struct UploadContext
{
    std::string payload;
    std::size_t offset = 0;
};

std::size_t payloadReadCallback(char *buffer, std::size_t size, std::size_t nitems, void *userp)
{
    auto *ctx = static_cast<UploadContext *>(userp);
    const std::size_t room = size * nitems;
    if (room == 0 || ctx->offset >= ctx->payload.size())
        return 0;
    const std::size_t remaining = ctx->payload.size() - ctx->offset;
    // Parenthesize to defeat a possible MSVC `min` macro (windows.h) clashing
    // with std::min when this TU is compiled alongside headers that pull it in.
    const std::size_t n = (std::min)(remaining, room);
    std::memcpy(buffer, ctx->payload.data() + ctx->offset, n);
    ctx->offset += n;
    return n;
}
}  // namespace

// Build the RFC 5322 message. Header fields are sanitized (CR/LF stripped); the
// body is appended verbatim after the header/body separator. Exposed as a
// static method so the security regression test can assert that no header
// injection is possible regardless of input.
std::string SmtpEmailService::buildMimeMessage(
  const std::string &to,
  const std::string &subject,
  const std::string &body,
  const std::string &fromName,
  const std::string &fromAddress
)
{
    const std::string safeTo = sanitizeHeaderValue(to);
    const std::string safeSubject = sanitizeHeaderValue(subject);
    const std::string safeFromName = sanitizeHeaderValue(fromName);
    const std::string safeFromAddr = sanitizeHeaderValue(fromAddress);

    std::ostringstream msg;
    msg << "Date: " << rfc5322Date() << "\r\n"
        << "From: " << safeFromName << " <" << safeFromAddr << ">\r\n"
        << "To: <" << safeTo << ">\r\n"
        << "Subject: " << safeSubject << "\r\n"
        << "MIME-Version: 1.0\r\n"
        << "Content-Type: text/plain; charset=UTF-8\r\n"
        << "Content-Transfer-Encoding: 8bit\r\n"
        << "\r\n"  // header/body separator — everything after is body
        << body << "\r\n";
    return msg.str();
}

void SmtpEmailService::sendEmail(
  const std::string &to,
  const std::string &subject,
  const std::string &body,
  std::function<void(bool)> &&callback
)
{
    auto config = config_;
    auto cb = std::make_shared<std::function<void(bool)>>(std::move(callback));

    // Run on a detached thread: libcurl's easy interface is blocking, so we keep
    // it off the Drogon event loop (preserving the previous non-blocking
    // behavior). All captured values are copies.
    std::thread([config, to, subject, body, cb]() {
        const std::string recipient = sanitizeHeaderValue(to);
        std::string payload = buildMimeMessage(to, subject, body, config.fromName, config.username);

        CURL *curl = curl_easy_init();
        if (!curl)
        {
            LOG_ERROR << "SMTP: failed to initialize libcurl";
            if (*cb)
                (*cb)(false);
            return;
        }

        UploadContext upload;
        upload.payload = std::move(payload);

        // smtps:// (implicit TLS) when useSsl, otherwise smtp:// with opportunistic
        // STARTTLS. No user-controlled data is interpolated into the URL.
        std::ostringstream url;
        url << (config.useSsl ? "smtps://" : "smtp://") << config.host << ":" << config.port;

        struct curl_slist *recipients = nullptr;
        recipients = curl_slist_append(recipients, ("<" + recipient + ">").c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
        curl_easy_setopt(
          curl, CURLOPT_USE_SSL, config.useSsl ? (long)CURLUSESSL_ALL : (long)CURLUSESSL_TRY
        );
        // Credentials via dedicated options — never on a command line / log.
        curl_easy_setopt(curl, CURLOPT_USERNAME, config.username.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, config.password.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, ("<" + config.username + ">").c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, payloadReadCallback);
        curl_easy_setopt(curl, CURLOPT_READDATA, &upload);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

        CURLcode res = curl_easy_perform(curl);

        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK)
        {
            LOG_INFO << "SMTP: Email sent to " << recipient;
            if (*cb)
                (*cb)(true);
        }
        else
        {
            // Log ONLY the libcurl error string + recipient — never credentials
            // or the message payload.
            LOG_ERROR << "SMTP: Failed to send to " << recipient << ": " << curl_easy_strerror(res);
            if (*cb)
                (*cb)(false);
        }
    }).detach();
}

// ============================================================
// Global email service factory
// ============================================================

IEmailService &getEmailService()
{
    static std::unique_ptr<IEmailService> instance;
    static std::once_flag initFlag;

    std::call_once(initFlag, []() {
        using authforge::common::config::ConfigManager;
        const char *smtpHost = ConfigManager::getEnv("OAUTH2_SMTP_HOST");
        const char *smtpUser = ConfigManager::getEnv("OAUTH2_SMTP_USER");
        const char *smtpPass = ConfigManager::getEnv("OAUTH2_SMTP_PASSWORD");

        if (
          smtpHost && smtpUser && smtpPass && std::strlen(smtpHost) > 0 &&
          std::strlen(smtpUser) > 0 && std::strlen(smtpPass) > 0
        )
        {
            // libcurl global init must happen once, before any worker thread
            // uses the easy interface (curl_global_init is not thread-safe).
            curl_global_init(CURL_GLOBAL_DEFAULT);

            SmtpEmailService::Config config;
            config.host = smtpHost;
            config.username = smtpUser;
            config.password = smtpPass;

            const char *smtpPort = ConfigManager::getEnv("OAUTH2_SMTP_PORT");
            if (smtpPort)
                config.port = std::atoi(smtpPort);

            const char *smtpFrom = ConfigManager::getEnv("OAUTH2_SMTP_FROM_NAME");
            if (smtpFrom)
                config.fromName = smtpFrom;

            const char *smtpSsl = ConfigManager::getEnv("OAUTH2_SMTP_SSL");
            if (smtpSsl && std::string(smtpSsl) == "false")
                config.useSsl = false;

            LOG_INFO << "Email service: SMTP (" << config.host << ":" << config.port << ")";
            instance = std::make_unique<SmtpEmailService>(config);
        }
        else
        {
            LOG_INFO << "Email service: Console (set OAUTH2_SMTP_* env vars to enable SMTP)";
            instance = std::make_unique<ConsoleEmailService>();
        }
    });

    return *instance;
}

}  // namespace oauth2
