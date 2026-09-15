# Ordered P0 task package

Status: canonical task index

Read [MASTER-HANDOFF.md](../MASTER-HANDOFF.md) and
[CURRENT_STATUS.md](../../project/CURRENT_STATUS.md) before starting a task.
Task files specify scope and acceptance criteria; this table records their
dependency order, while live status remains in `CURRENT_STATUS.md`.

| # | Task | Current state |
|---:|---|---|
| 01 | [Lifecycle coverage](01-lifecycle-coverage.md) | Accepted |
| 02 | [Production runtime bootstrap](02-production-runtime-bootstrap.md) | Accepted |
| 03 | [Papyrus observer 1.6.1170](03-papyrus-observer-1.6.1170.md) | Accepted |
| 04 | [Offline compatibility analyzer](04-offline-compatibility-analyzer.md) | Accepted |
| 05 | [Compatibility admission](05-compatibility-admission.md) | Accepted |
| 06 | [Verification envelope](06-verification-envelope.md) | Accepted |
| 07 | [Skyrim/SKSE async save contract](07-skyrim-skse-async-save-contract.md) | Local implementation; not accepted |
| 08 | [Power-loss-durable checkpoint](08-power-loss-durable-checkpoint.md) | Local implementation; not accepted |
| 09 | [Deterministic recovery](09-deterministic-recovery.md) | Planned |
| 10 | [Production dry-run pipeline](10-production-dry-run-pipeline.md) | Planned |
| 11 | [Divergent-save reconciliation](11-divergent-save-reconciliation.md) | Planned |
| 12 | [Reviewed quest profiles](12-reviewed-quest-profiles.md) | Planned |
| 13 | [Narrow mutation and two-client validation](13-narrow-mutation-two-client-validation.md) | Planned |
| 14 | [Failure/recovery live validation](14-failure-recovery-live-validation.md) | Planned |
| 15 | [Final adversarial P0 audit](15-final-adversarial-p0-audit.md) | Planned |

The default order is mandatory. Research may run ahead, but code from a later
task is not integrated until every required predecessor is accepted or a
reviewed architecture decision changes the dependency.
