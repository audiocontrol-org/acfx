// T017 -- Local publish step (`make publish-assets`, wiring is a later task).
//
// Uploads the content-hashed lesson assets described by the committed
// manifest (`site/public/manifest/svf.json`) to the PUBLIC Backblaze B2
// bucket `audiocontrol-acfx`, via `rclone`'s b2 backend, setting the
// correct `Content-Type` per asset. Credentials are read at runtime from a
// gitignored YAML file by path (never hardcoded, never logged) and passed
// to the `rclone` child process via `RCLONE_CONFIG_<REMOTE>_*` environment
// variables -- this script itself never prints a secret value.
//
// Each manifest asset's `url` is `${CDN_BASE}/<key>`; the key is the URL
// path's basename and IS the object name to upload to. Static assets
// (audio/response/impulse) live on disk under the asset-tool's output
// directory using that same content-hashed filename. The wasm asset is the
// one exception: on disk it is `build/web/svf.wasm` (not renamed), and gets
// uploaded TO its manifest key (e.g. `svf.ec9e1086.wasm`).
//
// `--dry-run` resolves + sha256-verifies every asset and prints the planned
// upload plan WITHOUT invoking rclone -- this is how a controller validates
// the script before ever touching the live bucket.

import { createHash } from "node:crypto";
import { spawnSync } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { homedir } from "node:os";
import { basename, isAbsolute, join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

import type { AssetEntry, AssetKind, Capability, LessonAssetManifest } from "./manifest/types.js";

const ASSET_KINDS: readonly AssetKind[] = ["wasm", "worklet", "audio", "response", "pole-zero", "impulse"];
const CAPABILITIES: readonly Capability[] = ["audio", "analysis"];

const DEFAULT_MANIFEST_PATH = "site/public/manifest/svf.json";
const DEFAULT_STATIC_DIR = "build/lesson-assets/svf-out";
const DEFAULT_WASM_PATH = "build/web/svf.wasm";
const DEFAULT_CREDENTIALS_PATH = "~/.config/backblaze/b2-audiocontrol-acfx-credentials.yaml";
const DEFAULT_BUCKET = "audiocontrol-acfx";
const DEFAULT_REMOTE_NAME = "acfxpublish";

// ---------------------------------------------------------------------------
// CLI args
// ---------------------------------------------------------------------------

interface Args {
  readonly manifestPath: string;
  readonly staticDir: string;
  readonly wasmPath: string;
  readonly credentialsPath: string;
  readonly bucket: string;
  readonly remoteName: string;
  readonly dryRun: boolean;
}

function parseArgs(argv: readonly string[]): Args {
  const flags = new Map<string, string>();
  let dryRun = false;
  for (const arg of argv) {
    if (arg === "--dry-run") {
      dryRun = true;
      continue;
    }
    const match = /^--([^=]+)=(.*)$/.exec(arg);
    const key = match?.[1];
    const value = match?.[2];
    if (key !== undefined && value !== undefined) {
      flags.set(key, value);
    }
  }
  return {
    manifestPath: flags.get("manifest") ?? DEFAULT_MANIFEST_PATH,
    staticDir: flags.get("static-dir") ?? DEFAULT_STATIC_DIR,
    wasmPath: flags.get("wasm-path") ?? DEFAULT_WASM_PATH,
    credentialsPath: flags.get("credentials") ?? DEFAULT_CREDENTIALS_PATH,
    bucket: flags.get("bucket") ?? DEFAULT_BUCKET,
    remoteName: flags.get("remote-name") ?? DEFAULT_REMOTE_NAME,
    dryRun,
  };
}

// ---------------------------------------------------------------------------
// Manifest parsing (mirrors tools/manifest/types.ts's validation style --
// deliberately not importing its private helpers, which aren't exported).
// ---------------------------------------------------------------------------

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function assertString(value: unknown, field: string): string {
  if (typeof value !== "string") {
    throw new Error(`expected string for "${field}", got ${typeof value}`);
  }
  return value;
}

function assertOptionalNumber(value: unknown, field: string): number | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (typeof value !== "number") {
    throw new Error(`expected number for "${field}", got ${typeof value}`);
  }
  return value;
}

