# Audit-barrage run

- timestamp: 20260626T021955993Z
- feature: svf-vertical-slice-after_implement
- run dir: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T021955993Z-svf-vertical-slice-after_implement
- prompt: PROMPT.md
- models configured: 3
- models completed: 2

## Per-model results
### claude

- exit code: 0
- duration: 193068 ms
- stdout bytes: 102316
- stderr bytes: 0
- report bytes: 5022
- stdout path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T021955993Z-svf-vertical-slice-after_implement/claude.md
- stderr path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T021955993Z-svf-vertical-slice-after_implement/stderr/claude.txt
- events path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T021955993Z-svf-vertical-slice-after_implement/claude.events.ndjson
- timed out: no
- terminal state: completed
- enforcement: enforced
- liveness: monitored (window 300s)
- timeout basis: derived (payload 25891 bytes × 13 s/KB, floor 420) → 420 s

### codex

- exit code: 0
- duration: 115056 ms
- stdout bytes: 0
- stderr bytes: 40680
- report bytes: 0
- stdout path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T021955993Z-svf-vertical-slice-after_implement/codex.md
- stderr path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T021955993Z-svf-vertical-slice-after_implement/stderr/codex.txt
- timed out: no
- terminal state: killed-no-liveness
- enforcement: enforced
- liveness: monitored (window 60s)
- staleness at kill: 61.5 s
- timeout basis: derived (payload 25891 bytes × 7 s/KB, floor 300) → 300 s

### sonnet

- exit code: 0
- duration: 174850 ms
- stdout bytes: 120882
- stderr bytes: 0
- report bytes: 4597
- stdout path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T021955993Z-svf-vertical-slice-after_implement/sonnet.md
- stderr path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T021955993Z-svf-vertical-slice-after_implement/stderr/sonnet.txt
- events path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T021955993Z-svf-vertical-slice-after_implement/sonnet.events.ndjson
- timed out: no
- terminal state: completed
- enforcement: enforced
- liveness: monitored (window 300s)
- timeout basis: derived (payload 25891 bytes × 13 s/KB, floor 420) → 420 s

## Fleet report

- configured: 3, produced: 2  ⚠ DEGRADED
- claude: completed [enforced, monitored]
- codex: killed-no-liveness [enforced, monitored]
- sonnet: completed [enforced, monitored]

