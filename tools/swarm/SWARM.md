# SWARM — trigger protocol + capture convention

## Trigger

- Orchestrator dispatches via `task()` with `subagent_type = <role.type>`
  (one of: oracle, explorer, explore, fixer, librarian, designer, councillor).
- Bake the role's `mission` + `output_contract` + `todo_ref` into the prompt.
- Roster `@names` (swarm-001…swarm-100) are **chat routing only** — never
  `subagent_type`. There is no `swarm-001` agent type.

## Concurrency

- Max **2–3 concurrent lanes** (credit limit observed at 5). Rest queue.
- Never parallelize overlapping writes. Only `write (build allowed)` roles
  may build (`swarm-013`, `swarm-097`); everyone else is READ-ONLY.

## Tier protocol

- **Majors (001–020)** run first; each defines a bounded fix or DONE/OPEN.
- **Minors (021–100)** fortify: docs, per-shader contracts, evidence,
  log-pattern research, cross-refs. They feed majors.
- Minors **never edit code majors own**; minors never edit code at all.

## Capture

Every bot returns a reconciliation report:

```
role-id | files-read | findings-table | bounded-fix or DONE/OPEN verdict
```

- Findings table follows the role's `output_contract` schema.
- Orchestrator reconciles reports into TODO.md / WORKFLOW_PLAN.md only.
  Bots propose patch lines; they do not apply them.

## 100-item mapping

- Roles map to todo slots via `todo_ref` (TODO.md / WORKFLOW_PLAN.md /
  roadmap phase, or null).
- Unmapped (`null`) roles take the next OPEN item from dispatch-checklist.md.
