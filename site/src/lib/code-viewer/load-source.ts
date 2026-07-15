// Build-time source loader for the "Build it" code viewer.
//
// Given a repo-relative path and an optional line range, this reads the REAL
// file out of the local repo checkout during `astro build` and returns the text
// to display plus a GitHub blob deep-link. The site stays self-contained: the
// code comes from the local source tree that is always present in the checkout,
// with NO runtime GitHub fetch and NO dependency on the `build/` output.
//
// Dependency invariant (design §4.2 review #2, mirrored from repo-refs): this
// reads repository files as DOCUMENTATION METADATA — plain text to display. It
// MUST NOT import, parse, or otherwise depend on C++ (or TypeScript) program
// semantics; it treats every file as an opaque line array. Syntax highlighting
// is applied downstream by Shiki as a purely lexical pass, never a semantic one.
//
// Fail-loud invariant (matches the resolver): a referenced file that does not
// exist, or a line range outside the file, THROWS at build time so `astro build`
// fails on repo drift instead of shipping a broken or empty code panel.

import { existsSync, readFileSync } from 'node:fs';
import { extname, resolve } from 'node:path';

import { DEFAULT_BRANCH, findRepoRoot, repoSlug } from '@lib/repo-refs/resolver';

/** Shiki language ids this viewer supports, derived from the file extension. */
export type SourceLang = 'cpp' | 'typescript' | 'markdown';

/** A loaded, ready-to-highlight source excerpt with its GitHub deep-link. */
export interface LoadedSource {
  /** The whole file's text (trailing newline stripped), for the expanded view. */
  readonly fullText: string;
  /** Just the requested line range, for the collapsed excerpt view. */
  readonly excerptText: string;
  /** Real 1-based first line of the excerpt (matches the file / GitHub). */
  readonly startLine: number;
  /** Real 1-based last line of the excerpt (inclusive). */
  readonly endLine: number;
  /** Shiki language id derived from the extension. */
  readonly lang: SourceLang;
  /** `…/blob/main/<repoPath>#L<start>-L<end>` — deep-links the excerpt range. */
  readonly githubUrl: string;
  /** Total number of lines in the file. */
  readonly lineCount: number;
}

/** Thrown when a referenced file or line range does not resolve at build time. */
export class UnresolvedSourceError extends Error {
  constructor(message: string) {
    super(`unresolved code-viewer source: ${message}`);
    this.name = 'UnresolvedSourceError';
  }
}

/** Map a file extension to a Shiki language; throw on an unsupported type. */
function langForPath(repoPath: string): SourceLang {
  const ext = extname(repoPath).toLowerCase();
  switch (ext) {
    case '.h':
    case '.hpp':
    case '.hh':
    case '.cpp':
    case '.cc':
    case '.cxx':
      return 'cpp';
    case '.ts':
    case '.tsx':
      return 'typescript';
    case '.md':
      return 'markdown';
    default:
      throw new UnresolvedSourceError(`unsupported file type "${ext}" for "${repoPath}"`);
  }
}

/** Input to {@link loadSource}: a repo path and an optional inclusive range. */
export interface LoadSourceInput {
  /** Repo-relative path to a real source file (e.g. "core/.../svf-primitive.h"). */
  readonly repoPath: string;
  /** Real 1-based first line of the excerpt; defaults to 1. */
  readonly startLine?: number;
  /** Real 1-based last line (inclusive); defaults to the last line of the file. */
  readonly endLine?: number;
}

/**
 * Read a file's excerpt (and full text) from the local repo at build time.
 * Throws {@link UnresolvedSourceError} if the file is missing or the range is
 * out of bounds — failing `astro build` loudly on drift.
 */
export function loadSource(input: LoadSourceInput): LoadedSource {
  const { repoPath } = input;
  const repoRoot = findRepoRoot();
  const absolute = resolve(repoRoot, repoPath);
  if (!existsSync(absolute)) {
    throw new UnresolvedSourceError(`"${repoPath}" does not exist under ${repoRoot}`);
  }

  const lang = langForPath(repoPath);
  // Read the file as opaque text (documentation metadata) and split into lines.
  // A single trailing newline is dropped so the last real line is not shadowed
  // by an empty gutter row.
  const raw = readFileSync(absolute, 'utf-8').replace(/\n$/, '');
  const lines = raw.split('\n');
  const lineCount = lines.length;

  const startLine = input.startLine ?? 1;
  const endLine = input.endLine ?? lineCount;

  if (!Number.isInteger(startLine) || !Number.isInteger(endLine)) {
    throw new UnresolvedSourceError(`non-integer line range [${startLine}, ${endLine}] for "${repoPath}"`);
  }
  if (startLine < 1 || endLine < startLine || endLine > lineCount) {
    throw new UnresolvedSourceError(
      `line range [${startLine}, ${endLine}] out of bounds for "${repoPath}" (1..${lineCount})`,
    );
  }

  // slice(startLine - 1, endLine): 1-based inclusive → 0-based exclusive-end.
  const excerptText = lines.slice(startLine - 1, endLine).join('\n');

  const slug = repoSlug(repoRoot);
  const githubUrl = `https://github.com/${slug.owner}/${slug.repo}/blob/${DEFAULT_BRANCH}/${repoPath}#L${startLine}-L${endLine}`;

  return { fullText: raw, excerptText, startLine, endLine, lang, githubUrl, lineCount };
}
