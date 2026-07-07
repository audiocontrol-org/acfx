---
id: TASK-14
title: >-
  opamp NullorSolver (opamp-stage-solver.h) duplicates ~200 lines of
  component-abstractions LinearSolver's
  Gaussian-elimination-with-partial-pivoting + fixed-node-reduction +
  companion-stamping; only borderOpAmps() and the bordered sizing are new. A
  separate TYPE is justified (bounded-charter; must not grow LinearSolver into
  MNA), but the shared linear-algebra core could be factored into a common
  helper both call rather than re-derived. Also: opamp-stage-solver.h
  advanceHistory() is noexcept yet calls voltage() which can throw
  std::out_of_range for an out-of-range node id -> std::terminate instead of a
  descriptive throw (defensive only; opamp builders use no inductors so not
  currently reachable). Surfaced by code-review of opamp-stages.
status: To Do
assignee: []
created_date: '2026-07-06 09:33'
updated_date: '2026-07-06 09:33'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 14000
---


