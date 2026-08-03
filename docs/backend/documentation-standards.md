# Documentation Organization Standards

## Directory Structure

This project uses a hierarchical documentation organization to maintain clarity and consistency.

```
authforge/
└── docs/                              # All project documentation
    ├── README.md                      # Documentation portal / index
    ├── backend/                       # Backend-specific documentation
    │   ├── api-reference.md           # API documentation
    │   ├── architecture-overview.md   # System architecture
    │   ├── ci-cd-guide.md             # CI/CD practices
    │   ├── configuration-guide.md
    │   ├── observability.md
    │   └── (other technical guides)
    ├── ops/                           # Operations & deployment
    │   ├── deployment.md
    │   ├── security-checklist.md
    │   └── account-lockout.md
    ├── admin/                         # Admin console docs (e.g. E2E guide, test cases)
    ├── frontend/                      # Frontend-specific docs (e.g. test cases)
    └── history/                       # Archived PRDs / design docs / iteration plans
        ├── PRD/
        ├── design/
        └── README.md
```

## Document Placement Guidelines

### Project-Level Documents (`docs/`)
- **When to use:** Cross-cutting concerns affecting both backend and frontend
- **Examples:** Overall project architecture, deployment guides, contributing guidelines
- **Current status:** Rarely used, most docs are component-specific

### Backend Documents (`docs/backend/`)
- **When to use:** Backend-specific technical documentation
- **Categories:**
  - **Technical guides**: API reference, architecture, testing, CI/CD, storage, security, etc.
  - Historical design specs/plans live under `docs/history/design/superpowers/` (archived)
- **Examples:** API design, database schemas, CI/CD workflows, security guides

### Frontend Documents (`docs/frontend/`)
- **When to use:** Frontend-specific documentation
- **Examples:** Component design, UI/UX guidelines, frontend deployment

## Naming Conventions

### Design Specifications
- **Format:** `YYYY-MM-DD-<topic>-design.md`
- **Example:** `2026-04-14-multiplatform-ci-design.md`
- **Location:** `docs/history/design/superpowers/specs/` (archived)

### Implementation Plans
- **Format:** `YYYY-MM-DD-<topic>-plan.md`
- **Example:** `2026-04-14-multiplatform-ci-plan.md`
- **Location:** `docs/history/design/superpowers/plans/` (archived)

### Technical Guides
- **Format:** `<topic>_guide.md` or `<topic>.md`
- **Examples:** `api-reference.md`, `testing-guide.md`, `security-architecture.md`
- **Location:** `docs/backend/` (backend), `docs/ops/` (operations)

## Decision Tree

When creating a new document, ask:

1. **Does it affect multiple components?**
   - Yes → `docs/` (project level, e.g. `docs/README.md` index)
   - No → Continue to next question

2. **Is it backend-specific?**
   - Yes → `docs/backend/`
   - No → Continue to next question

3. **Is it operations/deployment-specific?**
   - Yes → `docs/ops/`
   - No → Continue to next question

4. **Is it frontend-specific?**
   - Yes → `docs/frontend/`
   - No → Consider if it needs to be a separate document

## Standards Evolution

- **Created:** 2026-04-14
- **Purpose:** Resolve ambiguity about document placement
- **Maintainer:** vilas
- **Update process:** Revise this document when organizational patterns change

---

**Remember:** Consistent documentation organization improves discoverability and reduces confusion about where to find or create documentation.
