#pragma once

#include "labs/component-abstractions/solver/linear-solver.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/resistor.h"
#include "primitives/circuit/models/capacitor.h"
#include "primitives/circuit/models/inductor.h"
#include "primitives/circuit/models/sources.h"
#include "primitives/circuit/models/companion.h"
#include "primitives/circuit/node.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <variant>

// TransientClipper — the bounded TRANSIENT nonlinear solver for a single diode
// clipping port with reactive memory (US2; contracts/transient-clipper.h;
// research.md R3/R4; FR-008..014). It advances an assembled clipper netlist one
// backward-Euler timestep: each reactive element is discretised as a companion
// (via the frozen capacitor.h/inductor.h companion() hooks) and the diode
// nonlinearity is resolved by a bounded, voltage-limited Newton iteration.
//
// NON-NORMATIVE, PHASE-5-SUPERSEDED SCAFFOLDING (see core/labs/diode-clippers/
// README.md). LAB code, host-only, C++20 OK. It builds on the component-
// abstractions LinearSolver and the static NewtonClipper companion-linearization
// pattern; it must NEVER grow into general MNA / gmin / a multi-nonlinearity
// engine (FR-012).
//
// THE LOAD-BEARING MECHANISM — SEPARATED TIMESTEP / NEWTON LOOPS (FR-009). The
// static NewtonClipper refuses reactive elements because it reuses
// LinearSolver::solve() inside its Newton loop, and that call advances reactive
// history on EVERY invocation — so a capacitor's history would be advanced N
// times per sample and the transient would be corrupted. This solver fixes that
// by keeping the reactive elements OUT of the netlist the LinearSolver sees:
// each reactive element is replaced, once per timestep, by its FIXED
// backward-Euler companion (a Resistor + a CurrentSource computed from
// solver-held history), so the LinearSolver never sees a Capacitor/Inductor and
// its per-solve history advance is a no-op. The inner Newton iterations re-solve
// the same purely-linear system with only the DIODE companion changing; the
// reactive history is advanced EXACTLY ONCE, after the loop. That is the
// reactive+nonlinear case component-abstractions deliberately refused.
//
// THE SEAM (FR-006): the solver ASSEMBLES; each COMPONENT SUPPLIES ITS PHYSICS.
// It reads Diode::evaluate / Diode::limitJunctionVoltage and
// Capacitor/Inductor::companion(dt, ·); it never re-derives a constitutive law.
//
// NO FALLBACK / NO FABRICATION (FR-011): non-convergence within the iteration
// bound returns a NewtonStatus with converged == false plus the last iterate and
// residuals — never a substituted or fabricated output. dt <= 0 and a singular
// reduced system are descriptive throws (FR-013). double throughout (FR-014).
//
// NO HEAP ON step() (FR-008/010): the augmented netlist is a fixed-capacity
// Netlist<MaxNodes, MaxComponents + 2*MaxDiodes> built on the stack; the nested
// LinearSolver allocates nothing; all history is std::array.

namespace acfx::labs::diode_clippers {

// Per-sample convergence report (FR-011). converged == false is a legitimate,
// surfaced result: the node voltages are then the last (non-converged) iterate
// and must not be trusted as a physical answer.
struct NewtonStatus {
    bool converged = false;         // did |Δv| fall below the voltage tolerance?
    int iterations = 0;             // Newton iterations actually consumed (<= N)
    double voltageResidual = 0.0;   // final |v_{k+1} - v_k| across the port (V)
    double currentResidual = 0.0;   // final |ΔI| of the total diode current (A)
};

template <int MaxNodes, int MaxComponents, int MaxDiodes = 4>
class TransientClipper {
public:
    static_assert(MaxNodes > 0, "TransientClipper requires MaxNodes >= 1 (ground)");
    static_assert(MaxComponents > 0, "TransientClipper requires MaxComponents >= 1");
    static_assert(MaxDiodes > 0, "TransientClipper requires MaxDiodes >= 1");

