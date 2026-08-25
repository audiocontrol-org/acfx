---
id: TASK-26
title: backlog-capture-silently-drops-duplicate-ref
status: To Do
assignee: []
created_date: '2026-08-23 17:28'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
references:
  - .stack-control/backlog/config.yml
ordinal: 26000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
stackctl backlog capture dedups on the --ref value: invoking it with a NEW id and a NEW body but a --ref that an existing item already carries returns 'backlog capture: TASK-NN (already captured for ref <path>)' with EXIT 0, creates nothing, and silently discards the supplied body. Observed live 2026-08-23: a distinct finding (a firmware .elf linking successfully with a zero-sized vector table) was captured against specs/nucleo-f446-adapter/tasks.md, a ref already held by TASK-22 (an unrelated build-order gap); the item was not created and the body was lost. The exit code and message both read as success, so the loss is invisible unless the operator re-reads the store. Two independent findings frequently share one ref path, so ref is the wrong dedup key. Expected behaviour: dedup on id, or warn loudly and non-zero when a body is discarded, or append the body to the matched item. Workaround: pass a different --ref.
<!-- SECTION:DESCRIPTION:END -->
