> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: Workbench audio device + source + MIDI selection (in-UI)

**Feature Branch**: `platform-foundation`

**Created**: 2026-06-26

**Status**: Draft

**Input**: Make the desktop workbench usable by a person: select audio input/output
devices, the audio source (live input or a chosen file), and MIDI inputs from the
UI, and remember those choices across launches — without re-introducing any
real-time audio glitch, stall, or crash.

## User Scenarios & Testing *(mandatory)*

Today the workbench uses only the operating system's default audio device, picks
its source from an environment variable, and auto-enables all MIDI inputs. A person
who wants to route specific audio in and out of it, or play a file through it,
cannot do so from the UI. These stories make it usable.

### User Story 1 - Choose audio input and output devices (Priority: P1)

A person opens the workbench, opens an audio-settings surface, and selects which
audio **input** and **output** device the workbench uses (and, if they wish, the
sample rate and buffer size). The change takes effect without quitting the app.

**Why this priority**: This is the core of "usable" — without it you are stuck on
whatever the OS default device is and cannot route audio to your interface,
speakers, or a loopback device. It is the minimum viable slice.

**Independent Test**: Open the workbench, open audio settings, change the output
device to a different available device, and confirm processed audio now plays out
of the newly chosen device; change the input device and confirm the new input is
what gets filtered.

**Acceptance Scenarios**:

1. **Given** the workbench is running on the default device, **When** the person
   selects a different output device in audio settings, **Then** audio plays out of
   the newly selected device without an app restart and without a crash or a stuck
   stream.
2. **Given** the workbench is running, **When** the person selects a different input
   device, **Then** the filter processes audio from the newly selected input.
3. **Given** an output device fails to open, **When** the person selects it, **Then**
   the failure is shown to the person (not silently swallowed) and the previous
   working device remains in use.

---

### User Story 2 - Choose the audio source from the UI (Priority: P2)

A person selects, in the UI, whether the workbench filters the **live input device**
or loops an **audio file** they pick with a file chooser — with no reliance on an
environment variable.

**Why this priority**: The built-in file player is the deterministic source for
reproducible A/B listening; today it is only reachable via an env var. Making source
choice a first-class UI control is essential to "usable," just below device choice.

**Independent Test**: With the workbench running, switch the source to "File," pick
an audio file, and confirm it loops through the filter; switch back to "Live" and
confirm the input device is filtered again — all without restarting the app.

**Acceptance Scenarios**:

1. **Given** the workbench is filtering live input, **When** the person switches the
   source to a chosen file, **Then** the file loops through the filter and the A/B
   toggle compares dry vs filtered file audio — with no audible glitch or crash at
   the moment of switching.
2. **Given** the source is set to "File," **When** the person picks a different file,
   **Then** the new file plays without a restart.
3. **Given** the person selects "File" but cancels without choosing a file, **When**
   the chooser closes, **Then** the source remains a valid choice (it does not enter
   a broken no-source state) and nothing fails silently.

---

### User Story 3 - Remember selections across launches (Priority: P2)

The workbench remembers the person's device, sample-rate/buffer, source, and MIDI
selections, so relaunching restores them without re-selecting everything.

**Why this priority**: A tool the person has to fully reconfigure every launch is not
usable in practice. Persistence is what turns one-time selection into a usable tool.

**Independent Test**: Select non-default devices and a file source, quit the
workbench, relaunch it, and confirm the same devices and source are active without
re-selecting.

**Acceptance Scenarios**:

1. **Given** the person has selected specific input/output devices and a source,
   **When** they quit and relaunch the workbench, **Then** the same devices and
   source are active on launch.
2. **Given** a previously selected device is no longer present at next launch, **When**
   the workbench starts, **Then** it falls back to an available device and surfaces
   that the saved device was unavailable (no silent stuck/broken state).

---

### User Story 4 - Choose MIDI input devices (Priority: P3)

A person selects which MIDI input device(s) drive the workbench's parameter CCs,
rather than every input being enabled implicitly.

**Why this priority**: Useful for anyone with multiple controllers, but the existing
auto-enable-all behavior is an acceptable default; explicit selection is the polish
on top of devices, source, and persistence.

**Independent Test**: Connect two MIDI controllers, enable only one in the UI, and
confirm only that controller's CC 74/71 move cutoff/resonance.

**Acceptance Scenarios**:

