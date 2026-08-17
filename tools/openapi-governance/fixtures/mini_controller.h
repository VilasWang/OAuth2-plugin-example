// Fixture for check_spec_governance.py --selftest. Mirrors the macro idioms
// of the real controllers: multi-line ADD_METHOD_TO, `::drogon::Post,
// ::drogon::Options` double declaration (only the first method counts), and
// the bare-name method fallback.
#pragma once

class MiniController
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(
      MiniController::getX,
      "/x",
      ::drogon::Get
    );
    ADD_METHOD_TO(
      MiniController::postX,
      "/x",
      ::drogon::Post,
      ::drogon::Options
    );
    ADD_METHOD_TO(MiniController::showLoginPage, "/login", ::drogon::Get);
    ADD_METHOD_TO(MiniController::docs, "/docs/api/", ::drogon::Get);
    // Bare method name (no ::drogon:: prefix) exercises the second route
    // regex branch.
    ADD_METHOD_TO(MiniController::putY, "/y", Put);
    METHOD_LIST_END
};