    // Iteration bound N and convergence tolerances are exposed (research R4):
    // tune them against the harness, not the architecture; do not silently
    // retune to hide a non-converging case (FR-011).
    explicit TransientClipper(int maxIterations = 50,
                              double voltageTol = 1e-9,
                              double currentTol = 1e-12)
        : maxIterations_(maxIterations),
          voltageTol_(voltageTol),
          currentTol_(currentTol) {
        if (maxIterations_ < 1) {
            throw std::invalid_argument(
                "transient-clipper: iteration bound N must be >= 1");
        }
        if (voltageTol_ <= 0.0 || currentTol_ <= 0.0) {
            throw std::invalid_argument(
                "transient-clipper: voltage/current tolerances must be > 0");
        }
        reset();
    }

    // Return to a cold circuit at rest: clear reactive history, the warm-start
    // port voltage, and the underlying linear solver's state.
    void reset() noexcept {
        warmStart_ = 0.0;
        portP_ = kGround;
        portN_ = kGround;
        diodeCount_ = 0;
        prevNodeVoltage_.fill(0.0);
        inductorCurrent_.fill(0.0);
        linear_.reset();
    }

    // Advance one backward-Euler timestep of length dt and solve the clipper by
    // the separated-loop mechanism above. Warm-starts the port voltage from the
    // previous converged sample. On return, voltage(node) exposes the final
    // iterate's node voltages and the NewtonStatus reports convergence.
    //
    // Throws std::invalid_argument on dt <= 0; std::runtime_error on a second
    // interacting nonlinearity at a distinct node pair, a diode population past
    // MaxDiodes, or a singular reduced system (via LinearSolver).
    NewtonStatus step(const Netlist<MaxNodes, MaxComponents>& nl, double dt) {
        if (!(dt > 0.0)) {
            throw std::invalid_argument(
                "transient-clipper: timestep dt must be > 0");
        }

        collectPort(nl);  // records the single port + its diodes; validates scope

        double v = warmStart_;
        NewtonStatus status;
        double prevCurrent = totalDiodeCurrent(v);

        for (int iter = 1; iter <= maxIterations_; ++iter) {
            // Build the purely-linear augmented system: linear originals verbatim,
            // reactive elements replaced by their FIXED per-timestep companion,
            // the diode string linearized at the current guess v.
            const AugNetlist aug = buildAugmented(nl, v, dt);
            linear_.solve(aug, dt);

            if (diodeCount_ == 0) {
                // Pure linear + reactive network (e.g. the RC sanity net): one
                // solve is exact — there is no nonlinearity to iterate.
                status.iterations = iter;
                status.converged = true;
                break;
            }

            const double vRaw = linear_.voltage(portP_) - linear_.voltage(portN_);
            const double vLimited = limitStep(vRaw, v);  // pnjlim per diode

            const double dv = std::fabs(vLimited - v);
            const double newCurrent = totalDiodeCurrent(vLimited);
            const double di = std::fabs(newCurrent - prevCurrent);

            v = vLimited;
            prevCurrent = newCurrent;

            status.iterations = iter;
            status.voltageResidual = dv;
            status.currentResidual = di;

            if (dv < voltageTol_) {
                status.converged = true;
                break;
            }
        }

        warmStart_ = v;
        // Advance the reactive history EXACTLY ONCE, from the final solved node
        // voltages — the second half of the loop-separation mechanism (FR-009).
        advanceHistory(nl, dt);
        return status;
    }

    // Node voltage of the final iterate (ground == 0). Physically meaningful only
    // when the matching step() returned converged == true.
    double voltage(NodeId node) const { return linear_.voltage(node); }

    // Converged voltage across the clipper port, V(portP) - V(portN).
    double clipperVoltage() const {
        return linear_.voltage(portP_) - linear_.voltage(portN_);
    }

    int maxIterations() const noexcept { return maxIterations_; }
    double voltageTolerance() const noexcept { return voltageTol_; }
    double currentTolerance() const noexcept { return currentTol_; }

private:
    // The augmented, fully-linear netlist handed to LinearSolver: each reactive
    // element AND each diode contributes a Resistor + CurrentSource companion, so
    // the worst case is the original slots plus 2 per diode.
    using AugNetlist = Netlist<MaxNodes, MaxComponents + 2 * MaxDiodes>;

    // One diode of the clipper string with its orientation relative to the port
    // variable v = V(portP) - V(portN): orient == +1 when the diode's anode is
    // portP (vAK == v), -1 when reversed (vAK == -v).
    struct PortDiode {
        Diode diode;
        double orient;
    };

