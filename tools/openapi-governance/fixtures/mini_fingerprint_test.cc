// Fixture for check_spec_governance.py --selftest. Mirrors the shape of
// tests/integration/concurrency/Property4_OpenApiValidationBaselineTest.cc:
// a kFingerprint string built from "METHOD path\n" literals, terminated by
// an "enum class" that follows it.
#include <string>

namespace
{
const std::string &expectedFingerprint()
{
    static const std::string kFingerprint =
      "GET /x\n"
      "GET /docs/api/\n"
      "POST /x\n";
    return kFingerprint;
}

enum class FilterOutcome
{
    Passed
};
}  // namespace
