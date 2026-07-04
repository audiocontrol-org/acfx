#pragma once

#include "labs/component-abstractions/solver/linear-solver.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/resistor.h"
#include "primitives/circuit/models/sources.h"
#include "primitives/circuit/node.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <variant>

// NewtonClipper — the bounded, voltage-limited Newton solver for a SINGLE
// nonlinearity (one diode, or an antiparallel diode pair treated as one
// clipper element) layered on top of LinearSolver (contract
// reference-solver.md §Nonlinear solve; research.md R2 Shockley + pnjlim;
// FR-015/016).
//
// NON-NORMATIVE, PHASE-5-SUPERSEDED SCAFFOLDING. Like LinearSolver, this is
// LAB code, not a circuit primitive. It exists only to make the single-diode
// clipper transfer curve runnable before real MNA + Newton exists. Phase 5
// (Modified Nodal Analysis + general multi-nonlinearity Newton + implicit
// integration) supersedes it entirely; it must NEVER grow into a general
// nonlinear engine (design D3, OQ2). Do not depend on it as production DSP.
//
// THE SEAM (FR-006): the solver ASSEMBLES; the COMPONENT SUPPLIES ITS PHYSICS.
// Each Newton step reads Diode::evaluate(vAK) -> {current, conductance} and
// Diode::limitJunctionVoltage(vNew, vOld); it never re-derives the Shockley
// law or the pnjlim step clamp. Both live on the Diode because they are
// properties of THAT junction (Is, n, Vt), not of any solver.
//
// HOW IT LAYERS ON LinearSolver (FR-016 — SINGLE nonlinearity only): each
// Newton iteration linearizes the diode at the current voltage guess into its
// standard companion — a conductance Geq = g(vAK) in parallel with an
// equivalent current source Ieq = I(vAK) - Geq*vAK — and injects that
// companion as two ordinary LINEAR components (a Resistor of admittance Geq
// and a CurrentSource of -Ieq) appended to a copy of the netlist. That
// augmented, fully-linear netlist is then handed to LinearSolver, which does
// all the assembly and the Gaussian solve. The diode's own physics is thus
// the ONLY nonlinear thing in the loop; the linear machinery is reused verbatim
// (we never re-implement stamping). This is exactly the "companion + reuse the
// linear solver" resolution the contract calls for.
//
// SCOPE (FR-016, contract §Scope — a hard boundary):
//   - Exactly ONE clipper port: either a single Diode, or an antiparallel pair
//     (two Diodes across the SAME node pair). Their currents simply sum at the
//     shared port; that is still ONE nonlinearity.
//   - A second, INDEPENDENT nonlinearity (a diode on a DIFFERENT node pair, or
//     more than two diodes) is >=2 interacting nonlinear components and is
//     REFUSED with "out of reference-solver scope — deferred to Phase 5".
//   - Reactive elements (Capacitor / Inductor) alongside the diode are NOT
//     supported by this lab layer and are refused (the companion-reuse strategy
//     re-solves the linear network per Newton iteration, which would corrupt
//     LinearSolver's per-timestep reactive history; a correct reactive+nonlinear
//     transient is a Phase 5 subject). This keeps the guarantee honest: the
//     nonlinear contract (SC-004) is a STATIC soft-clip transfer curve.
//
// NO FALLBACK, NO FABRICATION (FR-015): on non-convergence within the iteration
// bound N the solver DOES NOT fall back or fabricate an output — it returns a
// Status with converged == false plus the iteration count and final residuals
// for the caller/test to inspect. Unsupported topology raises a descriptive
// std::exception. Never a silent wrong answer (repo standard).
//
// NO HEAP IN THE ITERATION (FR-013/011): the augmented netlist is a
// fixed-capacity Netlist (a std::array, sized by the template parameters plus a
// small constant for the companion components) built on the stack; LinearSolver
// itself allocates nothing. There is no new/delete anywhere in solve().
//
// double throughout (FR-022). C++17. HOST-ONLY lab code (not required to be
// platform-independent), but standard-library-only with no platform headers.

