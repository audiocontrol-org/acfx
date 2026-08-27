#!/usr/bin/env bash
# Repeatable build + sign + notarize + staple + package (+ GitHub release) for an
# acfx desktop plugin. Encodes the settings that each fixed a real distribution
# failure (see adapters/plugin/SIGNING-AND-NOTARIZATION.md):
#   - universal arch + a LOW deployment target (older Macs, the -1 open failure)
#   - Developer ID + hardened runtime + secure timestamp (notarization prereqs)
#   - notarize via notarytool, then STAPLE the ticket into the bundle
#   - ship bundles only (no dead-end installer / no README)
#
# RELEASES ARE IMMUTABLE (npm-style): a published release's bits never change.
# A new build is always a NEW release (new tag). This script refuses --release
# against an existing tag; delete the release explicitly if it truly must go.
#
# IMPORTANT: run this from a REAL terminal, not a sandboxed/headless context —
# notarytool credentials live in the login-session (data-protection) keychain and
# are unreachable from a sandbox.
#
# Usage:
#   scripts/release-plugin.sh [options]
# Options:
#   --clean               wipe build/desktop first (REQUIRED after changing the
#                         deployment target, since it's a compile-time flag)
#   --no-notarize         build + sign + package only (fast dev build)
#   --release <tag>       create/update a GitHub pre-release at <tag> and upload
#   --notes <file>        release notes markdown (with --release)
#   -h | --help
# Config (override via env): TARGET PRODUCT SIGN_IDENTITY TEAM_ID NOTARY_PROFILE
#   DEPLOYMENT_TARGET ARCHS
set -euo pipefail

TARGET="${TARGET:-acfx_plugin_reverse_reverb}"
PRODUCT="${PRODUCT:-acfx Reverse Reverb}"
SIGN_IDENTITY="${SIGN_IDENTITY:-Developer ID Application: Orion Letizi (ES3R29MZ5A)}"
TEAM_ID="${TEAM_ID:-ES3R29MZ5A}"
NOTARY_PROFILE="${NOTARY_PROFILE:-acfx}"
DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET:-10.13}"
ARCHS="${ARCHS:-arm64;x86_64}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build/desktop"
DIST_DIR="$REPO_ROOT/dist/$TARGET"
STAGE="$DIST_DIR/$PRODUCT"

DO_NOTARIZE=1; DO_RELEASE=0; RELEASE_TAG=""; NOTES_FILE=""; CLEAN=0
while [ $# -gt 0 ]; do
  case "$1" in
    --clean) CLEAN=1 ;;
    --no-notarize) DO_NOTARIZE=0 ;;
    --release) DO_RELEASE=1; RELEASE_TAG="${2:?--release needs a tag}"; shift ;;
    --notes) NOTES_FILE="${2:?--notes needs a file}"; shift ;;
    -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

step() { printf '\n\033[1m>> %s\033[0m\n' "$*"; }

step "Configure + build ($TARGET, archs=$ARCHS, min macOS=$DEPLOYMENT_TARGET)"
export CPM_SOURCE_CACHE="$REPO_ROOT/external/.cpm-cache"
[ "$CLEAN" = 1 ] && rm -rf "$BUILD_DIR"
# Refresh the editor's build stamp so it reflects this build.
touch "$REPO_ROOT/adapters/plugin/plugin-editor.cpp"
cmake --preset desktop -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET" >/dev/null
cmake --build --preset desktop --target "${TARGET}_VST3" "${TARGET}_AU" -j

ART="$BUILD_DIR/adapters/plugin/${TARGET}_artefacts/RelWithDebInfo"
AU="$STAGE/AU/$PRODUCT.component"
VST3="$STAGE/VST3/$PRODUCT.vst3"

step "Stage + sign (Developer ID, hardened runtime, timestamp)"
rm -rf "$DIST_DIR"; mkdir -p "$STAGE/AU" "$STAGE/VST3"
cp -R "$ART/AU/$PRODUCT.component" "$STAGE/AU/"
cp -R "$ART/VST3/$PRODUCT.vst3" "$STAGE/VST3/"
for b in "$AU" "$VST3"; do
  codesign --force --options runtime --timestamp --sign "$SIGN_IDENTITY" "$b"
  codesign --verify --strict "$b"
  echo "   signed: ${b#$STAGE/}"
done
# Verify the universal slices + min-OS actually took.
echo "   arm64  min: $(vtool -arch arm64 -show-build "$AU/Contents/MacOS/$PRODUCT" 2>/dev/null | awk '/minos/{print $2}')"
echo "   x86_64 min: $(otool -arch x86_64 -l "$AU/Contents/MacOS/$PRODUCT" 2>/dev/null | awk '/LC_VERSION_MIN_MACOSX/{f=1} f&&/version/{print $2; exit}')"

if [ "$DO_NOTARIZE" = 1 ]; then
  step "Notarize + staple (Apple; a few minutes)"
  for b in "$AU" "$VST3"; do
    z="$(mktemp -d)/n.zip"; ditto -c -k --keepParent "$b" "$z"
    xcrun notarytool submit "$z" --keychain-profile "$NOTARY_PROFILE" --wait --timeout 30m \
      | grep -E "id:|status:" || true
    xcrun stapler staple "$b"
    xcrun stapler validate "$b" >/dev/null && echo "   stapled: ${b#$STAGE/}"
  done
else
  step "Skipping notarization (--no-notarize)"
fi

step "Package (bundles only)"
ZIP="$DIST_DIR/${TARGET}-macOS-AU-VST3.zip"
( cd "$DIST_DIR" && ditto -c -k --sequesterRsrc --keepParent "$PRODUCT" "$(basename "$ZIP")" )
echo "   $ZIP"

if [ "$DO_RELEASE" = 1 ]; then
  step "Publish GitHub release ($RELEASE_TAG)"
  SHA="$(git -C "$REPO_ROOT" rev-parse HEAD)"
  REPO="$(gh repo view --json nameWithOwner --jq .nameWithOwner)"
  # RELEASES ARE IMMUTABLE. Never change the bits of a published release. If the
  # tag already exists, stop -- pick a new tag (a new build is a new release), or
  # explicitly `gh release delete <tag> --cleanup-tag` first if it must go.
  if gh release view "$RELEASE_TAG" >/dev/null 2>&1; then
    echo "ERROR: release '$RELEASE_TAG' already exists; releases are immutable." >&2
    echo "       Use a new tag for this build, or delete the old release first:" >&2
    echo "         gh release delete '$RELEASE_TAG' --cleanup-tag" >&2
    exit 1
  fi
  gh release create "$RELEASE_TAG" --target "$SHA" --prerelease \
    --title "$PRODUCT ($RELEASE_TAG)" ${NOTES_FILE:+--notes-file "$NOTES_FILE"} \
    "$ZIP#$PRODUCT (macOS AU + VST3, signed & notarized)"
  echo "   https://github.com/$REPO/releases/tag/$RELEASE_TAG"
fi

step "Done."