1. **Given** multiple MIDI inputs are available, **When** the person enables one and
   disables another, **Then** only the enabled device's CCs affect the filter.

---

### Edge Cases

- **No input device available**: choosing "Live" with no input must surface a clear
  message and not leave the workbench in a broken/silent state (it can prompt for the
  file source as an explicit choice, never silently).
- **Device removed while running** (e.g. unplugged interface): the workbench must not
  crash; it surfaces the loss and recovers to an available device.
- **Source / device change under load**: switching source or device while audio is
  flowing must never produce a torn buffer, a stuck stream, a lock-up, or a crash —
  the change happens with the audio engine stopped.
- **Corrupt or unreadable saved settings**: a malformed settings file must not crash
  startup; the workbench starts with safe defaults and reports the problem.
- **Saved file source missing at next launch**: surfaced, with a safe non-broken
  state, not silent silence.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The workbench MUST let the person select the audio **input** device and
  the audio **output** device from the UI, from the devices the system reports.
- **FR-002**: The workbench MUST let the person adjust sample rate and buffer size for
  the selected device from the UI.
- **FR-003**: The workbench MUST let the person choose the audio **source** in the UI:
  the live input device, or an audio **file** selected via a file chooser.
- **FR-004**: The workbench MUST NOT require an environment variable to reach the
  built-in file player (the env var MAY remain as a first-run convenience only).
- **FR-005**: The workbench MUST let the person select which **MIDI input** device(s)
  are active from the UI.
- **FR-006**: The workbench MUST **persist** the device, sample-rate/buffer, source
  (including the chosen file), and MIDI selections, and **restore** them on the next
  launch.
- **FR-007**: Every device or source change MUST take effect **without restarting the
  application process**.
- **FR-008**: Every device or source change MUST be applied only while the audio
  engine is **stopped**, so no change can produce a torn/garbage buffer, a stuck
  stream, an audio-thread lock, or a crash (real-time safety is preserved).
- **FR-009**: Failures (device fails to open, file unreadable, saved device/file
  missing, corrupt settings) MUST be **surfaced to the person** and leave the
  workbench in a safe, non-broken state — never silently swallowed and never replaced
  with mock/placeholder audio.
- **FR-010**: The audio-settings surface MUST be reachable from the main window and
  MUST NOT clutter the main sketch-and-hear controls (cutoff/resonance/mode + A/B).
- **FR-011**: The feature MUST NOT change the platform-independent core, the host
  `ProcessorNode` boundary, or their real-time guarantees — it is workbench-adapter
  scope only.

### Key Entities

- **Audio selection**: the chosen input device, output device, sample rate, and
  buffer size for the workbench's audio engine.
- **Source selection**: whether the workbench filters live input or a looped file,
  plus the chosen file's location when in file mode.
- **MIDI selection**: the set of enabled MIDI input devices.
- **Persisted settings**: the saved form of the above, restored on launch.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A person can route the workbench to a non-default input AND output
  device entirely from the UI, with audio flowing to/from the chosen devices, without
  editing a config file or setting an environment variable.
- **SC-002**: A person can play an audio file of their choosing through the filter,
  selected from the UI, without restarting the app and without an environment
  variable.
- **SC-003**: Switching device or source while audio is running never produces an
  audible torn/garbage buffer, a stuck stream, a lock-up, or a crash across repeated
  switches.
- **SC-004**: After selecting non-default devices and a source and relaunching, the
  same selections are active on launch without any re-selection.
- **SC-005**: A person can route a chosen MIDI controller's CCs to the filter
  parameters while a non-selected controller has no effect.
- **SC-006**: Every failure path (unavailable device, unreadable/missing file, corrupt
  settings) results in a visible message and a usable workbench, with zero instances
  of silent failure or placeholder audio.

## Assumptions

- The "person" is a developer/sound designer running the desktop workbench locally;
  there is a display and the platform's standard audio/MIDI stack.
- Routing another application's audio into the workbench (via a loopback device such
  as BlackHole) is achieved by selecting that loopback as the input device — the
  workbench provides device selection, not virtual-device creation.
- A single settings profile per user is sufficient; multiple named profiles are out
  of scope.
- The audio-settings UI presents the devices/rates/buffers the platform reports; the
  workbench does not invent or emulate devices.
- The existing real-time-safe core, parameter handoff, and in-memory file player are
  reused unchanged; this feature only changes how/when the source is (re)configured
  and how devices/MIDI are chosen and persisted.