namespace acfx::labs::component_abstractions {

// Outcome of a single-timestep Newton solve. The caller inspects this rather
// than trusting the node voltages blindly: on `converged == false` the node
// voltages are the last (non-converged) iterate and must NOT be used as a
// physical answer (FR-015).
struct NewtonStatus {
    bool converged = false;    // did |Δv| fall below the voltage tolerance?
    int iterations = 0;        // Newton iterations actually consumed (<= N)
    double voltageResidual = 0.0;   // final |v_k+1 - v_k| across the port (V)
    double currentResidual = 0.0;   // final |ΔI| of the total diode current (A)
};

template <int MaxNodes, int MaxComponents>
class NewtonClipper {
public:
    static_assert(MaxNodes > 0, "NewtonClipper requires MaxNodes >= 1 (ground)");
    static_assert(MaxComponents > 0,
                  "NewtonClipper requires MaxComponents >= 1");

    // Up to two diodes form one clipper (single + antiparallel pair); each
    // contributes a Resistor + a CurrentSource companion, so the augmented
    // netlist needs at most four extra component slots.
    static constexpr int kMaxDiodes = 2;
    static constexpr int kMaxCompanionComponents = 2 * kMaxDiodes;

    // Iteration bound N and convergence tolerances are exposed (contract
    // §Nonlinear solve step 2): tune them against the harness, not the
    // architecture (research R2 "captured, not blocking").
    explicit NewtonClipper(int maxIterations = 50,
                           double voltageTol = 1e-9,
                           double currentTol = 1e-12)
        : maxIterations_(maxIterations),
          voltageTol_(voltageTol),
          currentTol_(currentTol) {
        if (maxIterations_ < 1) {
            throw std::invalid_argument(
                "newton-clipper: iteration bound N must be >= 1");
        }
        if (voltageTol_ <= 0.0 || currentTol_ <= 0.0) {
            // A non-positive tolerance can never be met by |Δv| / |Δi| >= 0, so
            // it would silently render even a well-posed clipper unsolvable
            // (all N iterations, converged == false). Fail loud on bad config,
            // matching the iteration-bound check above.
            throw std::invalid_argument(
                "newton-clipper: voltage/current tolerances must be > 0");
        }
        reset();
    }

    // Clear all state: the warm-start voltage guess and the underlying linear
    // solver's history. Call between independent transient runs.
    void reset() noexcept {
        warmStart_ = 0.0;
        portP_ = kGround;
        portN_ = kGround;
        linear_.reset();
    }

    // Advance one backward-Euler timestep of length `dt` and solve the single
    // clipper by bounded, voltage-limited Newton. Warm-starts from the previous
    // step's converged port voltage. On return, voltage(node) exposes the node
    // voltages of the FINAL iterate and the returned Status reports convergence.
    //
    // Throws std::invalid_argument on dt <= 0; std::runtime_error on an
    // unsupported topology (no diode, reactive element present, or a second
    // independent nonlinearity — the last with the FR-016 scope message).
    NewtonStatus solve(const Netlist<MaxNodes, MaxComponents>& nl, double dt) {
        if (!(dt > 0.0)) {
            throw std::invalid_argument(
                "newton-clipper: timestep dt must be > 0");
        }

        collectClipper(nl);  // records diodeCount_ + the clipper port as members

        double v = warmStart_;
        NewtonStatus status;

        double prevCurrent = totalDiodeCurrent(v);
        for (int iter = 1; iter <= maxIterations_; ++iter) {
            // Linearize the diode(s) at the current guess, inject the companion
            // as linear components, and reuse LinearSolver for the whole solve.
            const AugNetlist aug = buildAugmented(nl, v);
            linear_.solve(aug, dt);

            // New raw port voltage from the fresh node solution.
            const double vRaw = linear_.voltage(portP_) - linear_.voltage(portN_);

            // Junction voltage limiting (research R2 / pnjlim) — damp the step so
            // the exp cannot overflow between iterations. Applied per diode so an
            // antiparallel pair is limited on whichever side is conducting.
            const double vLimited = limitStep(vRaw, v);

            const double dv = std::fabs(vLimited - v);
            const double newCurrent = totalDiodeCurrent(vLimited);
            const double di = std::fabs(newCurrent - prevCurrent);

            v = vLimited;
            prevCurrent = newCurrent;

            status.iterations = iter;
            status.voltageResidual = dv;
            status.currentResidual = di;

            if (dv < voltageTol_) {
                // Voltage settled; accept even if the tiny reverse-saturation
                // current residual never drops below currentTol_. (A separate
                // dv<voltageTol_ && di<currentTol_ branch would be redundant —
                // it is strictly subsumed by this one.)
                status.converged = true;
                break;
            }
        }

        warmStart_ = v;
        return status;
    }

