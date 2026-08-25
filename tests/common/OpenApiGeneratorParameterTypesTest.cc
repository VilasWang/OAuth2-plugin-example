#include <drogon/drogon_test.h>
#include <json/json.h>
#include <filesystem>
#include <fstream>
#include <fulla/drogon/observability/openapi/OpenApiGenerator.h>

using namespace fulla::drogon::observability::openapi;

DROGON_TEST(Unit_P2_OpenApiGenerator_ParameterTypes_String)
{
    // Test string parameter (default type)
    EndpointInfo endpoint;
    endpoint.path = "/test/string";
    endpoint.method = "GET";
    endpoint.summary = "String parameter test";
    endpoint.description = "Testing string parameter type";
    endpoint.tags = {"Test"};

    ParameterInfo stringParam;
    stringParam.name = "name";
    stringParam.description = "User name";
    stringParam.type = ParameterType::STRING;
    stringParam.location = ParameterLocation::QUERY;
    stringParam.required = true;
    endpoint.parameters.push_back(stringParam);

    endpoint.responses = {{200, "OK"}};
    endpoint.requiresAuth = false;

    OpenApiGenerator::addEndpoint(endpoint);

    // Generate spec and verify parameter
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();
    std::string methodLower = "get";
    Json::Value parameters = spec["paths"][endpoint.path][methodLower]["parameters"];

    CHECK(parameters.size() == 1);
    CHECK(parameters[0]["name"].asString() == "name");
    CHECK(parameters[0]["in"].asString() == "query");
    CHECK(parameters[0]["schema"]["type"].asString() == "string");
    CHECK(parameters[0]["required"].asBool() == true);
}

DROGON_TEST(Unit_P2_OpenApiGenerator_ParameterTypes_Integer)
{
    // Test integer parameter
    EndpointInfo endpoint;
    endpoint.path = "/test/integer";
    endpoint.method = "GET";
    endpoint.summary = "Integer parameter test";
    endpoint.description = "Testing integer parameter type";
    endpoint.tags = {"Test"};

    ParameterInfo intParam;
    intParam.name = "age";
    intParam.description = "User age";
    intParam.type = ParameterType::INTEGER;
    intParam.location = ParameterLocation::QUERY;
    intParam.required = true;
    intParam.defaultValue = "25";
    endpoint.parameters.push_back(intParam);

    endpoint.responses = {{200, "OK"}};
    endpoint.requiresAuth = false;

    OpenApiGenerator::addEndpoint(endpoint);

    // Generate spec and verify parameter
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();
    std::string methodLower = "get";
    Json::Value parameters = spec["paths"][endpoint.path][methodLower]["parameters"];

    CHECK(parameters.size() == 1);
    CHECK(parameters[0]["name"].asString() == "age");
    CHECK(parameters[0]["schema"]["type"].asString() == "integer");
    CHECK(parameters[0]["schema"]["default"].asInt() == 25);
}

DROGON_TEST(Unit_P2_OpenApiGenerator_ParameterTypes_Boolean)
{
    // Test boolean parameter
    EndpointInfo endpoint;
    endpoint.path = "/test/boolean";
    endpoint.method = "GET";
    endpoint.summary = "Boolean parameter test";
    endpoint.description = "Testing boolean parameter type";
    endpoint.tags = {"Test"};

    ParameterInfo boolParam;
    boolParam.name = "active";
    boolParam.description = "User active status";
    boolParam.type = ParameterType::BOOLEAN;
    boolParam.location = ParameterLocation::QUERY;
    boolParam.required = false;
    boolParam.defaultValue = "true";
    endpoint.parameters.push_back(boolParam);

    endpoint.responses = {{200, "OK"}};
    endpoint.requiresAuth = false;

    OpenApiGenerator::addEndpoint(endpoint);

    // Generate spec and verify parameter
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();
    std::string methodLower = "get";
    Json::Value parameters = spec["paths"][endpoint.path][methodLower]["parameters"];

    CHECK(parameters.size() == 1);
    CHECK(parameters[0]["name"].asString() == "active");
    CHECK(parameters[0]["schema"]["type"].asString() == "boolean");
    CHECK(parameters[0]["required"].asBool() == false);
    CHECK(parameters[0]["schema"]["default"].asBool() == true);
}

