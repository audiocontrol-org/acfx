# SVF Web Adapter + Parity — Implementation Plan (Phase 1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Compile the real `acfx::SvfEffect` audio path to WebAssembly as a new `adapters/web` target, and prove — via a parity test against a native reference built from the *same* source — that the browser path produces identical audio output.

**Architecture:** `adapters/web` is a thin extern-"C" ABI over `acfx::SvfEffect` (Constitution VI — depends only inward on `acfx_core`; core untouched). The one ABI source compiles two ways: to `svf.wasm` via Emscripten (the `web` preset) and to a native `svf-reference` CLI via the host compiler (the `web-ref` preset). A TypeScript parity test runs shared, versioned input vectors through both and compares output buffers within tolerance.

**Tech Stack:** C++20, Emscripten (emcc), CMake presets, Node ≥ 22 / TypeScript (strict) / vitest.

**Design reference:** `docs/superpowers/specs/2026-07-14-svf-training-site-design.md` §4.1, §4.3, §6.

## Scope of this phase

- **In:** the audio C ABI (`create/prepare/setParam/process`), the Emscripten WASM build, the TypeScript module loader, the native reference CLI, versioned input vectors, and the parity test.
- **Re-sequenced to Phase 5 (visualizer):** the **analysis ABI** (`getFrequencyResponse / getPoleZeroData / renderImpulseResponse`). Its exact contract is defined by the visualizer that consumes it; surfaced per Commandment V, not cut. (Operator may pull it forward.)
- **Not in this phase:** the AudioWorklet processor, the site, the asset tool, the CDN. (Later phases.)

## Global Constraints

Copied verbatim from the spec/constitution; every task implicitly includes these.

- **TypeScript strict for all JS-runtime code** — no plain `.js`, no `any`, no `@ts-ignore`, no unchecked casts. Type errors are build failures (Principle IX).
- **CI builds nothing** — every build/test here is a local `make` target / local command (§4.3).
- **Real-time safety** — no heap allocation or locks in `process()` (Principle VIII). The ABI allocates only in `create`/`prepare`.
- **No fallbacks / mock data outside tests** — a load/instantiate failure raises a descriptive error; never a substitute DSP (Principle VII).
- **Platform-independent core untouched** — nothing under `core/` changes; dependencies point inward (Principle VI).
- **Descriptive names, no numeric prefixes** (Commandment III). **Commit and push early and often** (Commandment I); no AI attribution.
- **No secrets in the repo** (relevant from Phase 3 on; nothing secret here).

## Prerequisites (one-time, local hardware)

Emscripten is not assumed present (`emcc` is absent on at least one dev machine). Install the SDK locally and activate it in the shell used for the `web` preset:

```bash
git clone https://github.com/emscripten-core/emsdk ~/emsdk
~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest
source ~/emsdk/emsdk_env.sh   # exports EMSDK and puts emcc on PATH
emcc --version                # verify
node --version                # verify Node >= 22
```

`source emsdk_env.sh` must be run in any shell that configures/builds the `web` preset (the preset reads `$env{EMSDK}`).

## File Structure

- `adapters/web/svf-web-abi.h` — extern-"C" ABI declarations (the browser contract).
- `adapters/web/svf-web-abi.cpp` — ABI implementation wrapping `acfx::SvfEffect`.
- `adapters/web/svf-reference-main.cpp` — native CLI: reads a vector JSON, runs the ABI, writes output JSON.
- `adapters/web/test/svf-web-abi-native-test.cpp` — native doctest: ABI output == direct `SvfEffect` output.
- `adapters/web/CMakeLists.txt` — branches on `EMSCRIPTEN`: WASM module vs native reference + native test.
- `adapters/web/loader/svf-module.ts` — TS loader wrapping the Emscripten module (Node + browser-agnostic).
- `adapters/web/test/svf-parity.test.ts` — vitest parity test (WASM vs native reference).
- `adapters/web/test/vectors/*.json` — versioned input-vector fixtures.
- `adapters/web/package.json`, `adapters/web/tsconfig.json`, `adapters/web/vitest.config.ts`.
- `CMakeLists.txt` — add `ACFX_BUILD_WEB` option + gated `add_subdirectory(adapters/web)`.
- `CMakePresets.json` — add `web` (Emscripten) and `web-ref` (host) presets.
- `.gitignore` — `adapters/web/node_modules/`, `adapters/web/dist/`, `build/`.
- `Makefile` — `web-ref`, `web-wasm`, `web-parity` targets.