    // Node voltage of the FINAL iterate (ground == 0). Only physically
    // meaningful when the matching solve() returned converged == true.
    double voltage(NodeId node) const {
        return linear_.voltage(node);
    }

    // Converged voltage across the clipper port, V(portP) - V(portN).
    double clipperVoltage() const {
        return linear_.voltage(portP_) - linear_.voltage(portN_);
    }

    int maxIterations() const noexcept { return maxIterations_; }
    double voltageTolerance() const noexcept { return voltageTol_; }
    double currentTolerance() const noexcept { return currentTol_; }

private:
    // The augmented, fully-linear netlist handed to LinearSolver: the original
    // components (diodes included but ignored by the linear layer) plus the
    // per-diode companion Resistor + CurrentSource.
    using AugNetlist = Netlist<MaxNodes, MaxComponents + kMaxCompanionComponents>;

    // One diode of the clipper together with its orientation relative to the
    // port variable v = V(portP) - V(portN): orient == +1 when the diode's
    // anode is portP (vAK == v), -1 when reversed (vAK == -v).
    struct ClipperDiode {
        Diode diode;
        double orient;
    };

    // Scan the netlist, validate the SINGLE-nonlinearity scope (FR-016), and
    // record the clipper diode(s) + the port node pair. Returns the diode count.
    int collectClipper(const Netlist<MaxNodes, MaxComponents>& nl) {
        diodeCount_ = 0;
        const auto comps = nl.components();
        for (std::size_t i = 0; i < comps.size(); ++i) {
            const Component& c = comps[i];
            if (std::holds_alternative<Capacitor>(c) ||
                std::holds_alternative<Inductor>(c)) {
                throw std::runtime_error(
                    "newton-clipper: reactive element (Capacitor/Inductor) "
                    "alongside the diode is not supported by the reference "
                    "Newton layer — deferred to Phase 5");
            }
            const auto* d = std::get_if<Diode>(&c);
            if (d == nullptr) {
                continue;
            }
            if (diodeCount_ == 0) {
                // The first diode defines the clipper port and its reference
                // orientation.
                portP_ = d->anode;
                portN_ = d->cathode;
            }
            const double orient = orientationOf(*d);
            if (orient == 0.0) {
                // A diode on a DIFFERENT node pair is a second, independent
                // nonlinearity — out of scope for the reference solver.
                throw std::runtime_error(
                    "out of reference-solver scope — deferred to Phase 5");
            }
            if (diodeCount_ >= kMaxDiodes) {
                throw std::runtime_error(
                    "out of reference-solver scope — deferred to Phase 5");
            }
            clipper_[static_cast<std::size_t>(diodeCount_)] =
                ClipperDiode{*d, orient};
            ++diodeCount_;
        }

        if (diodeCount_ == 0) {
            throw std::runtime_error(
                "newton-clipper: no diode present — this solver requires "
                "exactly one nonlinearity (use LinearSolver for a linear "
                "netlist)");
        }
        return diodeCount_;
    }