    // Scan the netlist, establish the SINGLE clipper port (the diode-string node
    // pair), and validate the bounded single-nonlinearity-location scope
    // (FR-012). A diode-free netlist (a linear/reactive sanity net) is allowed:
    // diodeCount_ == 0, no port.
    void collectPort(const Netlist<MaxNodes, MaxComponents>& nl) {
        diodeCount_ = 0;
        portP_ = kGround;
        portN_ = kGround;
        bool portSet = false;

        const auto comps = nl.components();
        for (std::size_t i = 0; i < comps.size(); ++i) {
            const auto* d = std::get_if<Diode>(&comps[i]);
            if (d == nullptr) {
                continue;
            }
            if (!portSet) {
                portP_ = d->anode;
                portN_ = d->cathode;
                portSet = true;
            }
            const double orient = orientationOf(*d);
            if (orient == 0.0) {
                // A diode on a DIFFERENT node pair is a second, interacting
                // nonlinearity — out of the bounded single-port scope.
                throw std::runtime_error(
                    "transient-clipper: a second nonlinearity at a distinct node "
                    "pair is out of the bounded single-port scope — deferred to "
                    "Phase 5");
            }
            if (diodeCount_ >= MaxDiodes) {
                throw std::runtime_error(
                    "transient-clipper: diode population exceeds MaxDiodes — out "
                    "of bounded scope (deferred to Phase 5)");
            }
            diodes_[static_cast<std::size_t>(diodeCount_)] = PortDiode{*d, orient};
            ++diodeCount_;
        }
    }

    // +1 if the diode's anode is portP and cathode portN; -1 if reversed (same
    // node pair, opposite orientation — antiparallel); 0 if it spans a different
    // node pair (a distinct nonlinearity location).
    double orientationOf(const Diode& d) const noexcept {
        if (d.anode == portP_ && d.cathode == portN_) {
            return 1.0;
        }
        if (d.anode == portN_ && d.cathode == portP_) {
            return -1.0;
        }
        return 0.0;
    }

    // Build the augmented linear netlist for port-voltage guess v. Linear
    // originals (Resistor / VoltageSource / CurrentSource) are copied verbatim;
    // each reactive element is replaced by its FIXED backward-Euler companion
    // (from held history — the SAME value every Newton iteration of this
    // timestep); each diode is linearized at v into its Norton companion.
    AugNetlist buildAugmented(const Netlist<MaxNodes, MaxComponents>& nl,
                              double v, double dt) const {
        AugNetlist aug;
        for (int k = 1; k < nl.nodeCount(); ++k) {
            aug.addNode();  // reproduce the node numbering (node k -> node k)
        }

        const auto comps = nl.components();
        for (std::size_t i = 0; i < comps.size(); ++i) {
            const Component& c = comps[i];
            if (std::holds_alternative<Resistor>(c) ||
                std::holds_alternative<VoltageSource>(c) ||
                std::holds_alternative<CurrentSource>(c)) {
                aug.add(c);  // linear element — verbatim
            } else if (const auto* cap = std::get_if<Capacitor>(&c)) {
                // Fixed capacitor companion from held (previous-step) node
                // voltages: branch current a->b is Geq*(v_a - v_b) - Ieq, so Geq
                // is a Resistor and +Ieq at a / -Ieq at b is a CurrentSource.
                const double vPrev = prevNodeVoltage(cap->a) - prevNodeVoltage(cap->b);
                const Companion comp = cap->companion(dt, vPrev);
                aug.add(Resistor{cap->a, cap->b, 1.0 / comp.Geq});
                aug.add(CurrentSource{cap->a, cap->b, comp.Ieq});
            } else if (const auto* ind = std::get_if<Inductor>(&c)) {
                // Fixed inductor companion (dual of the capacitor), from the held
                // branch current for this component slot.
                const double iPrev = inductorCurrent_[i];
                const Companion comp = ind->companion(dt, iPrev);
                aug.add(Resistor{ind->a, ind->b, 1.0 / comp.Geq});
                aug.add(CurrentSource{ind->a, ind->b, comp.Ieq});
            }
            // Diode: stripped here; its companion is appended below at guess v.
        }

        for (int i = 0; i < diodeCount_; ++i) {
            const PortDiode& pd = diodes_[static_cast<std::size_t>(i)];
            const double vAK = pd.orient * v;
            const DiodeSample s = pd.diode.evaluate(vAK);
            const double Geq = s.conductance;          // > 0 always (passive)
            const double Ieq = s.current - Geq * vAK;
            aug.add(Resistor{pd.diode.anode, pd.diode.cathode, 1.0 / Geq});
            aug.add(CurrentSource{pd.diode.anode, pd.diode.cathode, -Ieq});
        }
        return aug;
    }

