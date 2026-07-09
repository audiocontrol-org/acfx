---
id: TASK-18
title: >-
  reactive-integrator forwarded Newton config is dead: ReactiveIntegrator ctor
  validates+stores maxIterations_/voltageTol_ (contract C1: 'forwarded to
  NewtonSolver') but they are never read/forwarded — step() composes the
  caller-constructed NewtonSolver whose own config governs convergence. Either
  wire the integrator config into the composed solve or remove the vestigial
  params and simplify C1 to dt>0 (ripples to contract C1 + T003
  construction-validation). Surfaced during implicit-integration execute (US7).
status: To Do
assignee: []
created_date: '2026-07-09 07:57'
updated_date: '2026-07-09 07:57'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
ordinal: 18000
---


