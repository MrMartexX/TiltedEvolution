# Tilted Evolution project documentation

This directory contains design, implementation, validation and project-planning
material for the Skyrim Together Reborn fork.

## Start here

1. [Current project status](project/CURRENT_STATUS.md) — the only document that
   tracks the accepted integration SHA, live CI state and work that exists only
   on local branches.
2. [Architecture](project/ARCHITECTURE.md) — normative ownership, authority,
   persistence and failure-safety rules.
3. [Implementation roadmap](project/IMPLEMENTATION_ROADMAP.md) — dependency
   order for P0 quest synchronization and the wider multiplayer improvements.
4. [Documentation policy](project/DOCUMENTATION_POLICY.md) — document roles,
   status labels and update rules.
5. [Equal-party quest sync index](equal-party-quest-sync/README.md) — detailed
   P0 specifications, task package and validation evidence.
6. [External research index](research/README.md) — dated reviews of upstream
   issues, pull requests, forks and add-ons, with explicit intake decisions.

## Project areas

- `project/` — current status, normative architecture and roadmap.
- `equal-party-quest-sync/` — quest-sync specifications, durability/recovery
  design, schemas and the ordered P0 task package.
- `concepts/` — designs that are not yet implementation or production
  authority; see the [concept index](concepts/README.md).
- `diagnostics/` — diagnostic plans and preserved incident evidence; see the
  [diagnostics index](diagnostics/README.md).
- `research/` — immutable, dated external evidence and adopt/adapt/reject
  decisions; it does not make external code accepted.
- `samples/` — deterministic test/analyzer fixtures.

Documents outside these areas predate this index. They remain valid for their
technical detail, but their embedded milestone or "next step" statements are
not the current project status unless [CURRENT_STATUS.md](project/CURRENT_STATUS.md)
explicitly adopts them.
