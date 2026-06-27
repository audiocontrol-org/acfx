# Audit-barrage run

- timestamp: 20260626T011013820Z
- feature: svf-vertical-slice-after_implement
- run dir: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T011013820Z-svf-vertical-slice-after_implement
- prompt: PROMPT.md
- models configured: 3
- models completed: 2

## Per-model results
### claude

- exit code: 0
- duration: 211572 ms
- stdout bytes: 113500
- stderr bytes: 0
- report bytes: 9681
- stdout path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T011013820Z-svf-vertical-slice-after_implement/claude.md
- stderr path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T011013820Z-svf-vertical-slice-after_implement/stderr/claude.txt
- events path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T011013820Z-svf-vertical-slice-after_implement/claude.events.ndjson
- timed out: no
- terminal state: completed
- enforcement: enforced
- liveness: monitored (window 300s)
- timeout basis: derived (payload 34486 bytes × 13 s/KB, floor 420) → 438 s

### codex

- exit code: 0
- duration: 39808 ms
- stdout bytes: 2190
- stderr bytes: 37004
- report bytes: 2190
- stdout path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T011013820Z-svf-vertical-slice-after_implement/codex.md
- stderr path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T011013820Z-svf-vertical-slice-after_implement/stderr/codex.txt
- timed out: no
- terminal state: completed
- enforcement: enforced
- liveness: monitored (window 60s)
- timeout basis: derived (payload 34486 bytes × 7 s/KB, floor 300) → 300 s

### sonnet

- exit code: 143
- duration: 438588 ms
- stdout bytes: 68110
- stderr bytes: 0
- report bytes: 0
- stdout path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T011013820Z-svf-vertical-slice-after_implement/sonnet.md
- stderr path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T011013820Z-svf-vertical-slice-after_implement/stderr/sonnet.txt
- events path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T011013820Z-svf-vertical-slice-after_implement/sonnet.events.ndjson
- timed out: yes
- terminal state: timed-out
- enforcement: enforced
- liveness: monitored (window 300s)
- timeout basis: derived (payload 34486 bytes × 13 s/KB, floor 420) → 438 s

## Fleet report

- configured: 3, produced: 2  ⚠ DEGRADED
- claude: completed [enforced, monitored]
- codex: completed [enforced, monitored]
- sonnet: timed-out [enforced, monitored]

