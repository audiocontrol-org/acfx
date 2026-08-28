# justfile — the formal task interface for the acfx dev + release workflow.
#
# `just` (https://just.systems) is a command runner that orchestrates the common
# operations ON TOP OF CMake (which remains the C++ build system). It does NOT
# replace CMake or the effect target factory; it replaces the ad-hoc shell we
# used to run by hand.
#
#   Install:  brew install just
#   Discover: just --list
#
# The signed/notarized release recipes delegate to scripts/release-plugin.sh and
# scripts/release-plugins.sh -- those encode hard-won codesign/notarytool detail
# and stay standalone-runnable (notarytool needs the login-session keychain, so
# run release recipes from a real terminal, not a sandbox).

set shell := ["bash", "-euo", "pipefail", "-c"]

# --- machine config (override on the command line: `just arm_toolchain=... fw ...`) ---
archs        := "arm64;x86_64"
deployment   := "10.13"
arm_toolchain := "/Applications/ArmGNUToolchain/15.3.rel1/arm-none-eabi/bin"
sign_id      := "Developer ID Application: Orion Letizi (ES3R29MZ5A)"
cpm_cache    := justfile_directory() / "external/.cpm-cache"

# List all recipes.
default:
    @just --list

# --- firmware (NUCLEO-F446) ---

# Build a nucleo firmware image, e.g. `just fw spike_breathing_canyon`.
fw effect:
    CPM_SOURCE_CACHE="{{cpm_cache}}" PATH="{{arm_toolchain}}:$PATH" \
      cmake --build --preset nucleo --target acfx_nucleo_{{effect}} -j

# Build + flash firmware to the board, and report SRAM usage.
flash effect: (fw effect)
    #!/usr/bin/env bash
    set -euo pipefail
    export PATH="{{arm_toolchain}}:$PATH"
    elf="build/nucleo/adapters/nucleo/acfx_nucleo_{{effect}}.elf"
    bin="$(mktemp -d)/fw.bin"
    arm-none-eabi-objcopy -O binary "$elf" "$bin"
    st-flash --reset write "$bin" 0x08000000
    arm-none-eabi-size "$elf" | awk 'NR==2{printf "RAM: %d B (%.1f KB free)\n",$2+$3,(131072-$2-$3)/1024}'

# --- desktop plugins (VST3 / AU / Standalone) ---

# Configure the desktop build (universal, low min-OS) once.
configure-desktop:
    CPM_SOURCE_CACHE="{{cpm_cache}}" cmake --preset desktop \
      -DCMAKE_OSX_ARCHITECTURES="{{archs}}" -DCMAKE_OSX_DEPLOYMENT_TARGET="{{deployment}}" >/dev/null

# Build all three formats for a plugin target, e.g. `just plugin acfx_plugin_breathing_canyon`.
plugin target: configure-desktop
    CPM_SOURCE_CACHE="{{cpm_cache}}" \
      cmake --build --preset desktop --target {{target}}_VST3 {{target}}_AU {{target}}_Standalone -j

# Dev loop: build + Developer-ID sign + (re)install a plugin's AU + VST3 locally.
# e.g. `just reinstall acfx_plugin_breathing_canyon "acfx Breathing Canyon"`
reinstall target product: (plugin target)
    #!/usr/bin/env bash
    set -euo pipefail
    art="build/desktop/adapters/plugin/{{target}}_artefacts/RelWithDebInfo"
    for pair in "AU/{{product}}.component:$HOME/Library/Audio/Plug-Ins/Components" \
                "VST3/{{product}}.vst3:$HOME/Library/Audio/Plug-Ins/VST3"; do
      src="${pair%%:*}"; dst="${pair##*:}"
      codesign --force --options runtime --timestamp --sign "{{sign_id}}" "$art/$src" >/dev/null
      rm -rf "$dst/$(basename "$src")"; cp -R "$art/$src" "$dst/"
    done
    echo "reinstalled {{product}}"

# --- releases (signed + notarized; run from a real terminal for the keychain) ---

# One plugin -> one immutable release. Extra flags pass through (e.g. --clean).
# e.g. `just release acfx_plugin_breathing_canyon "acfx Breathing Canyon" canyon-2026-08-27 --clean`
release target product tag *flags:
    TARGET="{{target}}" PRODUCT="{{product}}" \
      bash scripts/release-plugin.sh --release {{tag}} {{flags}}

# Bundle the two experimental plugins (AU+VST3+Standalone each) into ONE release.
# e.g. `just release-experimental acfx-plugins-2026-08-27 notes.md`
release-experimental tag notes="":
    scripts/release-plugins.sh --release {{tag}} {{ if notes == "" { "" } else { "--notes " + notes } }} --clean \
      --plugin acfx_plugin_breathing_canyon "acfx Breathing Canyon" \
      --plugin acfx_plugin_reverse_reverb  "acfx Reverse Reverb"