DROGON_TEST(Unit_P2_OpenApiGenerator_ParameterTypes_Number)
{
    // Test number parameter (floating point)
    EndpointInfo endpoint;
    endpoint.path = "/test/number";
    endpoint.method = "GET";
    endpoint.summary = "Number parameter test";
    endpoint.description = "Testing number parameter type";
    endpoint.tags = {"Test"};

    ParameterInfo numParam;
    numParam.name = "price";
    numParam.description = "Item price";
    numParam.type = ParameterType::NUMBER;
    numParam.location = ParameterLocation::QUERY;
    numParam.required = true;
    numParam.defaultValue = "99.99";
    endpoint.parameters.push_back(numParam);

    endpoint.responses = {{200, "OK"}};
    endpoint.requiresAuth = false;

    OpenApiGenerator::addEndpoint(endpoint);

    // Generate spec and verify parameter
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();
    std::string methodLower = "get";
    Json::Value parameters = spec["paths"][endpoint.path][methodLower]["parameters"];

    CHECK(parameters.size() == 1);
    CHECK(parameters[0]["name"].asString() == "price");
    CHECK(parameters[0]["schema"]["type"].asString() == "number");
    CHECK(parameters[0]["schema"]["default"].asDouble() > 99.0);
}

DROGON_TEST(Unit_P2_OpenApiGenerator_ParameterTypes_Enum)
{
    // Test enum parameter
    EndpointInfo endpoint;
    endpoint.path = "/test/enum";
    endpoint.method = "GET";
    endpoint.summary = "Enum parameter test";
    endpoint.description = "Testing enum parameter type";
    endpoint.tags = {"Test"};

    ParameterInfo enumParam;
    enumParam.name = "status";
    enumParam.description = "Order status";
    enumParam.type = ParameterType::STRING;
    enumParam.location = ParameterLocation::QUERY;
    enumParam.required = true;
    enumParam.enumValues = "pending,processing,completed,cancelled";
    endpoint.parameters.push_back(enumParam);

    endpoint.responses = {{200, "OK"}};
    endpoint.requiresAuth = false;

    OpenApiGenerator::addEndpoint(endpoint);

    // Generate spec and verify parameter
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();
    std::string methodLower = "get";
    Json::Value parameters = spec["paths"][endpoint.path][methodLower]["parameters"];

    CHECK(parameters.size() == 1);
    CHECK(parameters[0]["name"].asString() == "status");
    CHECK(parameters[0]["schema"]["type"].asString() == "string");
    CHECK(parameters[0]["schema"].isMember("enum") == true);

    // Verify enum values
    Json::Value enumValues = parameters[0]["schema"]["enum"];
    CHECK(enumValues.size() == 4);
    CHECK(enumValues[0].asString() == "pending");
    CHECK(enumValues[1].asString() == "processing");
    CHECK(enumValues[2].asString() == "completed");
    CHECK(enumValues[3].asString() == "cancelled");
}

DROGON_TEST(Unit_P2_OpenApiGenerator_ParameterTypes_Format)
{
    // Test parameter with format specification
    EndpointInfo endpoint;
    endpoint.path = "/test/format";
    endpoint.method = "GET";
    endpoint.summary = "Format parameter test";
    endpoint.description = "Testing parameter format specification";
    endpoint.tags = {"Test"};

    ParameterInfo formatParam;
    formatParam.name = "email";
    formatParam.description = "User email";
    formatParam.type = ParameterType::STRING;
    formatParam.location = ParameterLocation::QUERY;
    formatParam.required = true;
    formatParam.format = "email";
    endpoint.parameters.push_back(formatParam);

    endpoint.responses = {{200, "OK"}};
    endpoint.requiresAuth = false;

    OpenApiGenerator::addEndpoint(endpoint);

    // Generate spec and verify parameter
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();
    std::string methodLower = "get";
    Json::Value parameters = spec["paths"][endpoint.path][methodLower]["parameters"];

    CHECK(parameters.size() == 1);
    CHECK(parameters[0]["name"].asString() == "email");
    CHECK(parameters[0]["schema"]["type"].asString() == "string");
    CHECK(parameters[0]["schema"]["format"].asString() == "email");
}

