---
id: TASK-8
title: 'tape-dynamics: measured parameter tuning (OQ3) + drive=0 gain-staging'
status: To Do
assignee: []
created_date: '2026-07-04 01:34'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
references:
  - 'https://github.com/audiocontrol-org/acfx/pull/15'
ordinal: 8000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
tape-dynamics OQ3 measured parameter tuning: concrete first-cut ranges shipped (drive 0-24dB, saturation->Ms 0.1-2, width->k 0.1-2, presets none/glue/saturate). Tune against the analysis harness. Includes drive=0dB gain-staging: harness measures ~-15dB level drop at drive=0dB (Langevin small-signal slope + Ms/output staging); decide whether a neutral setting normalizes toward ~unity OUTPUT level.
<!-- SECTION:DESCRIPTION:END -->
