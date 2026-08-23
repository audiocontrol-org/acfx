---
id: TASK-28
title: resolve-tiers-rejects-lettered-task-ids
status: To Do
assignee: []
created_date: '2026-08-23 19:40'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
references:
  - 'plugins/stack-control/src/execute/tasks-tier-parser.ts:42'
ordinal: 28000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
stackctl resolve-tiers refuses any tasks.md that uses a lettered task id (T010a, T012a), blocking the execute front door for the whole spec. Root cause: plugins/stack-control/src/execute/tasks-tier-parser.ts ID_AT_START = /^(T\d+)\b\s*/ — for "T010a", T\d+ consumes "T010" and then \b demands a boundary between "0" and "a", both word chars, so the match fails outright and the line is reported as category missing-id ("task checkbox has no T-id"). Lettered suffixes are a legitimate, documented convention: they let an operator INSERT a task without renumbering, which matters precisely because the execution ledger (.stack-control/execute/<feature>.ledger.jsonl) is keyed by task id — a renumber orphans completed ledger entries and makes them re-dispatch on resume. Also note the parse error fires regardless of checkbox state, so two already-completed, already-ledgered tasks refuse the entire run. Verified upstream /Users/orion/work/deskwork/plugins/stack-control at v0.59.0 carries the identical regex, so this is an unfixed upstream defect and not a stale plugin cache. Likely fix: widen to /^(T\d+[a-z]?)\b\s*/ (plus parser tests covering a lettered id, and a check that duplicate-id detection still distinguishes T010 from T010a). Surfaced while resuming specs/nucleo-f446-adapter at Phase 5; T010a and T012a were authored and ledgered by the prior session, which evidently never re-ran the gate afterward.
<!-- SECTION:DESCRIPTION:END -->
