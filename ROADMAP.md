---
doc-grammar: roadmap
---

# Roadmap

The governed dependency graph of this project's features. Each item is a
heading-keyed unit identified by its `<phase>:<kind>/<slug>` id.

Mutate the graph with `stackctl roadmap` verbs (run `stackctl roadmap --help`
for the full surface): `add` a new item, `advance` its status, `decompose`,
`reclassify`, `defer`, and `cluster` / `group` to gather existing items under a
created-or-reused parent. Example — cluster items under a new epic with a
dependency chain:

    stackctl roadmap cluster multi:feature/epic --children design:feature/a,impl:feature/b --chain --apply

For an edit that has no verb yet (e.g. moving a `part-of` / `depends-on` edge):
edit this file directly, then run `stackctl roadmap order` to revalidate the
graph (it fails loud on a cycle / dangling ref / duplicate id).

## design:feature/svf-vertical-slice
- status: closed
- validated: yes
- analyze-clean: yes
- spec: specs/svf-vertical-slice
Milestone 1: prove the acfx spine end-to-end with a State-Variable Filter — core abstractions (Effect concept, ProcessContext, AudioBlock, Parameter model, ProcessorNode boundary), the SVF effect (host-tested), desktop workbench + plugin fully working, Daisy+Teensy cross-compile/link proven, CMake presets + CPM pinning + CI

## design:feature/workbench-audio-config
- status: closed
- validated: yes
- analyze-clean: yes
- spec: specs/workbench-audio-config

## impl:feature/modulated-delay
- status: shipped
- spec: specs/modulated-delay

## design:feature/program-scaffolding
- status: planned
- spec: specs/program-scaffolding
