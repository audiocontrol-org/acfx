# Project Prospectus: acfx — A Progressive Audio DSP & Analog Modeling Platform

> **Program-vision document** (persisted 2026-06-29). This is the north star for the
> multi-phase acfx program; it is governed by the constitution
> (`.specify/memory/constitution.md`, Principles IX–XI) and the stack-control roadmap
> (the `progressive-dsp-platform` program node). It is a vision document, not a Spec Kit
> feature spec.

## Executive Summary

acfx is an open, cross-platform audio DSP framework designed around a simple philosophy:

Write an audio effect once, and run it everywhere.

Today, acfx provides a portable DSP core that can target desktop applications, DAW plugins, and embedded hardware from a single implementation. The proposed next phase expands the project into a comprehensive platform for developing, understanding, and deploying modern audio effects—from simple digital filters to faithful models of classic analog hardware.

Rather than treating DSP algorithms as isolated implementations, acfx will organize them into a progressive curriculum and reusable library of primitives. The resulting platform serves three audiences simultaneously:

* Developers building production audio software
* Embedded developers creating hardware effects
* Students and engineers learning advanced audio DSP

The long-term vision is to create the open reference implementation for modern audio effects development.

⸻

## Vision

Most audio DSP libraries provide implementations.

Most textbooks provide theory.

Very few projects connect the two.

acfx aims to bridge this gap by providing:

* reusable production-quality DSP components
* educational laboratory exercises
* progressively more sophisticated implementations
* portable code that executes unchanged on desktop and embedded systems

Every concept should progress through four stages:

Theory → Laboratory → Reusable Primitive → Production Effect

This structure encourages both learning and software reuse.

⸻

## Guiding Principles

### One Source, Many Targets

Effects should compile unchanged for:

* desktop workbench
* VST3 / AU / CLAP plugins
* Daisy
* Teensy
* future embedded targets

Platform-specific code remains isolated within adapters.

⸻

### Build Complexity Incrementally

Modern analog modeling is difficult because it combines DSP, numerical methods, and electrical engineering.

Instead of presenting advanced algorithms as a “black box,” acfx introduces one new concept at a time.

Each phase introduces only a single major idea before applying it to a complete effect.

⸻

### Production Quality

Educational code should not be disposable.

Laboratory implementations should evolve naturally into reusable primitives.

Every algorithm should ultimately become a stable, tested production component.

⸻

### Measurable Engineering

Every effect should be validated using objective measurements.

Examples include:

* frequency response
* harmonic distortion
* impulse response
* phase response
* latency
* CPU usage
* memory allocation
* numerical stability

Listening tests complement measurements but do not replace them.

⸻

## Project Architecture

The project is organized into three complementary layers.

### 1. Laboratories

Laboratories introduce new concepts through small, focused experiments.

Examples:

* one-pole filter
* waveshaping
* oversampling
* diode clipping
* RC circuit modeling
* convolution
* wave digital filters

Each laboratory contains:

* background theory
* implementation walkthrough
* visualization tools
* measurements
* listening examples

⸻

### 2. Primitives

Once understood, concepts become reusable library components.

Examples:

```
filters/
nonlinear/
dynamics/
analog/
circuit/
convolution/
wdf/
physical/
```

These primitives form the foundation for higher-level effects.

⸻

### 3. Effects

Effects demonstrate composition rather than isolated algorithms.

Examples include:

Saturation

* Soft Clip
* Tape
* Console
* Tube Preamp

Distortion

* Tube Screamer
* Rat
* Big Muff

Compression

* Optical
* FET
* VCA

Reverb

* Schroeder
* Moorer
* Convolution
* Spring

Each effect documents which primitives it uses, making the implementation transparent and educational.

⸻

## Development Roadmap

### Phase 1 — Digital Fundamentals

Deliverables

* Filters
* Delay
* Modulation
* Parameter system
* Measurement infrastructure

⸻

### Phase 2 — Nonlinear DSP

Deliverables

* Waveshapers
* Saturation
* Oversampling
* Harmonic analysis

⸻

### Phase 3 — Dynamic Systems

Deliverables

* Envelope followers
* Compressors
* Program-dependent saturation
* Tape dynamics

⸻

### Phase 4 — Circuit Modeling

Deliverables

* Component abstractions
* Passive tone stacks
* Diode clippers
* Operational amplifier stages

⸻

### Phase 5 — Numerical Circuit Solvers

Deliverables

* Modified Nodal Analysis
* Newton iteration
* Implicit integration

⸻

### Phase 6 — Wave Digital Filters

Deliverables

* WDF primitives
* Adaptors
* Passive networks
* Complete analog stages

⸻

### Phase 7 — Physical Modeling

Deliverables

* Digital waveguides
* Resonators
* Spring models
* String models

⸻

### Phase 8 — Convolution

Deliverables

* FIR convolution
* FFT convolution
* Partitioned convolution
* Cabinet simulation
* Reverb engines

⸻

### Phase 9 — Reference Hardware Models

Complete implementations of historically significant hardware.

Examples include:

* Tube Screamer
* Rat
* Big Muff
* Fender tone stack
* Neve preamp
* Tape machine
* Optical compressor

⸻

## Why This Matters

Modern commercial audio plugins increasingly rely on advanced numerical modeling, physical simulation, and hybrid techniques.

However, much of this knowledge remains fragmented across academic papers, conference proceedings, and proprietary implementations.

acfx seeks to make these techniques understandable, reproducible, and portable.

The project lowers the barrier to entry for advanced DSP while providing a robust foundation for production software.

⸻

## Long-Term Vision

The ultimate goal is to establish acfx as the reference platform for portable audio DSP development.

Success is measured not only by the number of available effects, but by the project’s ability to teach the engineering behind them.

An engineer should be able to begin with a simple digital filter and, through progressively more sophisticated laboratories and reusable components, eventually understand and implement faithful models of classic analog hardware.

In doing so, acfx becomes more than an effects library—it becomes a complete engineering curriculum for modern audio DSP.