DROGON_TEST(Unit_P2_OpenApiGenerator_ParameterTypes_Location)
{
    // Test different parameter locations
    EndpointInfo endpoint;
    endpoint.path = "/test/location/{id}";
    endpoint.method = "GET";
    endpoint.summary = "Location parameter test";
    endpoint.description = "Testing parameter locations";
    endpoint.tags = {"Test"};

    // Query parameter
    ParameterInfo queryParam;
    queryParam.name = "filter";
    queryParam.description = "Filter results";
    queryParam.type = ParameterType::STRING;
    queryParam.location = ParameterLocation::QUERY;
    queryParam.required = false;
    endpoint.parameters.push_back(queryParam);

    // Path parameter
    ParameterInfo pathParam;
    pathParam.name = "id";
    pathParam.description = "Resource ID";
    pathParam.type = ParameterType::INTEGER;
    pathParam.location = ParameterLocation::PATH;
    pathParam.required = true;
    endpoint.parameters.push_back(pathParam);

    // Header parameter
    ParameterInfo headerParam;
    headerParam.name = "Authorization";
    headerParam.description = "Bearer token";
    headerParam.type = ParameterType::STRING;
    headerParam.location = ParameterLocation::HEADER;
    headerParam.required = true;
    endpoint.parameters.push_back(headerParam);

    endpoint.responses = {{200, "OK"}};
    endpoint.requiresAuth = false;

    OpenApiGenerator::addEndpoint(endpoint);

    // Generate spec and verify parameter locations
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();
    std::string methodLower = "get";
    Json::Value parameters = spec["paths"][endpoint.path][methodLower]["parameters"];

    CHECK(parameters.size() == 3);

    // Verify query parameter
    bool foundQueryParam = false;
    bool foundPathParam = false;
    bool foundHeaderParam = false;

    for (const auto &param : parameters)
    {
        std::string paramIn = param["in"].asString();
        std::string paramName = param["name"].asString();

        if (paramIn == "query" && paramName == "filter")
        {
            foundQueryParam = true;
            CHECK(param["required"].asBool() == false);
        }
        else if (paramIn == "path" && paramName == "id")
        {
            foundPathParam = true;
            CHECK(param["schema"]["type"].asString() == "integer");
            CHECK(param["required"].asBool() == true);
        }
        else if (paramIn == "header" && paramName == "Authorization")
        {
            foundHeaderParam = true;
            CHECK(param["schema"]["type"].asString() == "string");
            CHECK(param["required"].asBool() == true);
        }
    }

    CHECK(foundQueryParam == true);
    CHECK(foundPathParam == true);
    CHECK(foundHeaderParam == true);
}

DROGON_TEST(Unit_P2_OpenApiGenerator_ResponseExamples_Basic)
{
    // Test response examples
    EndpointInfo endpoint;
    endpoint.path = "/test/response";
    endpoint.method = "GET";
    endpoint.summary = "Response example test";
    endpoint.description = "Testing response examples";
    endpoint.tags = {"Test"};

    // Add 200 response with example
    Json::Value successExample;
    successExample["status"] = "success";
    successExample["data"] = "test data";
    endpoint.responseExamples[200] = successExample;

    // Add 404 response with example
    Json::Value errorExample;
    errorExample["error"] = "not found";
    errorExample["code"] = 404;
    endpoint.responseExamples[404] = errorExample;

    endpoint.responses = {{200, "Success"}, {404, "Not found"}};
    endpoint.requiresAuth = false;

    OpenApiGenerator::addEndpoint(endpoint);

    // Generate spec and verify response examples
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();
    std::string methodLower = "get";
    Json::Value responses = spec["paths"][endpoint.path][methodLower]["responses"];

    // Verify 200 response has example
    CHECK(responses.isMember("200") == true);
    CHECK(responses["200"].isMember("content") == true);
    CHECK(responses["200"]["content"].isMember("application/json") == true);
    CHECK(responses["200"]["content"]["application/json"].isMember("example") == true);

    Json::Value example200 = responses["200"]["content"]["application/json"]["example"];
    CHECK(example200["status"].asString() == "success");
    CHECK(example200["data"].asString() == "test data");

    // Verify 404 response has example
    CHECK(responses.isMember("404") == true);
    CHECK(responses["404"].isMember("content") == true);
    CHECK(responses["404"]["content"].isMember("application/json") == true);
    CHECK(responses["404"]["content"]["application/json"].isMember("example") == true);

    Json::Value example404 = responses["404"]["content"]["application/json"]["example"];
    CHECK(example404["error"].asString() == "not found");
    CHECK(example404["code"].asInt() == 404);
}