---

### Task 1: Build-surface plumbing (option + presets + gitignore)

**Files:**
- Modify: `CMakeLists.txt` (add option + gated subdir)
- Modify: `CMakePresets.json` (add `web` + `web-ref` presets)
- Modify: `.gitignore`
- Create: `adapters/web/CMakeLists.txt` (minimal stub this task)

**Interfaces:**
- Produces: the `ACFX_BUILD_WEB` CMake option; presets `web` (Emscripten toolchain) and `web-ref` (host). Later tasks add real targets inside `adapters/web/CMakeLists.txt`.

- [ ] **Step 1: Add the option + gated subdirectory to `CMakeLists.txt`**

After the existing `option(ACFX_BUILD_TEENSY ...)` line add:

```cmake
option(ACFX_BUILD_WEB     "Build the WebAssembly (Emscripten) adapter"       OFF)
```

After the existing `if(ACFX_BUILD_TEENSY) ... endif()` block add:

```cmake
if(ACFX_BUILD_WEB)
  add_subdirectory(adapters/web)
endif()
```

- [ ] **Step 2: Create the minimal `adapters/web/CMakeLists.txt` stub**

```cmake
# adapters/web — the browser adapter. One extern-"C" ABI over acfx::SvfEffect
# compiles two ways: to svf.wasm via Emscripten (EMSCRIPTEN set by the `web`
# preset toolchain), and to a native reference CLI + test via the host compiler
# (the `web-ref` preset). Depends only inward on acfx_core (Constitution VI).
message(STATUS "adapters/web: EMSCRIPTEN=${EMSCRIPTEN}")
```

- [ ] **Step 3: Add the two presets to `CMakePresets.json`**

Add to `configurePresets` (after `teensy`):

```json
{
  "name": "web",
  "inherits": "base",
  "displayName": "WebAssembly adapter (Emscripten)",
  "toolchainFile": "$env{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake",
  "cacheVariables": { "ACFX_BUILD_WEB": "ON", "CMAKE_CXX_STANDARD": "20" }
},
{
  "name": "web-ref",
  "inherits": "base",
  "displayName": "Native reference + native ABI test (host)",
  "cacheVariables": { "ACFX_BUILD_WEB": "ON", "ACFX_BUILD_TESTS": "ON", "CMAKE_CXX_STANDARD": "20" }
}
```

Add to `buildPresets`:

```json
{ "name": "web", "configurePreset": "web" },
{ "name": "web-ref", "configurePreset": "web-ref" }
```

- [ ] **Step 4: Add ignore rules to `.gitignore`**

```gitignore
# web adapter (Phase 1)
adapters/web/node_modules/
adapters/web/dist/
build/
```

- [ ] **Step 5: Verify the host preset configures**