function assertAssetKind(value: unknown, field: string): AssetKind {
  const kind = assertString(value, field);
  if (!(ASSET_KINDS as readonly string[]).includes(kind)) {
    throw new Error(`invalid asset kind "${kind}" for "${field}" (expected one of ${ASSET_KINDS.join(", ")})`);
  }
  return kind as AssetKind;
}

function assertOptionalCapabilities(value: unknown, field: string): readonly Capability[] | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (!Array.isArray(value)) {
    throw new Error(`expected array for "${field}"`);
  }
  return (value as readonly unknown[]).map((entry, index) => {
    if (!(CAPABILITIES as readonly string[]).includes(entry as string)) {
      throw new Error(`invalid capability "${String(entry)}" at "${field}[${index}]"`);
    }
    return entry as Capability;
  });
}

function assertOptionalParams(value: unknown, field: string): Readonly<Record<string, number>> | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (!isRecord(value)) {
    throw new Error(`expected object for "${field}"`);
  }
  const out: Record<string, number> = {};
  for (const [key, entry] of Object.entries(value)) {
    if (typeof entry !== "number") {
      throw new Error(`expected number for "${field}.${key}"`);
    }
    out[key] = entry;
  }
  return out;
}

function parseAssetEntry(value: unknown, index: number, label: string): AssetEntry {
  if (!isRecord(value)) {
    throw new Error(`${label}: assets[${index}] is not an object`);
  }
  const prefix = `${label}.assets[${index}]`;
  return {
    kind: assertAssetKind(value["kind"], `${prefix}.kind`),
    url: assertString(value["url"], `${prefix}.url`),
    sha256: assertString(value["sha256"], `${prefix}.sha256`),
    contentType: assertString(value["contentType"], `${prefix}.contentType`),
    capabilities: assertOptionalCapabilities(value["capabilities"], `${prefix}.capabilities`),
    capabilityVersion: assertOptionalNumber(value["capabilityVersion"], `${prefix}.capabilityVersion`),
    params: assertOptionalParams(value["params"], `${prefix}.params`),
    sampleRate: assertOptionalNumber(value["sampleRate"], `${prefix}.sampleRate`),
    provenance: assertString(value["provenance"], `${prefix}.provenance`),
  };
}

function parseManifest(value: unknown, label: string): LessonAssetManifest {
  if (!isRecord(value)) {
    throw new Error(`${label}: manifest root is not an object`);
  }
  const version = value["version"];
  if (version !== 1) {
    throw new Error(`${label}.version: expected 1, got ${String(version)}`);
  }
  const lesson = value["lesson"];
  if (lesson !== "svf") {
    throw new Error(`${label}.lesson: expected "svf", got ${String(lesson)}`);
  }
  const sourceProvenance = assertString(value["sourceProvenance"], `${label}.sourceProvenance`);
  const rawAssets = value["assets"];
  if (!Array.isArray(rawAssets)) {
    throw new Error(`${label}.assets is not an array`);
  }
  const assets = (rawAssets as readonly unknown[]).map((entry, index) => parseAssetEntry(entry, index, label));
  return { version, lesson, sourceProvenance, assets };
}

function readManifest(path: string): LessonAssetManifest {
  let raw: string;
  try {
    raw = readFileSync(path, "utf8");
  } catch (err) {
    throw new Error(`failed to read manifest ${path}: ${String(err)}`);
  }
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch (err) {
    throw new Error(`failed to parse JSON at ${path}: ${String(err)}`);
  }
  return parseManifest(parsed, path);
}

// ---------------------------------------------------------------------------
// Credentials (flat `key: value` YAML -- keyID / applicationKey / endpoint).
// Deliberately a minimal parser for this one known-flat shape rather than a
// general YAML dependency: fails loud on anything it doesn't recognize
// rather than silently misparsing a secret.
// ---------------------------------------------------------------------------

export interface B2Credentials {
  readonly keyID: string;
  readonly applicationKey: string;
  readonly endpoint: string;
}

function expandHome(path: string): string {
  if (path === "~") {
    return homedir();
  }
  if (path.startsWith("~/")) {
    return join(homedir(), path.slice(2));
  }
  return path;
}

function stripQuotes(value: string): string {
  if (value.length >= 2) {
    const first = value[0];
    const last = value[value.length - 1];
    if ((first === '"' && last === '"') || (first === "'" && last === "'")) {
      return value.slice(1, -1);
    }
  }
  return value;
}

