#include <drogon/drogon_test.h>
#include <json/json.h>
#include <filesystem>
#include <fstream>
#include <fulla/drogon/observability/openapi/OpenApiGenerator.h>

using namespace fulla::drogon::observability::openapi;

DROGON_TEST(Unit_P2_OpenApiGenerator_ValidateOpenApiSpec_Structure)
{
    // Generate OpenAPI specification
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();

    // Verify required top-level fields
    CHECK(spec.isMember("openapi"));
    CHECK(spec.isMember("info"));
    CHECK(spec.isMember("paths"));
    CHECK(spec.isMember("servers"));

    // Verify OpenAPI version
    CHECK(spec["openapi"].asString() == "3.0.0");

    // Verify info section
    CHECK(spec["info"].isMember("title"));
    CHECK(spec["info"].isMember("version"));
    CHECK(spec["info"].isMember("description"));

    // Verify servers section exists and has at least one server
    CHECK(spec["servers"].isArray());
    CHECK(spec["servers"].size() > 0);

    // Verify components/schemas section
    CHECK(spec.isMember("components"));
    CHECK(spec["components"].isMember("schemas"));
}

DROGON_TEST(Unit_P2_OpenApiGenerator_ValidateOpenApiSpec_InfoFields)
{
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();

    // Verify info fields are not empty
    CHECK(!spec["info"]["title"].asString().empty());
    CHECK(!spec["info"]["version"].asString().empty());
    CHECK(!spec["info"]["description"].asString().empty());

    // Verify info field types
    CHECK(spec["info"]["title"].isString());
    CHECK(spec["info"]["version"].isString());
    CHECK(spec["info"]["description"].isString());
}

DROGON_TEST(Unit_P2_OpenApiGenerator_ValidateOpenApiSpec_ServerConfig)
{
    // Set server configuration for test
    OpenApiGenerator::setServerConfig("http://localhost:5555", "Test OAuth2 Server");

    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();

    // Verify server configuration
    CHECK(spec["servers"][0].isMember("url"));
    CHECK(spec["servers"][0].isMember("description"));

    // Verify server URL format
    std::string serverUrl = spec["servers"][0]["url"].asString();
    CHECK(serverUrl.find("http") == 0);  // Should start with http or https
    CHECK(serverUrl == "http://localhost:5555");
    CHECK(spec["servers"][0]["description"].asString() == "Test OAuth2 Server");
}

DROGON_TEST(Unit_P2_OpenApiGenerator_AddEndpoint_Basic)
{
    // Clear existing endpoints
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();
    size_t initialPathCount = spec["paths"].size();

    // Add a test endpoint
    EndpointInfo testEndpoint;
    testEndpoint.path = "/test/endpoint";
    testEndpoint.method = "GET";
    testEndpoint.summary = "Test endpoint";
    testEndpoint.description = "Test endpoint description";
    testEndpoint.tags = {"Test", "API"};
    testEndpoint.parameters = {{"param1", "Test parameter"}};
    testEndpoint.responses = {{200, "Success"}, {400, "Bad Request"}};
    testEndpoint.requiresAuth = false;

    OpenApiGenerator::addEndpoint(testEndpoint);

    // Generate new spec
    Json::Value newSpec = OpenApiGenerator::generateOpenApiSpec();

    // Verify endpoint was added
    CHECK(newSpec["paths"].size() > initialPathCount);
    CHECK(newSpec["paths"].isMember(testEndpoint.path));
}

DROGON_TEST(Unit_P2_OpenApiGenerator_AddEndpoint_RequiredFields)
{
    // Add endpoint with all required fields
    EndpointInfo endpoint;
    endpoint.path = "/required/fields/test";
    endpoint.method = "POST";
    endpoint.summary = "Required fields test";
    endpoint.description = "Testing all required fields";
    endpoint.tags = {"Test"};
    endpoint.parameters = {{"id", "Test ID"}};
    endpoint.responses = {{201, "Created"}};
    endpoint.requiresAuth = true;

    OpenApiGenerator::addEndpoint(endpoint);

    // Generate spec and verify endpoint structure
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();

    // Convert method to lowercase for JSON key access
    std::string methodLower = endpoint.method;
    std::transform(methodLower.begin(), methodLower.end(), methodLower.begin(), ::tolower);

    Json::Value pathItem = spec["paths"][endpoint.path][methodLower];

    // Verify required fields in path item
    CHECK(pathItem.isMember("summary") == true);
    CHECK(pathItem.isMember("description") == true);
    CHECK(pathItem.isMember("operationId") == true);
    CHECK(pathItem.isMember("tags") == true);
    CHECK(pathItem.isMember("parameters") == true);
    CHECK(pathItem.isMember("responses") == true);
    CHECK(pathItem.isMember("security") == true);

    // Verify values
    CHECK(pathItem["summary"].asString() == endpoint.summary);
    CHECK(pathItem["description"].asString() == endpoint.description);
    CHECK(pathItem["tags"].size() > 0);
}