Run: `cmake --preset web-ref`
Expected: configures successfully; output includes `adapters/web: EMSCRIPTEN=` (empty on host).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt CMakePresets.json .gitignore adapters/web/CMakeLists.txt
git commit -m "build(web): add ACFX_BUILD_WEB option + web/web-ref presets"
```

---

### Task 2: The audio C ABI over SvfEffect (native TDD)

**Files:**
- Create: `adapters/web/svf-web-abi.h`, `adapters/web/svf-web-abi.cpp`
- Create: `adapters/web/test/svf-web-abi-native-test.cpp`
- Modify: `adapters/web/CMakeLists.txt`

**Interfaces:**
- Produces (the ABI every later task and the WASM/native builds use):
  - `SvfHandle* svf_create(void)`
  - `void svf_destroy(SvfHandle*)`
  - `void svf_prepare(SvfHandle*, double sampleRate, int maxBlockSize, int numChannels)`
  - `void svf_set_param(SvfHandle*, unsigned char paramId, float normalized)` — `paramId`: 0=cutoff, 1=resonance, 2=mode; `normalized` in 0..1.
  - `void svf_process(SvfHandle*, float* samples, int numSamples)` — mono, in place.

- [ ] **Step 1: Write the failing native test**

`adapters/web/test/svf-web-abi-native-test.cpp`:

```cpp
#include "doctest/doctest.h"
#include "svf-web-abi.h"
#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/process-context.h"
#include "effects/svf/svf-effect.h"
#include <vector>

// The ABI must produce bit-identical output to calling acfx::SvfEffect directly
// with the same prepare/param/process sequence (the ABI adds no DSP of its own).
TEST_CASE("svf ABI matches direct SvfEffect output") {
    constexpr double sr = 48000.0;
    constexpr int n = 256;
    std::vector<float> in(n);
    for (int i = 0; i < n; ++i) in[i] = 0.5f * static_cast<float>((i % 32) - 16) / 16.0f;

    // Direct reference.
    std::vector<float> direct = in;
    acfx::SvfEffect fx;
    fx.prepare(acfx::ProcessContext{sr, n, 1});
    fx.setParameter(acfx::ParamId{0}, 0.5f);   // cutoff mid
    fx.setParameter(acfx::ParamId{1}, 0.3f);   // resonance
    fx.setParameter(acfx::ParamId{2}, 0.0f);   // lowpass
    {
        float* chans[1] = {direct.data()};
        acfx::AudioBlock io(chans, 1, n);
        fx.process(io);
    }

    // Through the ABI.
    std::vector<float> viaAbi = in;
    SvfHandle* h = svf_create();
    svf_prepare(h, sr, n, 1);
    svf_set_param(h, 0, 0.5f);
    svf_set_param(h, 1, 0.3f);
    svf_set_param(h, 2, 0.0f);
    svf_process(h, viaAbi.data(), n);
    svf_destroy(h);

    for (int i = 0; i < n; ++i) CHECK(viaAbi[i] == doctest::Approx(direct[i]).epsilon(0.0f));
}
```

- [ ] **Step 2: Write the ABI header** — `adapters/web/svf-web-abi.h`:

```cpp
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

typedef struct SvfHandle SvfHandle;

SvfHandle* svf_create(void);
void       svf_destroy(SvfHandle* h);
void       svf_prepare(SvfHandle* h, double sampleRate, int maxBlockSize, int numChannels);
void       svf_set_param(SvfHandle* h, unsigned char paramId, float normalized);
void       svf_process(SvfHandle* h, float* samples, int numSamples);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: Write the ABI implementation** — `adapters/web/svf-web-abi.cpp`:

```cpp
#include "svf-web-abi.h"

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/process-context.h"
#include "effects/svf/svf-effect.h"

// Opaque handle wraps one SvfEffect. Allocation happens ONLY in svf_create;
// svf_process is allocation-free (Principle VIII).
struct SvfHandle {
    acfx::SvfEffect effect;
};

extern "C" {

SvfHandle* svf_create(void) { return new SvfHandle(); }
void svf_destroy(SvfHandle* h) { delete h; }

void svf_prepare(SvfHandle* h, double sampleRate, int maxBlockSize, int numChannels) {
    h->effect.prepare(acfx::ProcessContext{sampleRate, maxBlockSize, numChannels});
}

void svf_set_param(SvfHandle* h, unsigned char paramId, float normalized) {
    h->effect.setParameter(acfx::ParamId{paramId}, normalized);
}

void svf_process(SvfHandle* h, float* samples, int numSamples) {
    float* chans[1] = {samples};
    acfx::AudioBlock io(chans, 1, numSamples);
    h->effect.process(io);
}

} // extern "C"
```

