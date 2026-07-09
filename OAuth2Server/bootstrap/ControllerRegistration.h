#pragma once

// M3 Task 25 (authforge-sdk-refactor): extracted from main.cc's inline
// block of drogon::app().registerController(...) calls for every
// AutoCreation=false controller now living in libs/drogon (see PROGRESS.md
// for the AutoCreation=false mechanism-verification background). Kept as
// its own bootstrap module because the list is long (15 controllers) and
// growing it inline in main.cc would defeat the purpose of this task.

namespace bootstrap
{

// Explicitly constructs and registers every AutoCreation=false controller
// (all controllers under authforge::drogon::controllers, plus
// oauth2::controllers::OAuth2StandardController) on drogon::app(). Must be
// called before drogon::app().run().
void registerAllControllers();

}  // namespace bootstrap
