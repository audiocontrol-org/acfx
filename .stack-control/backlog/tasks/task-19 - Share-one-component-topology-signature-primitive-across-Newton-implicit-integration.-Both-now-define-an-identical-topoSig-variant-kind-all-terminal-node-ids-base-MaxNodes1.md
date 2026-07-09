---
id: TASK-19
title: >-
  Share one component-topology-signature primitive across Newton +
  implicit-integration. Both now define an identical topoSig (variant kind + all
  terminal node ids, base-(MaxNodes+1) fold): NewtonSolver::topoSig (private
  static) and acfx::integration::componentTopoSig (topology-signature.h).
  Replicated because implicit-integration's charter forbids modifying the Newton
  sibling. Extract ONE shared definition into the frozen circuit vocabulary and
  have both call it, so there is a single definition of 'planned topology' (PR
  #22 review concern). Cross-primitive; touches shipped Newton.
status: To Do
assignee: []
created_date: '2026-07-09 18:34'
updated_date: '2026-07-09 18:34'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 19000
---