- [ ] **Step 4: Add the native reference/test wiring to `adapters/web/CMakeLists.txt`**

Append (below the `message(STATUS ...)` line):

```cmake
if(NOT EMSCRIPTEN)
  # Host build: the native ABI library + its doctest. (web-ref preset.)
  add_library(acfx_web_abi STATIC svf-web-abi.cpp)
  target_include_directories(acfx_web_abi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(acfx_web_abi PUBLIC acfx_core)
  target_compile_features(acfx_web_abi PUBLIC cxx_std_20)

  if(ACFX_BUILD_TESTS AND TARGET doctest)
    enable_testing()
    add_executable(acfx_web_abi_native_test test/svf-web-abi-native-test.cpp)
    target_link_libraries(acfx_web_abi_native_test PRIVATE acfx_web_abi acfx_core doctest)
    target_compile_definitions(acfx_web_abi_native_test PRIVATE DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
    add_test(NAME acfx_web_abi_native_test COMMAND acfx_web_abi_native_test)
  endif()
endif()
```

- [ ] **Step 5: Configure, build, and run the native test — verify it passes**

```bash
cmake --preset web-ref
cmake --build build/web-ref --target acfx_web_abi_native_test
ctest --test-dir build/web-ref -R acfx_web_abi_native_test --output-on-failure
```

Expected: `acfx_web_abi_native_test` PASSES (ABI output equals direct `SvfEffect`).

- [ ] **Step 6: Commit**

```bash
git add adapters/web/svf-web-abi.h adapters/web/svf-web-abi.cpp \
        adapters/web/test/svf-web-abi-native-test.cpp adapters/web/CMakeLists.txt
git commit -m "feat(web): extern-C SVF audio ABI over SvfEffect (native parity test)"
```

---

### Task 3: Emscripten WASM build

**Files:**
- Modify: `adapters/web/CMakeLists.txt` (Emscripten branch)

**Interfaces:**
- Produces: `build/web/svf.mjs` + `build/web/svf.wasm` — an ES6 module exporting the ABI (`_svf_create` … `_svf_process`) plus `_malloc`/`_free` and the `HEAPF32` view, with `ccall`/`cwrap` runtime methods.

- [ ] **Step 1: Add the Emscripten branch to `adapters/web/CMakeLists.txt`**

Append:

```cmake
if(EMSCRIPTEN)
  # WASM build: one module from the ABI source. (web preset.)
  add_executable(svf svf-web-abi.cpp)
  target_link_libraries(svf PRIVATE acfx_core)
  target_compile_features(svf PRIVATE cxx_std_20)
  set_target_properties(svf PROPERTIES OUTPUT_NAME "svf" SUFFIX ".mjs")
  target_link_options(svf PRIVATE
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sENVIRONMENT=web,worker,node"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXPORTED_FUNCTIONS=['_svf_create','_svf_destroy','_svf_prepare','_svf_set_param','_svf_process','_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPF32']"
    "-sMODULARIZE_EXPORT_NAME=createSvfModule"
  )
endif()
```

- [ ] **Step 2: Configure + build the WASM (requires `source emsdk_env.sh`)**

```bash
cmake --preset web
cmake --build build/web --target svf
ls -la build/web/svf.wasm build/web/svf.mjs
```

Expected: both `svf.wasm` and `svf.mjs` exist.

- [ ] **Step 3: Node smoke — load the module and call the ABI**

Run (from `build/web`):

