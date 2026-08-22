# Documentation Index

Start here and pick your track. File names use **kebab-case**; cross-references
use relative paths.

## Evaluating AuthForge

- [Project README](../README.md) — features, quick start, tech stack
- [Architecture Overview](backend/architecture-overview.md) — package layering, request flow
- [Security Architecture](backend/security-architecture.md) — token lifecycle, protection strategies
- [RBAC Guide](backend/rbac-guide.md) — roles, permissions, triple-scope control

## Integrating (SDK consumers)

- [SDK Integration Guide](backend/sdk-integration-guide.md) — release artifacts, `find_package` wiring
- [SDK Runtime Contract](backend/sdk-runtime-contract.md) — threading / ABI / exception / logging promises
- [API Reference](backend/api-reference.md) — endpoints, error catalog
- [Plugin Integration](backend/plugin-integration.md) — hosting `OAuth2Plugin` in a Drogon app
- [OIDC Guide](backend/oidc-guide.md) · [Google](backend/google-guide.md) / [WeChat](backend/wechat-guide.md) social login

## Operating

- [Production Deployment](ops/deployment.md) — Docker Compose / Helm walkthrough
- [Windows / Docker Desktop](ops/deployment-windows-docker-desktop.md)
- [Configuration Guide](backend/configuration-guide.md) — config files, env vars, secrets
- [Observability](backend/observability.md) — metrics, audit logging, health checks
- [Security Checklist](ops/security-checklist.md) · [Verification Checklist](ops/verification-checklist.md)
- [Account Lockout](ops/account-lockout.md) — lockout rules and reset procedures
- [Data Persistence](backend/data-persistence.md) · [Data Consistency](backend/data-consistency.md)

## Contributing

- [CONTRIBUTING.md](../CONTRIBUTING.md) — build, conventions, CI gates
- [Testing Guide](backend/testing-guide.md) — test tree, categories, how to run
- [CI/CD Pipeline](backend/ci-cd-guide.md) — workflows, release pipeline
- [Documentation Standards](backend/documentation-standards.md)
- Frontend: [admin E2E guide](admin/e2e-testing-guide.md) · [admin test cases](admin/test-cases.md) · [user test cases](frontend/test-cases.md)

## Productization

- [Productization Evolution Plan](productization-evolution/productization-evolution-plan.md) — roadmap, priorities, risks (companion to the research report)
- [Progress Status](productization-evolution/progress-status.md) — what's done, in-progress, and not started across all phases
- [Next-Phase Implementation Plan](productization-evolution/next-phase-implementation-plan.md) — detailed action items for the immediate next stage
- [IAM Architecture Audit](productization-evolution/iam-architecture-audit.md) — codebase-wide IAM capability audit with file:line evidence
- [Productization Research](productization-evolution/productization-research.md) — market/competitor/pricing analysis (input to the evolution plan)
- [Benchmark Facility Design](productization-evolution/in-progress/benchmark-facility-design.md) — Phase 0 HTTP performance baseline design (M1 done, M2–M4 pending)

## Performance

All performance-optimization program docs live in
[performance-optimization/](performance-optimization/) — the program brief,
analysis reports (static + instrumented), the non-code optimization plan, and
the rolling optimization-wave implementation plans:

- [Program Brief (prompt)](performance-optimization/performance-optimization-prompt.md) — methodology, acceptance rules
- [Wave-1 Static Analysis Report](performance-optimization/performance-optimization-report.md) — baseline parse, bottleneck table (pre-instrumentation)
- [Instrumented Hotspot Report](performance-optimization/performance-hotspot-instrumented-report.md) — pg_stat_statements ledger + stage-probe decomposition + gdb sampling; the quantified lever table
- [Non-Code Optimization Plan](performance-optimization/noncode-performance-optimization.md) — config/DB-level wave (delivered) + §十 backlog
- [Wave-2 Optimization Plan](performance-optimization/optimization-wave-2-plan.md) — code-level levers from the instrumented findings (caching, RTT elimination, tz-lock)
- [Backend Memory Retention Investigation](performance-optimization/backend-memory-retention-investigation.md) — discovery-path unbounded leak (~730 B/request), root of the comparison table's heaviest-stack RSS

## Archive

Historical PRDs, design documents, and iteration plans live under
[history/](history/README.md) — kept for traceability, not current-state
reference.