DROGON_TEST(Unit_P2_OpenApiGenerator_AddEndpoint_ParametersStructure)
{
    // Add endpoint with parameters
    EndpointInfo endpoint;
    endpoint.path = "/parameters/test";
    endpoint.method = "GET";
    endpoint.summary = "Parameters test";
    endpoint.description = "Testing parameter structure";
    endpoint.tags = {"Test"};
    endpoint.parameters = {
      {"param1", "First parameter"}, {"param2", "Second parameter"}, {"param3", "Third parameter"}
    };
    endpoint.responses = {{200, "OK"}};
    endpoint.requiresAuth = false;

    OpenApiGenerator::addEndpoint(endpoint);

    // Generate spec and verify parameters
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();

    // Convert method to lowercase for JSON key access
    std::string methodLower = endpoint.method;
    std::transform(methodLower.begin(), methodLower.end(), methodLower.begin(), ::tolower);

    Json::Value parameters = spec["paths"][endpoint.path][methodLower]["parameters"];

    CHECK(parameters.isArray() == true);
    CHECK(parameters.size() == 3);

    // Verify parameter structure
    for (const auto &param : parameters)
    {
        CHECK(param.isMember("name") == true);
        CHECK(param.isMember("in") == true);
        CHECK(param.isMember("description") == true);
        CHECK(param.isMember("required") == true);
        CHECK(param.isMember("schema") == true);
        CHECK(param["schema"].isMember("type") == true);
    }
}

DROGON_TEST(Unit_P2_OpenApiGenerator_AddEndpoint_ResponsesStructure)
{
    // Add endpoint with multiple response codes
    EndpointInfo endpoint;
    endpoint.path = "/responses/test";
    endpoint.method = "POST";
    endpoint.summary = "Responses test";
    endpoint.description = "Testing response structure";
    endpoint.tags = {"Test"};
    endpoint.responses =
      {{200, "Success response"},
       {201, "Resource created"},
       {400, "Bad request"},
       {401, "Unauthorized"},
       {500, "Server error"}};
    endpoint.requiresAuth = true;

    OpenApiGenerator::addEndpoint(endpoint);

    // Generate spec and verify responses
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();

    // Convert method to lowercase for JSON key access
    std::string methodLower = endpoint.method;
    std::transform(methodLower.begin(), methodLower.end(), methodLower.begin(), ::tolower);

    Json::Value responses = spec["paths"][endpoint.path][methodLower]["responses"];

    // Verify all response codes exist
    CHECK(responses.isMember("200") == true);
    CHECK(responses.isMember("201") == true);
    CHECK(responses.isMember("400") == true);
    CHECK(responses.isMember("401") == true);
    CHECK(responses.isMember("500") == true);

    // Verify response descriptions
    CHECK(responses["200"]["description"].asString() == "Success response");
    CHECK(responses["201"]["description"].asString() == "Resource created");
}