// Parses a minimal flat `key: value` YAML mapping. Not a general YAML
// parser -- deliberately narrow to the one credentials-file shape this
// script reads (no nesting, no lists, no anchors).
export function parseFlatYamlMapping(raw: string): Record<string, string> {
  const out: Record<string, string> = {};
  const lines = raw.split(/\r?\n/);
  for (const line of lines) {
    const trimmed = line.trim();
    if (trimmed === "" || trimmed.startsWith("#")) {
      continue;
    }
    const colonIndex = trimmed.indexOf(":");
    if (colonIndex === -1) {
      throw new Error(`malformed YAML line (expected "key: value"): ${trimmed}`);
    }
    const key = trimmed.slice(0, colonIndex).trim();
    const value = stripQuotes(trimmed.slice(colonIndex + 1).trim());
    if (key === "") {
      throw new Error(`malformed YAML line (empty key): ${trimmed}`);
    }
    out[key] = value;
  }
  return out;
}

// Reads the B2 credentials file BY PATH at runtime. Never hardcode, never
// log, never return anything other than the three fields the caller needs.
export function readB2Credentials(path: string): B2Credentials {
  const resolvedPath = expandHome(path);
  if (!existsSync(resolvedPath)) {
    throw new Error(`credentials file not found: ${resolvedPath}`);
  }
  let raw: string;
  try {
    raw = readFileSync(resolvedPath, "utf8");
  } catch (err) {
    throw new Error(`failed to read credentials file ${resolvedPath}: ${String(err)}`);
  }
  const fields = parseFlatYamlMapping(raw);
  const keyID = fields["keyID"];
  const applicationKey = fields["applicationKey"];
  const endpoint = fields["endpoint"];
  if (keyID === undefined || keyID === "") {
    throw new Error(`credentials file ${resolvedPath} is missing "keyID"`);
  }
  if (applicationKey === undefined || applicationKey === "") {
    throw new Error(`credentials file ${resolvedPath} is missing "applicationKey"`);
  }
  if (endpoint === undefined || endpoint === "") {
    throw new Error(`credentials file ${resolvedPath} is missing "endpoint"`);
  }
  return { keyID, applicationKey, endpoint };
}

// ---------------------------------------------------------------------------
// Upload planning
// ---------------------------------------------------------------------------

export interface PlannedUpload {
  readonly key: string;
  readonly kind: AssetKind;
  readonly localPath: string;
  readonly contentType: string;
  readonly manifestSha256: string;
  readonly actualSha256: string;
}

// The manifest's `url` is `${CDN_BASE}/<key>` -- the key is the URL path's
// basename, and is also the object name uploaded to the bucket.
export function keyFromUrl(url: string): string {
  let pathname: string;
  try {
    pathname = new URL(url).pathname;
  } catch (err) {
    throw new Error(`asset url is not a valid URL: ${url} (${String(err)})`);
  }
  const key = basename(pathname);
  if (key === "" || key === "/") {
    throw new Error(`asset url has no basename: ${url}`);
  }
  return key;
}

// Static assets (audio/response/impulse/worklet/pole-zero) are physically
// stored on disk under their content-hashed filename already. The wasm
// asset is the one exception: on disk it is NOT renamed
// (`build/web/svf.wasm`), so it must be resolved separately and uploaded TO
// its manifest key.
export function resolveLocalPath(asset: AssetEntry, key: string, staticDir: string, wasmPath: string): string {
  if (asset.kind === "wasm") {
    return wasmPath;
  }
  return join(staticDir, key);
}

function sha256File(path: string): string {
  const data = readFileSync(path);
  return createHash("sha256").update(data).digest("hex");
}