    // +1 if the diode's anode is the port's positive node and its cathode the
    // negative node; -1 if reversed (same node pair, opposite orientation —
    // the antiparallel companion); 0 if it spans a different node pair.
    double orientationOf(const Diode& d) const noexcept {
        if (d.anode == portP_ && d.cathode == portN_) {
            return 1.0;
        }
        if (d.anode == portN_ && d.cathode == portP_) {
            return -1.0;
        }
        return 0.0;
    }

    // Build the augmented linear netlist for the current port-voltage guess `v`.
    // Reproduces the original node set and components, then appends each diode's
    // Newton companion (Geq conductance as a Resistor of R = 1/Geq, and the
    // equivalent current source Ieq injected as a CurrentSource of -Ieq so the
    // constant term lands on the RHS with the correct sign).
    AugNetlist buildAugmented(const Netlist<MaxNodes, MaxComponents>& nl,
                              double v) const {
        AugNetlist aug;
        // Reproduce the original node numbering (node k -> local k); addNode()
        // hands out 1,2,... in order, matching the source netlist exactly.
        for (int k = 1; k < nl.nodeCount(); ++k) {
            aug.addNode();
        }
        const auto comps = nl.components();
        for (std::size_t i = 0; i < comps.size(); ++i) {
            aug.add(comps[i]);
        }
        for (int i = 0; i < diodeCount_; ++i) {
            const ClipperDiode& cd = clipper_[static_cast<std::size_t>(i)];
            const double vAK = cd.orient * v;
            const DiodeSample s = cd.diode.evaluate(vAK);
            const double Geq = s.conductance;      // > 0 always (passive)
            const double Ieq = s.current - Geq * vAK;
            // Companion conductance as a Resistor (admittance() == 1/R == Geq).
            aug.add(Resistor{cd.diode.anode, cd.diode.cathode, 1.0 / Geq});
            // Companion current source: branch current a->b is Geq*(vA-vB)+Ieq,
            // so the constant Ieq moves to the RHS as -Ieq at the anode /
            // +Ieq at the cathode. CurrentSource(+p/-n) with I = -Ieq does that.
            aug.add(CurrentSource{cd.diode.anode, cd.diode.cathode, -Ieq});
        }
        return aug;
    }

    // Apply junction voltage limiting for the whole clipper. For each diode we
    // map the port voltage into that diode's frame (vAK = orient*v), let the
    // diode clamp its own step, and map the result back. Applying the diodes in
    // sequence limits whichever side of an antiparallel pair is conducting (the
    // limiter is a no-op on the reverse-biased side).
    double limitStep(double vRaw, double vOld) const noexcept {
        double vLim = vRaw;
        for (int i = 0; i < diodeCount_; ++i) {
            const ClipperDiode& cd = clipper_[static_cast<std::size_t>(i)];
            const double vakOld = cd.orient * vOld;
            const double vakNew = cd.orient * vLim;
            const double limited =
                cd.diode.limitJunctionVoltage(vakNew, vakOld);
            vLim = cd.orient * limited;
        }
        return vLim;
    }

    // Total true (Shockley) diode current across the port at guess `v`,
    // summing the antiparallel pair. Used only for the current-residual report.
    double totalDiodeCurrent(double v) const noexcept {
        double total = 0.0;
        for (int i = 0; i < diodeCount_; ++i) {
            const ClipperDiode& cd = clipper_[static_cast<std::size_t>(i)];
            total += cd.diode.evaluate(cd.orient * v).current;
        }
        return total;
    }

    // ---- fixed-capacity state (NO heap) -----------------------------------

    LinearSolver<MaxNodes, MaxComponents + kMaxCompanionComponents> linear_{};

    int maxIterations_;
    double voltageTol_;
    double currentTol_;

    // Warm-start guess for the port voltage, carried across timesteps.
    double warmStart_ = 0.0;

    // The clipper's port node pair and its (1 or 2) diodes, established by
    // collectClipper() each solve().
    NodeId portP_ = kGround;
    NodeId portN_ = kGround;
    std::array<ClipperDiode, static_cast<std::size_t>(kMaxDiodes)> clipper_{};
    int diodeCount_ = 0;
};

}  // namespace acfx::labs::component_abstractions
