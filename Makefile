# acfx developer Makefile — a thin, well-known-target wrapper over the CMake
# presets defined in CMakePresets.json. The presets remain the source of truth
# (build dirs: build/<preset>); this file just spares you re-typing the
# configure/build/test invocations. See CLAUDE.md / .specify for project rules.
#
# Common use:
#   make            # help
#   make test       # host-side correctness suite (no hardware) — the everyday loop
#   make build      # desktop workbench + plugin (JUCE)
#   make install    # copy built plug-ins into your user plug-in folders (macOS)
#   make clean      # remove build/

CMAKE ?= cmake
CTEST ?= ctest
BUILD ?= build

# macOS user plug-in install locations (used by `make install`). Override on the
# command line if your setup differs, e.g. `make install VST3_DIR=/some/path`.
VST3_DIR ?= $(HOME)/Library/Audio/Plug-Ins/VST3
AU_DIR   ?= $(HOME)/Library/Audio/Plug-Ins/Components
CLAP_DIR ?= $(HOME)/Library/Audio/Plug-Ins/CLAP

.DEFAULT_GOAL := help
.PHONY: help test build desktop daisy teensy all clean distclean install

help:
	@echo "acfx make targets:"
	@echo "  make test       Configure + build the host test suite and run ctest (no hardware)"
	@echo "  make build      Configure + build the desktop workbench + plugin (JUCE; fetches deps on first run)"
	@echo "  make daisy      Configure + build the Daisy firmware (needs the arm-none-eabi toolchain)"
	@echo "  make teensy     Configure + build the Teensy firmware (needs the Teensy toolchain)"
	@echo "  make all        build + daisy + teensy"
	@echo "  make lesson-assets    Build native asset-tool + WASM, run both fragment producers, regenerate site/public/manifest/svf.json"
	@echo "  make staleness-guard  Non-building hash check: manifest sourceProvenance vs current core/+adapters/web source"
	@echo "  make install    Build the plugin, then copy VST3/AU/CLAP bundles to your user plug-in folders (macOS)"
	@echo "  make clean      Remove the build/ tree (keeps the CPM dependency cache under external/)"
	@echo "  make distclean  Remove build/ AND the CPM dependency cache"

# The everyday loop: configure, build, and run the platform-independent core tests.
test:
	$(CMAKE) --preset test
	$(CMAKE) --build --preset test
	$(CTEST) --preset test

# Desktop standalone workbench + DAW plugin. `build` and `desktop` are aliases.
build desktop:
	$(CMAKE) --preset desktop
	$(CMAKE) --build --preset desktop

daisy:
	$(CMAKE) --preset daisy
	$(CMAKE) --build --preset daisy

teensy:
	$(CMAKE) --preset teensy
	$(CMAKE) --build --preset teensy

all: build daisy teensy

# --- web adapter (Phase 1). All local; CI builds nothing. ---
.PHONY: web-ref web-wasm web-parity
web-ref:
	$(CMAKE) --preset web-ref && $(CMAKE) --build build/web-ref --target svf-reference acfx_web_abi_native_test
	$(CTEST) --test-dir build/web-ref -R acfx_web_abi_native_test --output-on-failure

web-wasm:
	rm -rf build/web && emcmake $(CMAKE) --preset web && $(CMAKE) --build build/web --target svf

web-parity: web-ref web-wasm
	cd adapters/web && npm install && npm run typecheck && npm test

# --- svf-training-site lesson assets + manifest (Phase 2). All local; this
# does NOT upload to any CDN -- that is Phase 3 (`make publish-assets`). ---
.PHONY: lesson-assets staleness-guard
lesson-assets: web-wasm
	$(CMAKE) --preset lesson-assets
	$(CMAKE) --build build/lesson-assets --target acfx_lesson_assets_tool
	./build/lesson-assets/tools/lesson-assets/acfx_lesson_assets_tool --out build/lesson-assets/svf-out
	cd tools && npm install && npm run wasm-fragment -- --wasm=../build/web/svf.wasm --out=../build/web
	cd tools && npm run assemble -- \
		--static=../build/lesson-assets/svf-out/static.fragment.json \
		--wasm=../build/web/wasm.fragment.json \
		--out=../site/public/manifest/svf.json

# Non-building staleness guard (FR-012): hash-compare committed manifest
# sourceProvenance vs current core/+adapters/web source. No compile.
staleness-guard:
	cd tools && npm install && npm run staleness-guard -- --manifest=../site/public/manifest/svf.json

clean:
	rm -rf $(BUILD)

distclean: clean
	rm -rf external/.cpm-cache

# The project defines no cmake install() rules and sets COPY_PLUGIN_AFTER_BUILD
# FALSE, so "install" means: copy the plug-in bundles produced by the desktop
# build into your user plug-in folders. Fails loud if the build produced none
# (no silent no-op) — run `make build` and check the plugin's FORMATS.
install: desktop
	@bundles="$$(find $(BUILD)/desktop -type d \( -name '*.vst3' -o -name '*.component' -o -name '*.clap' \) 2>/dev/null)"; \
	if [ -z "$$bundles" ]; then \
	  echo "install: no .vst3/.component/.clap bundles found under $(BUILD)/desktop"; \
	  echo "install: check adapters/plugin/CMakeLists.txt FORMATS, then run 'make build'."; \
	  exit 1; \
	fi; \
	mkdir -p "$(VST3_DIR)" "$(AU_DIR)" "$(CLAP_DIR)"; \
	for b in $$bundles; do \
	  case "$$b" in \
	    *.vst3)      dest="$(VST3_DIR)";; \
	    *.component) dest="$(AU_DIR)";; \
	    *.clap)      dest="$(CLAP_DIR)";; \
	    *)           continue;; \
	  esac; \
	  echo "install: $$b -> $$dest/"; \
	  rm -rf "$$dest/$$(basename "$$b")"; \
	  cp -R "$$b" "$$dest/"; \
	done
