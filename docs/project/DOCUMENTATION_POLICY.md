# Documentation policy

Status: normative documentation process

## One source for each kind of truth

- `CURRENT_STATUS.md` is the only mutable status snapshot. It records accepted
  integration state, CI evidence, local-only work and the next ordered action.
- `ARCHITECTURE.md` owns cross-system invariants and authority boundaries.
- `IMPLEMENTATION_ROADMAP.md` owns dependency order and completion gates.
- `equal-party-quest-sync/tasks/` contains stable task specifications. A task's
  status is tracked in the task index and `CURRENT_STATUS.md`, not inferred from
  the existence of its file.
- Technical design files explain a subsystem. They do not declare the whole P0
  closed.
- Evidence and incident reports are immutable observations tied to exact builds,
  environments and dates. Later results append or supersede them explicitly.
- External research records exact source URLs/SHAs, review date, license status,
  affected authority/wire/ABI boundaries, missing evidence and an explicit
  adopt/adapt/reject decision. An upstream merge or green CI is not local
  acceptance.
- Concept documents describe possible future designs and never grant runtime
  authority.

## Required document status

Every new planning, concept or evidence document must identify its role near
the title using one of these terms:

- `Normative` — an accepted invariant or required process.
- `Accepted` — integrated implementation with its required validation.
- `Local implementation` — code exists outside the accepted integration branch.
- `In progress` — active work without a completed acceptance gate.
- `Planned` — approved direction with no accepted implementation.
- `Blocked` — the exact external or architectural dependency is documented.
- `Evidence snapshot` — observations for an exact build/environment.
- `Historical` — retained context whose milestone statements are superseded.
- `Superseded` — kept only for traceability and linked to its replacement.

## SHA and CI rules

- Mutable branch SHAs belong only in `CURRENT_STATUS.md` or an immutable evidence
  report.
- Roadmaps and task specifications point to the current-status file instead of
  embedding a supposedly permanent baseline.
- A task is not `Accepted` merely because its branch exists or local tests pass.
  Acceptance requires integration ancestry plus all required CI/live gates.
- Every accepted code-changing slice records exact Linux and Windows TPTests
  assertion/test-case counts.
- Workflow-level success is insufficient when the required build/test step was
  skipped.

## Update protocol

After every accepted task or corrective slice:

1. live-check the PR, remote integration HEAD and required workflows;
2. update `CURRENT_STATUS.md` in the same documentation change or immediately
   following it;
3. update the task index status and evidence links;
4. change the roadmap only when dependency order or scope changed;
5. mark stale prose as historical instead of silently rewriting old evidence;
6. validate all relative Markdown links and required task topology with:

   ```text
   python Tools/documentation/validate_docs.py
   ```

Before implementing from an external source, refresh mutable PR tips and branch
heads, pin the reviewed source commit, and preserve attribution. Code without a
compatible license may inform behavior and tests but must not be copied.

This policy deliberately separates implementation claims from plans. A clear
document is not proof that the described runtime path is connected or safe.