```bash
node --input-type=module -e "
import createSvfModule from './svf.mjs';
const m = await createSvfModule();
const h = m._svf_create();
m._svf_prepare(h, 48000, 256, 1);
m._svf_set_param(h, 0, 0.5);
const n = 8, bytes = n*4, p = m._malloc(bytes);
const buf = new Float32Array(n).fill(1.0);
m.HEAPF32.set(buf, p>>2);
m._svf_process(h, p, n);
const out = Array.from(m.HEAPF32.subarray(p>>2, (p>>2)+n));
m._free(p); m._svf_destroy(h);
console.log('out[0..2]=', out.slice(0,3));
if (out.every(x => x === 1.0)) throw new Error('filter had no effect');
console.log('WASM smoke OK');
"
```

Expected: prints `WASM smoke OK` (the lowpass alters the constant-1 input).

- [ ] **Step 4: Commit**

```bash
git add adapters/web/CMakeLists.txt
git commit -m "build(web): emscripten WASM module (svf.mjs + svf.wasm)"
```

---

### Task 4: TypeScript loader + toolchain (strict)

**Files:**
- Create: `adapters/web/package.json`, `adapters/web/tsconfig.json`, `adapters/web/vitest.config.ts`
- Create: `adapters/web/loader/svf-module.ts`

**Interfaces:**
- Produces: `class SvfModule` with `static load(mjsUrl: string, wasmUrl?: string): Promise<SvfModule>`, and instance methods `create(): number`, `prepare(h, sr, maxBlock, ch): void`, `setParam(h, id, norm): void`, `process(h, samples: Float32Array): void` (in place), `destroy(h): void`. Used by the parity test (Task 6) and, later, the AudioWorklet (Phase 5).

- [ ] **Step 1: Create `adapters/web/package.json`**

```json
{
  "name": "@acfx/web",
  "private": true,
  "type": "module",
  "scripts": {
    "typecheck": "tsc --noEmit",
    "test": "vitest run"
  },
  "devDependencies": {
    "typescript": "^5.6.0",
    "vitest": "^2.1.0"
  }
}
```

- [ ] **Step 2: Create `adapters/web/tsconfig.json` (strict)**

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "ESNext",
    "moduleResolution": "Bundler",
    "strict": true,
    "noUncheckedIndexedAccess": true,
    "noImplicitOverride": true,
    "verbatimModuleSyntax": true,
    "skipLibCheck": true,
    "types": ["node"]
  },
  "include": ["loader", "test"]
}
```

- [ ] **Step 3: Create `adapters/web/vitest.config.ts`**

```ts
import { defineConfig } from "vitest/config";

export default defineConfig({
  test: { include: ["test/**/*.test.ts"], environment: "node" },
});
```

- [ ] **Step 4: Write the loader** — `adapters/web/loader/svf-module.ts`:

```ts
// Typed wrapper over the Emscripten SVF module. No `any`: the Emscripten surface
// we use is declared explicitly. A failed load throws (Principle VII — no
// silent fallback DSP).
interface EmscriptenSvf {
  _svf_create(): number;
  _svf_destroy(h: number): void;
  _svf_prepare(h: number, sampleRate: number, maxBlockSize: number, numChannels: number): void;
  _svf_set_param(h: number, paramId: number, normalized: number): void;
  _svf_process(h: number, samplesPtr: number, numSamples: number): void;
  _malloc(bytes: number): number;
  _free(ptr: number): void;
  HEAPF32: Float32Array;
}

type ModuleFactory = (opts?: Record<string, unknown>) => Promise<EmscriptenSvf>;

export class SvfModule {
  private constructor(private readonly m: EmscriptenSvf) {}

  static async load(mjsUrl: string): Promise<SvfModule> {
    const mod = (await import(/* @vite-ignore */ mjsUrl)) as { default: ModuleFactory };
    const instance = await mod.default();
    return new SvfModule(instance);
  }

  create(): number { return this.m._svf_create(); }
  destroy(h: number): void { this.m._svf_destroy(h); }
  prepare(h: number, sampleRate: number, maxBlockSize: number, numChannels: number): void {
    this.m._svf_prepare(h, sampleRate, maxBlockSize, numChannels);
  }
  setParam(h: number, paramId: number, normalized: number): void {
    this.m._svf_set_param(h, paramId, normalized);
  }

