#!/usr/bin/env bash
# Bundle SEVERAL acfx desktop plugins into ONE immutable GitHub release.
#
# Each plugin is built + signed + notarized + stapled + packaged by the existing
# per-plugin release-plugin.sh (its stage-only path -- i.e. invoked WITHOUT
# --release, so it produces dist/<target>/<target>-macOS.zip but publishes
# nothing). This script then attaches every zip to a single release.
#
# RELEASES ARE IMMUTABLE (npm-style): it refuses a tag that already exists --
# up front, before the long builds. A new build is always a new tag.
#
# Like release-plugin.sh, run this from a REAL terminal, not a sandboxed/headless
# context -- notarytool credentials live in the login-session (data-protection)
# keychain and are unreachable from a sandbox.
#
# Usage:
#   scripts/release-plugins.sh --release <tag> [--notes <file>] [--clean] \
#     --plugin <target> "<product>" [--plugin <target> "<product>" ...]
#
# Example (Breathing Canyon + Reverse Reverb):
#   scripts/release-plugins.sh --release acfx-plugins-2026-08-27 \
#     --notes notes.md --clean \
#     --plugin acfx_plugin_breathing_canyon "acfx Breathing Canyon" \
#     --plugin acfx_plugin_reverse_reverb  "acfx Reverse Reverb"
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

TAG=""; NOTES=""; CLEAN=""
declare -a TARGETS=(); declare -a PRODUCTS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --release) TAG="${2:?--release needs a tag}"; shift ;;
    --notes)   NOTES="${2:?--notes needs a file}"; shift ;;
    --clean)   CLEAN="--clean" ;;
    --plugin)  TARGETS+=("${2:?--plugin needs a target}"); PRODUCTS+=("${3:?--plugin needs a product}"); shift 2 ;;
    -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done
[ -n "$TAG" ] || { echo "ERROR: --release <tag> is required" >&2; exit 2; }
[ "${#TARGETS[@]}" -gt 0 ] || { echo "ERROR: at least one --plugin is required" >&2; exit 2; }

step() { printf '\n\033[1m>> %s\033[0m\n' "$*"; }

# Immutability: fail before the (long) builds if the tag is taken.
if gh release view "$TAG" >/dev/null 2>&1; then
  echo "ERROR: release '$TAG' already exists; releases are immutable." >&2
  echo "       Use a new tag, or: gh release delete '$TAG' --cleanup-tag" >&2
  exit 1
fi

declare -a ASSETS=()
for i in "${!TARGETS[@]}"; do
  t="${TARGETS[$i]}"; p="${PRODUCTS[$i]}"
  step "Build + sign + notarize: $p ($t)"
  # --clean only on the first plugin: the deployment target is a compile-time
  # cache flag set once; the rest build incrementally against the same config.
  cl=""; [ "$i" = 0 ] && cl="$CLEAN"
  TARGET="$t" PRODUCT="$p" bash "$REPO_ROOT/scripts/release-plugin.sh" $cl
  ASSETS+=("$REPO_ROOT/dist/$t/$t-macOS.zip#$p (macOS AU + VST3 + Standalone, signed & notarized)")
done

step "Publish one GitHub release ($TAG) with ${#TARGETS[@]} plugin(s)"
SHA="$(git -C "$REPO_ROOT" rev-parse HEAD)"
gh release create "$TAG" --target "$SHA" --prerelease \
  --title "acfx experimental plugins ($TAG)" ${NOTES:+--notes-file "$NOTES"} \
  "${ASSETS[@]}"
REPO="$(gh repo view --json nameWithOwner --jq .nameWithOwner)"
echo "   https://github.com/$REPO/releases/tag/$TAG"

step "Done."