// Resolves + sha256-verifies every manifest asset against its local file.
// Fails loud (throws, aggregating all problems) rather than uploading a
// file that doesn't match the manifest.
export function planUploads(manifest: LessonAssetManifest, staticDir: string, wasmPath: string): PlannedUpload[] {
  const plans: PlannedUpload[] = [];
  const problems: string[] = [];

  for (const asset of manifest.assets) {
    const key = keyFromUrl(asset.url);
    const localPath = resolveLocalPath(asset, key, staticDir, wasmPath);

    if (!existsSync(localPath)) {
      problems.push(`${key}: local file not found: ${localPath}`);
      continue;
    }

    const actualSha256 = sha256File(localPath);
    if (actualSha256 !== asset.sha256) {
      problems.push(
        `${key}: sha256 mismatch -- manifest=${asset.sha256} actual=${actualSha256} (local file ${localPath})`,
      );
      continue;
    }

    plans.push({
      key,
      kind: asset.kind,
      localPath,
      contentType: asset.contentType,
      manifestSha256: asset.sha256,
      actualSha256,
    });
  }

  if (problems.length > 0) {
    throw new Error(`publish-assets: ${problems.length} asset(s) failed verification:\n  ${problems.join("\n  ")}`);
  }

  return plans;
}

// ---------------------------------------------------------------------------
// rclone invocation
// ---------------------------------------------------------------------------

// Builds the RCLONE_CONFIG_<REMOTE>_* env-var block for an in-process,
// config-file-free b2 remote. These are only ever set on the SPAWNED
// rclone child process's env (see `uploadOne`) -- never logged, never
// merged into this script's own process.env.
function rcloneEnvFor(remoteName: string, credentials: B2Credentials): Record<string, string> {
  const prefix = `RCLONE_CONFIG_${remoteName.toUpperCase()}_`;
  return {
    [`${prefix}TYPE`]: "b2",
    [`${prefix}ACCOUNT`]: credentials.keyID,
    [`${prefix}KEY`]: credentials.applicationKey,
    [`${prefix}ENDPOINT`]: credentials.endpoint,
  };
}

function uploadOne(plan: PlannedUpload, remoteName: string, bucket: string, credentials: B2Credentials): void {
  const dest = `${remoteName}:${bucket}/${plan.key}`;
  const args = [
    "copyto",
    plan.localPath,
    dest,
    "--header-upload",
    `Content-Type: ${plan.contentType}`,
    "--checksum",
  ];

  const result = spawnSync("rclone", args, {
    env: {
      ...process.env,
      ...rcloneEnvFor(remoteName, credentials),
    },
    stdio: ["ignore", "pipe", "pipe"],
    encoding: "utf8",
  });

  if (result.error) {
    throw new Error(`${plan.key}: failed to spawn rclone: ${String(result.error)}`);
  }
  if (result.status !== 0) {
    const stderr = (result.stderr ?? "").trim();
    throw new Error(`${plan.key}: rclone copyto exited ${String(result.status)}${stderr ? `: ${stderr}` : ""}`);
  }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

function main(): void {
  const args = parseArgs(process.argv.slice(2));
  const cwd = process.cwd();

  const manifestPath = resolve(cwd, args.manifestPath);
  const staticDir = isAbsolute(args.staticDir) ? args.staticDir : resolve(cwd, args.staticDir);
  const wasmPath = isAbsolute(args.wasmPath) ? args.wasmPath : resolve(cwd, args.wasmPath);

  const manifest = readManifest(manifestPath);
  const plans = planUploads(manifest, staticDir, wasmPath);

  process.stdout.write(
    `publish-assets: ${String(plans.length)} asset(s) resolved + sha256-verified from ${manifestPath}\n`,
  );

  if (args.dryRun) {
    for (const plan of plans) {
      process.stdout.write(
        `  [dry-run] would upload ${plan.key} (${plan.kind}) content-type=${plan.contentType} ` +
          `sha256=${plan.actualSha256} <- ${plan.localPath}\n`,
      );
    }
    process.stdout.write(`publish-assets: dry-run complete, ${String(plans.length)} planned upload(s), no rclone invoked\n`);
    return;
  }

  const credentials = readB2Credentials(args.credentialsPath);

  for (const plan of plans) {
    uploadOne(plan, args.remoteName, args.bucket, credentials);
    process.stdout.write(`  uploaded ${plan.key} content-type=${plan.contentType} OK\n`);
  }

  process.stdout.write(`publish-assets: ${String(plans.length)} asset(s) uploaded to ${args.bucket}\n`);
}

// Only run when executed directly as the CLI entrypoint (`node
// dist/publish-assets.js ...`) -- NOT as a side effect of another module
// importing this file's exported helpers (e.g. for unit tests).
const isDirectlyExecuted =
  process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;
if (isDirectlyExecuted) {
  main();
}
