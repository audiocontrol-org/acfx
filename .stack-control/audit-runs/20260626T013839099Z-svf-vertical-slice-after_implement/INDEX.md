# Audit-barrage run

- timestamp: 20260626T013839099Z
- feature: svf-vertical-slice-after_implement
- run dir: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T013839099Z-svf-vertical-slice-after_implement
- prompt: PROMPT.md
- models configured: 3
- models completed: 2

## Per-model results
### claude

- exit code: 0
- duration: 137285 ms
- stdout bytes: 83163
- stderr bytes: 0
- report bytes: 7980
- stdout path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T013839099Z-svf-vertical-slice-after_implement/claude.md
- stderr path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T013839099Z-svf-vertical-slice-after_implement/stderr/claude.txt
- events path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T013839099Z-svf-vertical-slice-after_implement/claude.events.ndjson
- timed out: no
- terminal state: completed
- enforcement: enforced
- liveness: monitored (window 300s)
- timeout basis: derived (payload 34587 bytes × 13 s/KB, floor 420) → 440 s

### codex

- exit code: 0
- duration: 65040 ms
- stdout bytes: 0
- stderr bytes: 34890
- report bytes: 0
- stdout path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T013839099Z-svf-vertical-slice-after_implement/codex.md
- stderr path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T013839099Z-svf-vertical-slice-after_implement/stderr/codex.txt
- timed out: no
- terminal state: killed-no-liveness
- enforcement: enforced
- liveness: monitored (window 60s)
- staleness at kill: 64.8 s
- timeout basis: derived (payload 34587 bytes × 7 s/KB, floor 300) → 300 s

### sonnet

- exit code: 0
- duration: 363931 ms
- stdout bytes: 131096
- stderr bytes: 0
- report bytes: 7833
- stdout path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T013839099Z-svf-vertical-slice-after_implement/sonnet.md
- stderr path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T013839099Z-svf-vertical-slice-after_implement/stderr/sonnet.txt
- events path: /Users/orion/work/acfx-work/platform-foundation/.stack-control/audit-runs/20260626T013839099Z-svf-vertical-slice-after_implement/sonnet.events.ndjson
- timed out: no
- terminal state: completed
- enforcement: enforced
- liveness: monitored (window 300s)
- timeout basis: derived (payload 34587 bytes × 13 s/KB, floor 420) → 440 s

## Fleet report

- configured: 3, produced: 2  ⚠ DEGRADED
- claude: completed [enforced, monitored]
- codex: killed-no-liveness [enforced, monitored]
- sonnet: completed [enforced, monitored]

