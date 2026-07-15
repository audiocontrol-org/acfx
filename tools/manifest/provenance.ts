// Shared `sourceProvenance` definition for both fragment producers
// (`wasm-fragment.ts`, and mirrored in the native `tools/lesson-assets`
// C++ tool) and the non-building staleness guard (`tools/staleness-guard.ts`).
//
// `sourceProvenance` is the DSP SOURCE hash, not the whole-tree `git rev-parse
// HEAD`. A whole-tree hash would make the staleness guard fire after ANY
// commit anywhere in the repo (docs, unrelated modules, ...), which is
// useless. Per docs/superpowers/specs/2026-07-14-svf-training-site-design.md
// §4.4 and specs/svf-training-site/contracts/lesson-asset-manifest.md, the
// value tracks only the two subtrees that actually feed the lesson assets:
// `core/` (the DSP core) and `adapters/web` (the web adapter, including the
// Emscripten target).
//
//   sourceProvenance = "<coreTreeSha>:<webTreeSha>"
//   coreTreeSha = `git rev-parse HEAD:core`
//   webTreeSha  = `git rev-parse HEAD:adapters/web`
//
// The native tool (`tools/lesson-assets/asset-tool-main.cpp`) computes this
// identically; the assembler (`assemble.ts`) fails loud if the two
// producers' fragments disagree, so keep both implementations in sync.

import { execFileSync } from "node:child_process";

function gitTreeSha(cwd: string, subtree: string): string {
  return execFileSync("git", ["rev-parse", `HEAD:${subtree}`], { cwd, encoding: "utf8" }).trim();
}

export function computeSourceProvenance(cwd: string): string {
  const coreTreeSha = gitTreeSha(cwd, "core");
  const webTreeSha = gitTreeSha(cwd, "adapters/web");
  return `${coreTreeSha}:${webTreeSha}`;
}
