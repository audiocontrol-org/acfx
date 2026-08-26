# Signing & notarizing acfx plugins for macOS distribution

How to turn a locally-built AU/VST3 into a bundle other people can download and
load with **no Gatekeeper prompts and no OS-version errors**. Written from a real
distribution of `acfx Reverse Reverb`; every step here fixed a concrete failure.

The order matters: **build → sign → notarize → staple → package**. You cannot
notarize an unsigned build, and you cannot staple before notarization succeeds.

## Prerequisites (one time)

- A **Developer ID Application** certificate in your login keychain
  (`security find-identity -v -p codesigning` should list it). This is the
  project's signing identity: `Developer ID Application: Orion Letizi
  (ES3R29MZ5A)`, team `ES3R29MZ5A`.
- A **notarytool credential profile**. Create it in a real Terminal
  (see the keychain gotcha below) with an app-specific password from
  appleid.apple.com:
  ```
  xcrun notarytool store-credentials acfx \
    --apple-id "you@example.com" --team-id ES3R29MZ5A --password "xxxx-xxxx-xxxx-xxxx"
  ```
  Profile name used throughout here: `acfx`.
- Your Apple Developer **agreements must be current** — see gotcha below.

## 1. Build (universal + a LOW deployment target)

Two settings are non-negotiable for a build other people can run:

- `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` — universal (Apple Silicon + Intel).
- `CMAKE_OSX_DEPLOYMENT_TARGET="10.13"` — the **minimum macOS**. Without this it
  defaults to the *build machine's* OS. A build made on macOS 15 gets stamped
  "minOS 15.0", and every older Mac refuses to load it with
  `OpenAComponent: result: -1` (the metadata still validates, so the AU appears
  to "PASS" then fails to open). 10.13 covers Intel Macs back to 2017; the arm64
  slice is automatically clamped to 11.0 (the Apple-Silicon floor).

Changing the deployment target is a compile flag, so it needs a **clean build**
(`rm -rf build/desktop` first):
```
cmake --preset desktop \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="10.13"
cmake --build --preset desktop --target acfx_plugin_reverse_reverb_VST3 acfx_plugin_reverse_reverb_AU -j
```

Verify the floor took (x86_64 uses the legacy `LC_VERSION_MIN_MACOSX` load
command below 10.14, which `vtool` won't show — use `otool`):
```
vtool -arch arm64 -show-build "<bundle>/Contents/MacOS/<name>" | grep minos     # 11.0
otool -arch x86_64 -l "<bundle>/Contents/MacOS/<name>" | grep -A2 LC_VERSION_MIN # version 10.13
```

## 2. Sign (Developer ID + hardened runtime + timestamp)

Hardened runtime (`--options runtime`) and a secure `--timestamp` are **required
for notarization**, not optional:
```
CERT="Developer ID Application: Orion Letizi (ES3R29MZ5A)"
codesign --force --options runtime --timestamp --sign "$CERT" "<bundle>"
codesign --verify --strict --verbose=2 "<bundle>"   # "satisfies its Designated Requirement"
```
JUCE's build step ad-hoc-signs the bundle; this replaces that with the real
Developer ID signature. Sign each format bundle.

## 3. Notarize + 4. Staple

Notarization submits a **zip** of the bundle; stapling then writes the ticket
into the **bundle on disk** (not the zip):
```
ditto -c -k --keepParent "<bundle>" submit.zip
xcrun notarytool submit submit.zip --keychain-profile acfx --wait --timeout 20m   # -> status: Accepted
xcrun stapler staple "<bundle>"
xcrun stapler validate "<bundle>"                                                 # "The staple and validate action worked!"
```
Confirm Gatekeeper is satisfied:
```
spctl -a -vv -t install "<bundle>"   # accepted / source=Notarized Developer ID
```

## 5. Package + distribute

- **Ship the bundles only** — the `.component` and `.vst3`. Do **not** include a
  `install.command` script (unsigned scripts are quarantined and Gatekeeper
  refuses to run them on double-click — a dead end) or a README with install
  steps (put those on the release page instead; bad instructions are worse than
  none).
- Zip with `ditto -c -k --sequesterRsrc --keepParent`.
- End-user install (put this on the GitHub Release page, not in the zip): in
  Finder, **Go → Go to Folder…**, paste `~/Library/Audio/Plug-Ins/Components`,
  drag in the `.component`; repeat for `~/Library/Audio/Plug-Ins/VST3` with the
  `.vst3`; restart the DAW. Because the build is notarized there is **nothing to
  clear** — no quarantine step, no right-click-Open.

## Gotchas that cost real time

- **minOS too high → `-1` on open** (the big one). Always set
  `CMAKE_OSX_DEPLOYMENT_TARGET`; see step 1.
- **Apple agreement lag.** A pending/updated Apple Developer Program License
  Agreement blocks notarization with `HTTP status code: 403. A required
  agreement is missing or has expired.` Accept it at developer.apple.com/account
  (Account Holder role) and in App Store Connect → Business. After accepting, it
  takes a few minutes to propagate through Apple's edge cache — the submission
  keeps failing until it does, then succeeds on retry with no other changes.
- **notarytool credentials live in the data-protection keychain**, which the
  `security` CLI cannot read and which is gated to your **GUI login session**.
  Store credentials and run notarytool from a normal Terminal, not from a
  sandboxed/headless context — otherwise you get `No Keychain password item
  found for profile`.
- **Ad-hoc signature is not enough** for distribution; use the Developer ID and
  notarize. A locally-usable ad-hoc build will still trip Gatekeeper on other
  machines.