    // Apply junction voltage limiting (pnjlim) for the whole clipper string: map
    // the port voltage into each diode's frame (vAK = orient*v), let the diode
    // clamp its own step, map back. A no-op on a reverse-biased diode.
    double limitStep(double vRaw, double vOld) const noexcept {
        double vLim = vRaw;
        for (int i = 0; i < diodeCount_; ++i) {
            const PortDiode& pd = diodes_[static_cast<std::size_t>(i)];
            const double vakOld = pd.orient * vOld;
            const double vakNew = pd.orient * vLim;
            const double limited = pd.diode.limitJunctionVoltage(vakNew, vakOld);
            vLim = pd.orient * limited;
        }
        return vLim;
    }

    // Total true (Shockley) diode current across the port at guess v — the
    // current-residual report only (not gating).
    double totalDiodeCurrent(double v) const noexcept {
        double total = 0.0;
        for (int i = 0; i < diodeCount_; ++i) {
            const PortDiode& pd = diodes_[static_cast<std::size_t>(i)];
            total += pd.diode.evaluate(pd.orient * v).current;
        }
        return total;
    }

    // Advance reactive history for the next timestep from the final solved node
    // voltages (mirrors LinearSolver::advanceHistory, but owned here because the
    // reactive elements were stripped from the netlist the LinearSolver saw).
    void advanceHistory(const Netlist<MaxNodes, MaxComponents>& nl, double dt) noexcept {
        const auto comps = nl.components();
        for (std::size_t i = 0; i < comps.size(); ++i) {
            const auto* ind = std::get_if<Inductor>(&comps[i]);
            if (ind == nullptr) {
                continue;
            }
            const double iPrev = inductorCurrent_[i];
            const Companion comp = ind->companion(dt, iPrev);
            const double vDiff = linear_.voltage(ind->a) - linear_.voltage(ind->b);
            // i^n = Geq*v^n + iPrev (Ieq = -iPrev), the backward-Euler update.
            inductorCurrent_[i] = comp.Geq * vDiff + iPrev;
        }
        for (int k = 0; k < nl.nodeCount(); ++k) {
            prevNodeVoltage_[static_cast<std::size_t>(k)] = linear_.voltage(k);
        }
    }

    // Previous-step voltage at a node (ground == 0), the capacitor vPrev source.
    double prevNodeVoltage(NodeId node) const noexcept {
        if (isGround(node)) {
            return 0.0;
        }
        return prevNodeVoltage_[static_cast<std::size_t>(node)];
    }

    // ---- fixed-capacity state (NO heap) -----------------------------------

    component_abstractions::LinearSolver<MaxNodes, MaxComponents + 2 * MaxDiodes>
        linear_{};

    int maxIterations_;
    double voltageTol_;
    double currentTol_;

    double warmStart_ = 0.0;  // previous converged port voltage (Newton warm start)

    NodeId portP_ = kGround;
    NodeId portN_ = kGround;
    std::array<PortDiode, static_cast<std::size_t>(MaxDiodes)> diodes_{};
    int diodeCount_ = 0;

    // Reactive companion history owned by this solver (loop-separation):
    //   prevNodeVoltage_ — previous solved node voltages (capacitor vPrev source).
    //   inductorCurrent_ — per-component previous inductor branch currents.
    std::array<double, static_cast<std::size_t>(MaxNodes)> prevNodeVoltage_{};
    std::array<double, static_cast<std::size_t>(MaxComponents)> inductorCurrent_{};
};

}  // namespace acfx::labs::diode_clippers