DROGON_TEST(Unit_P2_OpenApiGenerator_ParameterTypes_Complex)
{
    // Test complex endpoint with multiple parameter types
    EndpointInfo endpoint;
    endpoint.path = "/users/{userId}/posts";
    endpoint.method = "GET";
    endpoint.summary = "Get user posts";
    endpoint.description = "Retrieve posts for a specific user";
    endpoint.tags = {"Users", "Posts"};

    // Path parameter (integer)
    ParameterInfo userIdParam;
    userIdParam.name = "userId";
    userIdParam.description = "User ID";
    userIdParam.type = ParameterType::INTEGER;
    userIdParam.location = ParameterLocation::PATH;
    userIdParam.required = true;
    endpoint.parameters.push_back(userIdParam);

    // Query parameter (boolean)
    ParameterInfo publishedParam;
    publishedParam.name = "published";
    publishedParam.description = "Only show published posts";
    publishedParam.type = ParameterType::BOOLEAN;
    publishedParam.location = ParameterLocation::QUERY;
    publishedParam.required = false;
    publishedParam.defaultValue = "true";
    endpoint.parameters.push_back(publishedParam);

    // Query parameter (enum)
    ParameterInfo sortParam;
    sortParam.name = "sort";
    sortParam.description = "Sort order";
    sortParam.type = ParameterType::STRING;
    sortParam.location = ParameterLocation::QUERY;
    sortParam.required = false;
    sortParam.enumValues = "date,views,comments";
    sortParam.defaultValue = "date";
    endpoint.parameters.push_back(sortParam);

    // Query parameter (number)
    ParameterInfo limitParam;
    limitParam.name = "limit";
    limitParam.description = "Maximum number of results";
    limitParam.type = ParameterType::INTEGER;
    limitParam.location = ParameterLocation::QUERY;
    limitParam.required = false;
    limitParam.defaultValue = "10";
    endpoint.parameters.push_back(limitParam);

    // Response example
    Json::Value postsExample;
    postsExample["posts"] = Json::Value(Json::arrayValue);
    Json::Value post1;
    post1["id"] = 1;
    post1["title"] = "Test Post";
    postsExample["posts"].append(post1);
    endpoint.responseExamples[200] = postsExample;

    endpoint.responses = {{200, "List of posts"}, {404, "User not found"}};
    endpoint.requiresAuth = true;

    OpenApiGenerator::addEndpoint(endpoint);

    // Generate spec and verify complex endpoint
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();
    std::string methodLower = "get";
    Json::Value pathItem = spec["paths"][endpoint.path][methodLower];

    // Verify basic endpoint structure
    CHECK(pathItem["summary"].asString() == "Get user posts");
    CHECK(pathItem["tags"].size() == 2);
    CHECK(pathItem.isMember("security") == true);

    // Verify parameters
    Json::Value parameters = pathItem["parameters"];
    CHECK(parameters.size() == 4);

    // Verify parameter types and properties
    for (const auto &param : parameters)
    {
        std::string paramName = param["name"].asString();
        std::string paramType = param["schema"]["type"].asString();
        std::string paramIn = param["in"].asString();

        if (paramName == "userId")
        {
            CHECK(paramIn == "path");
            CHECK(paramType == "integer");
            CHECK(param["required"].asBool() == true);
        }
        else if (paramName == "published")
        {
            CHECK(paramIn == "query");
            CHECK(paramType == "boolean");
            CHECK(param["required"].asBool() == false);
            CHECK(param["schema"]["default"].asBool() == true);
        }
        else if (paramName == "sort")
        {
            CHECK(paramIn == "query");
            CHECK(paramType == "string");
            CHECK(param["schema"].isMember("enum") == true);
            CHECK(param["schema"]["enum"].size() == 3);
        }
        else if (paramName == "limit")
        {
            CHECK(paramIn == "query");
            CHECK(paramType == "integer");
            CHECK(param["schema"]["default"].asInt() == 10);
        }
    }

    // Verify response example
    Json::Value responseExample =
      pathItem["responses"]["200"]["content"]["application/json"]["example"];
    CHECK(responseExample.isMember("posts") == true);
    CHECK(responseExample["posts"].isArray() == true);
}
