// Doc auto-resolver (design §4.5, contract: specs/svf-training-site/contracts/
// artifact-registry.md) — the build-time half of "Go deeper" (§3.6).
//
// From a lesson's typed `LessonMeta` it resolves the effect's CURRENT
// spec / plan / tasks / tests / implementation / roadmap node to concrete repo
// paths + GitHub blob links, so those links are GENERATED from metadata rather
// than hand-maintained. Runs during `astro build` (Node) — this module owns the
// only `node:fs` / `node:child_process` access in repo-refs.
//
// Load-bearing invariant (FR-009 / SC-005): a declared anchor that no longer
// resolves — a `sourcePaths` file that was moved/deleted, a missing spec/plan/
// tasks/tests file, or a roadmap node no longer present as a heading in
// ROADMAP.md — THROWS `UnresolvedAnchorError`. Because it runs at build time,
// that fails `astro build` loudly instead of shipping a silent dead link.
//
// Dependency invariant (design review #2): the resolver reads repository paths
// as DOCUMENTATION METADATA only. It checks a path's existence and reads
// markdown (ROADMAP.md) — it MUST NOT import, parse, or otherwise depend on C++
// implementation semantics. It reads the repo as a filesystem index, never as a
// program (design §4.2 "Hard dependency invariant").

import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

/**
 * Typed lesson metadata (design §4.5). A lesson declares its `effect` id, its
 * roadmap node, and the repository anchors it maps to; strictly typed so a bad
 * field is a build error (Principle IX). Authored per-lesson in
 * `src/content/<effect>/lesson.meta.ts`.
 */
export interface LessonMeta {
  readonly effect: 'svf';
  readonly roadmapNode: 'design:feature/svf-training-site';
  readonly repoAnchors: {
    /** Spec-Kit feature dir, e.g. "specs/svf-training-site". */
    readonly specDir: string;
    /** Feature slug, e.g. "svf-training-site". */
    readonly featureSlug: string;
    /** Real implementation files the lesson references (repo-relative). */
    readonly sourcePaths: readonly string[];
  };
}

/** A resolved repository reference: repo-relative path + its GitHub blob URL. */
export interface RepoRef {
  /** Repo-relative path, e.g. "specs/svf-training-site/spec.md". */
  readonly repoPath: string;
  /** `https://github.com/<owner>/<repo>/blob/<branch>/<repoPath>`. */
  readonly url: string;
}

/** The resolved "Go deeper" link set for a lesson (design §4.5). */
export interface GoDeeperLinks {
  readonly spec: RepoRef;
  readonly plan: RepoRef;
  readonly tasks: RepoRef;
  readonly tests: readonly RepoRef[];
  readonly implementation: readonly RepoRef[];
  readonly roadmapNode: { readonly id: string; readonly found: boolean };
}

/**
 * Thrown when a declared anchor no longer resolves — a missing repo path or a
 * roadmap node absent from ROADMAP.md. At build time this fails `astro build`
 * (FR-009 / SC-005), surfacing repo drift instead of hiding it behind a dead
 * link.
 */
export class UnresolvedAnchorError extends Error {
  constructor(message: string) {
    super(`unresolved repo anchor: ${message}`);
    this.name = 'UnresolvedAnchorError';
  }
}

/** Branch used to construct GitHub blob URLs (design §4.5 uses `main`). */
const DEFAULT_BRANCH = 'main';

/**
 * Walk up from this module's directory to the repository root (the dir holding
 * `.git`, a directory in a normal clone or a file in a worktree). Throws if no
 * `.git` is found before the filesystem root.
 */
function findRepoRoot(): string {
  let dir = dirname(fileURLToPath(import.meta.url));
  for (;;) {
    if (existsSync(join(dir, '.git'))) {
      return dir;
    }
    const parent = dirname(dir);
    if (parent === dir) {
      throw new UnresolvedAnchorError(
        `repository root not found: no ".git" in any ancestor of ${dirname(fileURLToPath(import.meta.url))}`,
      );
    }
    dir = parent;
  }
}

/** GitHub `owner/repo`, derived from the `origin` remote URL. */
interface RepoSlug {
  readonly owner: string;
  readonly repo: string;
}

