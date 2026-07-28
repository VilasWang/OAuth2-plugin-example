#include <authforge/storage/postgres/PostgresRepositoryBase.h>
#include <drogon/drogon.h>

namespace authforge::storage::postgres
{

void PostgresRepositoryBase::initFromConfig(const Json::Value &config)
{
    dbClientName_ = config.get("db_client_name", "default").asString();
    dbClientReaderName_ = config.get("db_client_reader", dbClientName_).asString();

    LOG_INFO << "PostgresRepositoryBase initFromConfig: Looking for Master=" << dbClientName_
             << ", Reader=" << dbClientReaderName_;

    try
    {
        dbClientMaster_ = ::drogon::app().getDbClient(dbClientName_);
        dbClientReader_ = ::drogon::app().getDbClient(dbClientReaderName_);

        if (!dbClientMaster_)
            LOG_ERROR << "dbClientMaster_ is NULL after lookup for name: " << dbClientName_;
        if (!dbClientReader_)
            LOG_ERROR << "dbClientReader_ is NULL after lookup for name: " << dbClientReaderName_;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "Exception in initFromConfig: " << e.what();
    }
}

}  // namespace authforge::storage::postgres