  // In-place mono process: copies into the WASM heap, runs, copies back.
  process(h: number, samples: Float32Array): void {
    const n = samples.length;
    const ptr = this.m._malloc(n * 4);
    try {
      this.m.HEAPF32.set(samples, ptr >> 2);
      this.m._svf_process(h, ptr, n);
      samples.set(this.m.HEAPF32.subarray(ptr >> 2, (ptr >> 2) + n));
    } finally {
      this.m._free(ptr);
    }
  }
}
```

- [ ] **Step 5: Install deps and typecheck**

```bash
cd adapters/web && npm install && npm run typecheck
```

Expected: `tsc --noEmit` exits 0 (no type errors).

- [ ] **Step 6: Commit**

```bash
git add adapters/web/package.json adapters/web/tsconfig.json \
        adapters/web/vitest.config.ts adapters/web/loader/svf-module.ts adapters/web/package-lock.json
git commit -m "feat(web): strict-TS Emscripten module loader + toolchain"
```

---

### Task 5: Input-vector fixtures + native reference CLI

**Files:**
- Create: `adapters/web/test/vectors/lowpass-sweep.json`
- Create: `adapters/web/svf-reference-main.cpp`
- Modify: `adapters/web/CMakeLists.txt` (native reference exe)

**Interfaces:**
- Vector JSON schema (versioned): `{ "version": 1, "sampleRate": number, "params": [{"id": number, "norm": number}], "input": number[] }`.
- Produces: `svf-reference <vector.json>` → prints `{ "version": 1, "output": number[] }` to stdout. Consumed by the parity test (Task 6).

- [ ] **Step 1: Create the fixture** — `adapters/web/test/vectors/lowpass-sweep.json`:

```json
{
  "version": 1,
  "sampleRate": 48000,
  "params": [ { "id": 0, "norm": 0.5 }, { "id": 1, "norm": 0.3 }, { "id": 2, "norm": 0.0 } ],
  "input": [1.0, 0.0, -1.0, 0.5, 0.25, -0.5, 0.75, -0.25, 0.1, -0.1, 0.6, -0.6, 0.33, -0.33, 0.9, -0.9]
}
```

- [ ] **Step 2: Write the native reference CLI** — `adapters/web/svf-reference-main.cpp`:

```cpp
// Reads a vector JSON (schema in the plan), runs it through the SVF ABI, and
// prints {"version":1,"output":[...]} to stdout. Minimal hand-rolled JSON I/O so
// the reference has no external deps. Test-support code (not shipped).
#include "svf-web-abi.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static std::string slurp(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(2); }
    std::string s; char buf[4096]; size_t r;
    while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, r);
    std::fclose(f);
    return s;
}

