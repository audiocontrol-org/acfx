# Contract — Program roadmap nodes & edges (the interface future sub-projects consume)

This is the governance "interface" the scaffolding produces: the shape of the roadmap that
later sub-project sessions read to know what to build next and in what order. It is built via
`stackctl roadmap` (dry-run → apply); identifiers satisfy the `<phase>:<kind>/<slug>` grammar
(phase ∈ {design,plan,impl,multi}; kind ∈ {feature,primitive,fix,gap}).

## Node shapes

- **Program node** — `multi:feature/progressive-dsp-platform`. The root of the program; its
  spec/ref points at the prospectus. All phases are `part-of` it.
- **Phase cluster (×9)** — `multi:feature/phase-<slug>`. One per prospectus phase. `part-of`
  the program; phase N+1 `depends-on` phase N.
- **Headline deliverable** — `design:<kind>/<slug>`, status `planned`, `part-of` its phase.
  `kind` = `primitive` for reusable DSP blocks, `feature` for composed effects, `gap` for
  cross-cutting/infra. Becomes a real spec (`impl:...`) when its sub-project is defined.
- **Shipped work** — existing nodes keep their id/status (e.g. `impl:feature/modulated-delay`
  (shipped)); only a `part-of` edge to Phase 1 is added.

## Edge semantics

- `part-of` — composition: deliverable → phase, phase → program. Renders the containment tree.
- `depends-on` — ordering: phase N+1 → phase N; a deliverable → the three-layer-structure item
  where it requires the reorg. Drives `roadmap next` / `order`.

## Consumer obligations (future sessions)

| Consumer | Uses the roadmap to… |
|---|---|
| `/stack-control:define` (next sub-project) | resolve the next-ready item, attach its spec to the deliverable node (compass precondition) |
| `/stack-control:roadmap` | answer next-ready / why-blocked / order; render the program graph |
| a contributor | see the whole program, what's done (Phase 1 shipped work), and what's next |

## Invariants

- No orphan deliverable: every deliverable is `part-of` exactly one phase; every phase is
  `part-of` the program.
- Acyclic: `depends-on` forms a DAG (phases strictly ordered 1→9).
- Next-ready after scaffolding = the Phase-1 measurement-infrastructure deliverable.
- Building is idempotent: re-running `add`/`add-edge` does not duplicate nodes/edges.
