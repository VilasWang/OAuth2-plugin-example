#pragma once

// Task 13 (authforge-sdk-refactor, design.md §5.6/§6): libs/common ports.
// IUuidGenerator replaces Domain-layer calls to drogon::utils::getUuid()
// (used e.g. by RequestId generation and anywhere a fresh opaque
// identifier is needed), per design.md §5.6's port table: "IUuidGenerator
// | getUuid | OpenSSL/标准库实现". This is one of the concrete
// drogon::utils replacement points design.md §5.6 calls the "主体工作"
// (main body of work) of going Drogon-free in the Domain layer -- the
// actual call-site migration happens in a later M2a task (Task 14) once
// an Adapter-side default implementation of this port exists; this task
// only declares the port shape.

#include <string>

namespace authforge::common::ports
{

/**
 * @brief Generates fresh UUID (or UUID-shaped) identifiers.
 */
class IUuidGenerator
{
  public:
    virtual ~IUuidGenerator() = default;

    /// Generate a new UUID string (canonical 8-4-4-4-12 hyphenated form).
    virtual std::string generate() = 0;
};

}  // namespace authforge::common::ports