// Extract every number appearing in the JSON value for `key` (a flat array).
static std::vector<double> numbersAfter(const std::string& j, const std::string& key) {
    std::vector<double> out;
    size_t k = j.find("\"" + key + "\"");
    if (k == std::string::npos) return out;
    size_t lb = j.find('[', k), rb = j.find(']', lb);
    size_t i = lb + 1;
    while (i < rb) {
        char* end = nullptr;
        double v = std::strtod(j.c_str() + i, &end);
        if (end == j.c_str() + i) { ++i; continue; }
        out.push_back(v);
        i = static_cast<size_t>(end - j.c_str());
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: svf-reference <vector.json>\n"); return 2; }
    const std::string j = slurp(argv[1]);
    const std::vector<double> sr = numbersAfter(j, "sampleRate");
    const std::vector<double> input = numbersAfter(j, "input");
    // params: pairs of id,norm harvested from the "params" array's numbers.
    const std::vector<double> params = numbersAfter(j, "params");

    std::vector<float> buf(input.size());
    for (size_t i = 0; i < input.size(); ++i) buf[i] = static_cast<float>(input[i]);

    SvfHandle* h = svf_create();
    svf_prepare(h, sr.empty() ? 48000.0 : sr[0], static_cast<int>(buf.size()), 1);
    for (size_t p = 0; p + 1 < params.size(); p += 2)
        svf_set_param(h, static_cast<unsigned char>(params[p]), static_cast<float>(params[p + 1]));
    svf_process(h, buf.data(), static_cast<int>(buf.size()));
    svf_destroy(h);

    std::printf("{\"version\":1,\"output\":[");
    for (size_t i = 0; i < buf.size(); ++i)
        std::printf("%s%.9g", i ? "," : "", static_cast<double>(buf[i]));
    std::printf("]}\n");
    return 0;
}
```

- [ ] **Step 3: Add the native reference exe to the host branch of `adapters/web/CMakeLists.txt`**

Inside the existing `if(NOT EMSCRIPTEN)` block, after `acfx_web_abi`:

```cmake
  add_executable(svf-reference svf-reference-main.cpp)
  target_link_libraries(svf-reference PRIVATE acfx_web_abi acfx_core)
  target_compile_features(svf-reference PRIVATE cxx_std_20)
```

- [ ] **Step 4: Build the reference and run it on the fixture**

```bash
cmake --build build/web-ref --target svf-reference
./build/web-ref/adapters/web/svf-reference adapters/web/test/vectors/lowpass-sweep.json
```

Expected: prints `{"version":1,"output":[...16 numbers...]}` where the output differs from the input.

- [ ] **Step 5: Commit**

```bash
git add adapters/web/test/vectors/lowpass-sweep.json adapters/web/svf-reference-main.cpp adapters/web/CMakeLists.txt
git commit -m "test(web): versioned input vectors + native SVF reference CLI"
```

---

### Task 6: Parity test — WASM vs native reference

**Files:**
- Create: `adapters/web/test/svf-parity.test.ts`

**Interfaces:**
- Consumes: `SvfModule.load` (Task 4), the `web` build's `svf.mjs` (Task 3), the `svf-reference` exe (Task 5), and the fixture (Task 5).

- [ ] **Step 1: Write the parity test** — `adapters/web/test/svf-parity.test.ts`:

```ts
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { SvfModule } from "../loader/svf-module.ts";

interface Vector { version: number; sampleRate: number; params: { id: number; norm: number }[]; input: number[]; }

const here = fileURLToPath(new URL(".", import.meta.url));
const wasmModuleUrl = new URL("../../../build/web/svf.mjs", import.meta.url).href;
const referenceExe = fileURLToPath(new URL("../../../build/web-ref/adapters/web/svf-reference", import.meta.url));
const vectorPath = `${here}vectors/lowpass-sweep.json`;

function nativeOutput(path: string): number[] {
  const raw = execFileSync(referenceExe, [path], { encoding: "utf8" });
  return (JSON.parse(raw) as { output: number[] }).output;
}

async function wasmOutput(vec: Vector): Promise<number[]> {
  const mod = await SvfModule.load(wasmModuleUrl);
  const h = mod.create();
  mod.prepare(h, vec.sampleRate, vec.input.length, 1);
  for (const p of vec.params) mod.setParam(h, p.id, p.norm);
  const buf = Float32Array.from(vec.input);
  mod.process(h, buf);
  mod.destroy(h);
  return Array.from(buf);
}

describe("SVF WASM/native parity", () => {
  it("WASM output matches the native reference within tolerance", async () => {
    const vec = JSON.parse(readFileSync(vectorPath, "utf8")) as Vector;
    const native = nativeOutput(vectorPath);
    const wasm = await wasmOutput(vec);
    expect(wasm.length).toBe(native.length);
    for (let i = 0; i < native.length; i++) {
      expect(Math.abs(wasm[i]! - native[i]!)).toBeLessThan(1e-6);
    }
  });
});
```

- [ ] **Step 2: Ensure both builds exist, then run the parity test**

```bash
source ~/emsdk/emsdk_env.sh
cmake --preset web     && cmake --build build/web     --target svf
cmake --preset web-ref && cmake --build build/web-ref --target svf-reference
cd adapters/web && npm test
```

Expected: the `SVF WASM/native parity` test PASSES (every sample within `1e-6`).

- [ ] **Step 3: Commit**

```bash
git add adapters/web/test/svf-parity.test.ts
git commit -m "test(web): WASM/native parity test on shared input vectors"
```

---

### Task 7: Local orchestration (Makefile) + phase docs

**Files:**
- Modify: `Makefile`
- Create: `adapters/web/README.md`

**Interfaces:**
- Produces: `make web-ref`, `make web-wasm`, `make web-parity` — the local (non-CI) entry points.

- [ ] **Step 1: Add targets to `Makefile`**

```makefile
# --- web adapter (Phase 1). All local; CI builds nothing. ---
.PHONY: web-ref web-wasm web-parity
web-ref:
	cmake --preset web-ref && cmake --build build/web-ref --target svf-reference acfx_web_abi_native_test
	ctest --test-dir build/web-ref -R acfx_web_abi_native_test --output-on-failure

web-wasm:
	cmake --preset web && cmake --build build/web --target svf

web-parity: web-ref web-wasm
	cd adapters/web && npm install && npm run typecheck && npm test
```

- [ ] **Step 2: Write `adapters/web/README.md`**

```markdown
# adapters/web — WebAssembly adapter (Phase 1)

The browser adapter: one extern-"C" ABI over `acfx::SvfEffect`, compiled to
`svf.wasm` (Emscripten) and to a native `svf-reference` CLI from the same source.
A parity test proves the two agree.

## Build & test (local only — CI builds nothing)

Prereq: `source ~/emsdk/emsdk_env.sh` (Emscripten), Node >= 22.

- `make web-ref`    — native ABI test + reference CLI
- `make web-wasm`   — the WASM module (`build/web/svf.{mjs,wasm}`)
- `make web-parity` — typecheck + WASM/native parity test

Depends only inward on `acfx_core` (Constitution VI). The analysis ABI
(frequency response / poles-zeros / impulse) arrives with the visualizer (Phase 5).
```

- [ ] **Step 3: Run the full local flow**

```bash
source ~/emsdk/emsdk_env.sh
make web-parity
```

Expected: typecheck clean; parity test PASSES.

- [ ] **Step 4: Commit and push**

```bash
git add Makefile adapters/web/README.md
git commit -m "build(web): local make targets + adapters/web README (Phase 1 complete)"
git push
```

---

## Self-Review

**Spec coverage (design §4.1 / §6, Phase-1 slice):**
- Audio C ABI over the real `SvfEffect` → Tasks 2–3. ✓
- Emscripten WASM build as a thin `adapters/web` target → Tasks 1, 3. ✓
- Strict-TypeScript loader → Task 4. ✓
- Parity via native reference + shared versioned input vectors (no copied constants) → Tasks 5–6. ✓
- All builds/tests local, CI builds nothing → Task 7 (Makefile), Prerequisites. ✓
- Core untouched; depends inward → ABI in `adapters/web` linking `acfx_core` only. ✓
- Analysis ABI explicitly re-sequenced to Phase 5 (surfaced, not cut). ✓

**Placeholder scan:** no TBD/TODO; every code + command step is concrete. ✓

**Type consistency:** ABI names (`svf_create/destroy/prepare/set_param/process`) identical across `svf-web-abi.h`, the impl, the native test, `svf-reference-main.cpp`, the emcc `EXPORTED_FUNCTIONS`, and the loader's `EmscriptenSvf` (`_`-prefixed). Loader method names (`load/create/prepare/setParam/process/destroy`) match their use in the parity test. Vector JSON schema identical in the fixture, the reference CLI, and the parity test. ✓