/**
 * Derive `owner/repo` from `git remote get-url origin`, accepting both SSH
 * (`git@github.com:owner/repo(.git)`) and HTTPS
 * (`https://github.com/owner/repo(.git)`) forms.
 */
function repoSlug(repoRoot: string): RepoSlug {
  let remoteUrl: string;
  try {
    remoteUrl = execFileSync('git', ['-C', repoRoot, 'remote', 'get-url', 'origin'], {
      encoding: 'utf-8',
    }).trim();
  } catch (error) {
    throw new UnresolvedAnchorError(
      `could not read git "origin" remote at ${repoRoot}: ${error instanceof Error ? error.message : String(error)}`,
    );
  }
  const match = /[:/](?<owner>[^/:]+)\/(?<repo>[^/]+?)(?:\.git)?$/.exec(remoteUrl);
  const owner = match?.groups?.owner;
  const repo = match?.groups?.repo;
  if (owner === undefined || repo === undefined) {
    throw new UnresolvedAnchorError(`could not parse owner/repo from origin URL "${remoteUrl}"`);
  }
  return { owner, repo };
}

/**
 * Resolve one repo-relative path to a `RepoRef`, verifying it exists under the
 * repo root. Throws `UnresolvedAnchorError` (naming the anchor) if it does not.
 */
function resolveRef(repoRoot: string, slug: RepoSlug, anchor: string, repoPath: string): RepoRef {
  const absolute = resolve(repoRoot, repoPath);
  if (!existsSync(absolute)) {
    throw new UnresolvedAnchorError(`${anchor} → "${repoPath}" does not exist under ${repoRoot}`);
  }
  return {
    repoPath,
    url: `https://github.com/${slug.owner}/${slug.repo}/blob/${DEFAULT_BRANCH}/${repoPath}`,
  };
}

/**
 * Verify a roadmap node id exists as a Markdown heading (`## <id>`) in
 * ROADMAP.md. Reads the file as text metadata only. Throws if ROADMAP.md is
 * missing or the heading is absent.
 */
function resolveRoadmapNode(repoRoot: string, id: string): { id: string; found: boolean } {
  const roadmapPath = join(repoRoot, 'ROADMAP.md');
  if (!existsSync(roadmapPath)) {
    throw new UnresolvedAnchorError(`roadmap node "${id}" → ROADMAP.md not found at ${roadmapPath}`);
  }
  const heading = `## ${id}`;
  const found = readFileSync(roadmapPath, 'utf-8')
    .split('\n')
    .some((line) => line.trim() === heading);
  if (!found) {
    throw new UnresolvedAnchorError(`roadmap node "${id}" not found as heading "${heading}" in ROADMAP.md`);
  }
  return { id, found: true };
}

/**
 * Resolve a lesson's `repoAnchors` into concrete "Go deeper" links at build
 * time. Every anchor must resolve; the first that does not throws
 * `UnresolvedAnchorError`, failing the build (FR-009 / SC-005).
 *
 * Anchors:
 * - spec/plan/tasks — `<specDir>/{spec,plan,tasks}.md`
 * - tests — the effect's host test, by convention `tests/core/<effect>-test.cpp`
 * - implementation — each entry of `repoAnchors.sourcePaths`
 * - roadmap node — `roadmapNode`, verified present as a heading in ROADMAP.md
 */
export function resolveGoDeeper(meta: LessonMeta): GoDeeperLinks {
  const repoRoot = findRepoRoot();
  const slug = repoSlug(repoRoot);
  const { specDir, sourcePaths } = meta.repoAnchors;

  const testPath = `tests/core/${meta.effect}-test.cpp`;

  return {
    spec: resolveRef(repoRoot, slug, 'spec', join(specDir, 'spec.md')),
    plan: resolveRef(repoRoot, slug, 'plan', join(specDir, 'plan.md')),
    tasks: resolveRef(repoRoot, slug, 'tasks', join(specDir, 'tasks.md')),
    tests: [resolveRef(repoRoot, slug, 'tests', testPath)],
    implementation: sourcePaths.map((path, index) =>
      resolveRef(repoRoot, slug, `implementation[${index}]`, path),
    ),
    roadmapNode: resolveRoadmapNode(repoRoot, meta.roadmapNode),
  };
}