DROGON_TEST(Unit_P2_OpenApiGenerator_AddEndpoint_Authentication)
{
    // Add endpoint that requires authentication
    EndpointInfo authEndpoint;
    authEndpoint.path = "/auth/test";
    authEndpoint.method = "GET";
    authEndpoint.summary = "Authentication test";
    authEndpoint.description = "Testing authentication requirement";
    authEndpoint.tags = {"Test"};
    authEndpoint.responses = {{200, "OK"}};
    authEndpoint.requiresAuth = true;

    OpenApiGenerator::addEndpoint(authEndpoint);

    // Add endpoint without authentication
    EndpointInfo noAuthEndpoint;
    noAuthEndpoint.path = "/noauth/test";
    noAuthEndpoint.method = "GET";
    noAuthEndpoint.summary = "No authentication test";
    noAuthEndpoint.description = "Testing public endpoint";
    noAuthEndpoint.tags = {"Test"};
    noAuthEndpoint.responses = {{200, "OK"}};
    noAuthEndpoint.requiresAuth = false;

    OpenApiGenerator::addEndpoint(noAuthEndpoint);

    // Generate spec and verify authentication settings
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();

    // Convert method to lowercase for JSON key access
    std::string authMethodLower = authEndpoint.method;
    std::transform(
      authMethodLower.begin(), authMethodLower.end(), authMethodLower.begin(), ::tolower
    );
    std::string noAuthMethodLower = noAuthEndpoint.method;
    std::transform(
      noAuthMethodLower.begin(), noAuthMethodLower.end(), noAuthMethodLower.begin(), ::tolower
    );

    // Verify auth endpoint has security requirement
    Json::Value authPathItem = spec["paths"][authEndpoint.path][authMethodLower];
    CHECK(authPathItem.isMember("security") == true);
    CHECK(authPathItem["security"].size() > 0);

    // Verify no-auth endpoint exists and has correct path
    Json::Value noAuthPathItem = spec["paths"][noAuthEndpoint.path][noAuthMethodLower];
    CHECK(noAuthPathItem.isMember("summary") == true);
    CHECK(noAuthPathItem["summary"].asString() == "No authentication test");
}

DROGON_TEST(Unit_P2_OpenApiGenerator_SetApiInfo_Legacy)
{
    // Set custom API info
    std::string testTitle = "Test API Title";
    std::string testVersion = "2.0.0";
    std::string testDescription = "Test API description";

    OpenApiGenerator::setApiInfo(testTitle, testVersion, testDescription);

    // Generate spec and verify custom info
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();

    CHECK(spec["info"]["title"].asString() == testTitle);
    CHECK(spec["info"]["version"].asString() == testVersion);
    CHECK(spec["info"]["description"].asString() == testDescription);
}

DROGON_TEST(Unit_P2_OpenApiGenerator_Components_Schemas)
{
    // Generate spec and verify component schemas
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();
    Json::Value schemas = spec["components"]["schemas"];

    // Verify at least Error schema exists
    CHECK(schemas.isMember("Error"));

    // Verify Error schema structure
    Json::Value errorSchema = schemas["Error"];
    CHECK(errorSchema.isMember("type"));
    CHECK(errorSchema.isMember("properties"));

    Json::Value errorProps = errorSchema["properties"];
    CHECK(errorProps.isMember("code"));
    CHECK(errorProps.isMember("category"));
    CHECK(errorProps.isMember("message"));
    CHECK(errorProps.isMember("details"));
    CHECK(errorProps.isMember("request_id"));
}

DROGON_TEST(Unit_P2_OpenApiGenerator_CompleteSpec_Integrity)
{
    // Generate complete spec
    Json::Value spec = OpenApiGenerator::generateOpenApiSpec();

    // Verify spec is not null
    CHECK(!spec.isNull());
    CHECK(spec.isObject());

    // Verify spec is valid JSON (no syntax errors)
    CHECK(spec.type() == Json::objectValue);

    // Verify all required OpenAPI 3.0 fields exist
    std::vector<std::string> requiredFields = {"openapi", "info", "paths"};
    for (const auto &field : requiredFields)
    {
        CHECK(spec.isMember(field));
    }

    // Verify openapi version format
    std::string openapiVersion = spec["openapi"].asString();
    CHECK(openapiVersion.find("3.0") == 0);
}

DROGON_TEST(Unit_P2_OpenApiGenerator_WriteToFile_CreateDirectory)
{
    // Test writing to a new directory
    std::string testPath = "test_output/openapi_test.json";

    // Remove test directory if it exists
    if (std::filesystem::exists("test_output"))
    {
        std::filesystem::remove_all("test_output");
    }

    // Write spec to file
    bool result = OpenApiGenerator::writeToFile(testPath);

    // Verify file was created
    CHECK(result == true);
    CHECK(std::filesystem::exists(testPath) == true);

    // Verify file contains valid JSON
    std::ifstream file(testPath);
    Json::Value spec;
    Json::CharReaderBuilder builder;
    std::string errs;
    bool parseSuccess = Json::parseFromStream(builder, file, &spec, &errs);
    file.close();

    CHECK(parseSuccess == true);

    // Verify spec structure
    CHECK(spec.isMember("openapi") == true);
    CHECK(spec.isMember("info") == true);

    // Cleanup
    if (std::filesystem::exists("test_output"))
    {
        std::filesystem::remove_all("test_output");
    }
}
