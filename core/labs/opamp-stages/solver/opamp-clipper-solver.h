#pragma once

#include "labs/opamp-stages/solver/opamp-stage-solver.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/resistor.h"
#include "primitives/circuit/models/capacitor.h"
#include "primitives/circuit/models/inductor.h"
#include "primitives/circuit/models/sources.h"
#include "primitives/circuit/models/opamp.h"
#include "primitives/circuit/models/companion.h"
#include "primitives/circuit/node.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <variant>

// OpAmpClipperSolver — the bounded op-amp feedback-diode clipper solver for the
// opamp-stages lab (US2; contracts/opamp-stage-solver.md mode 3; research
// R6/R7/R8; FR-013..018). It advances an assembled op-amp+diode-clipper netlist
// (the TS808 core: Rin, an OpAmp, and a feedback network of Rf || Cf ||
// antiparallel diode string) one backward-Euler timestep and resolves the diode
// nonlinearity by a bounded, voltage-limited Newton iteration.
//
// THE COUPLING (research R8): this reuses the diode-clippers TransientClipper's
// SEPARATED timestep/Newton structure UNCHANGED in shape — the ONLY difference
// is that the linear system each Newton iteration solves is now the BORDERED
// (nullor-augmented) system (NullorSolver, opamp-stage-solver.h) rather than a
// plain nodal one:
//
//   - Companions ONCE per timestep: each reactive element (the feedback Cf) is
//     replaced, once per step, by its FIXED backward-Euler companion (a Resistor
//     + a CurrentSource computed from solver-held history), so the inner solver
//     never sees a Capacitor/Inductor and its own per-solve history advance is a
//     no-op. The clipper owns the real reactive history and advances it exactly
//     once, after Newton converges.
//   - Inner Newton HOLDS the companions fixed and only re-linearizes the diode
//     string: each diode becomes its Norton companion (Resistor + CurrentSource)
//     at the current port-voltage guess v (via Diode::evaluate).
//   - Each iteration solves the BORDERED system (the OpAmp's nullor rows/columns
//     are included — the NullorSolver borders the op-amp exactly), reads the new
//     port voltage v = V(portP) - V(portN), pnjlim-damps it per diode
//     (Diode::limitJunctionVoltage), and tests |Δv| < voltageTol.
//   - On convergence, warm-start + reactive history advance EXACTLY ONCE.
//
// NON-NORMATIVE, PHASE-5-SUPERSEDED SCAFFOLDING. LAB code, host-only, C++20 OK.
// It must NEVER grow into general MNA / gmin / a multi-nonlinearity engine: the
// same three bounded-charter tripwires as the linear solver apply. Tripwire (ii)
// — the single-nonlinearity-location refusal — is enforced HERE by the port
// scan: >= 2 interacting nonlinearities at distinct node pairs is a descriptive
// out-of-scope throw (deferred to Phase 5), exactly as TransientClipper refuses.
//
// NO FALLBACK / NO FABRICATION (FR-014): non-convergence within the iteration
// bound returns a NewtonStatus with converged == false plus the last iterate and
// residuals — never a substituted or fabricated output. A non-converged step
// leaves solver state UNCHANGED (the failed iterate never contaminates the next
// sample's warm-start or the reactive history). dt <= 0 and a singular bordered
// system are descriptive throws (via NullorSolver). double throughout.
//
// NO HEAP ON step() (FR-011): the augmented netlist is a fixed-capacity
// Netlist<MaxNodes, MaxComponents + 2*MaxDiodes> on the stack; the nested
// NullorSolver allocates nothing; all history is std::array.

namespace acfx::labs::opamp_stages {

// Per-sample convergence report (FR-014). converged == false is a legitimate,
// surfaced result: the node voltages are then the last (non-converged) iterate
// and must not be trusted as a physical answer.
struct NewtonStatus {
    bool converged = false;         // did |Δv| fall below the voltage tolerance?
    int iterations = 0;             // Newton iterations actually consumed (<= N)
    double voltageResidual = 0.0;   // final |v_{k+1} - v_k| across the port (V)
    double currentResidual = 0.0;   // final |ΔI| of the total diode current (A)
};

template <int MaxNodes, int MaxComponents, int MaxDiodes = 4, int MaxOpAmps = 1>
class OpAmpClipperSolver {
public:
    static_assert(MaxNodes > 0, "OpAmpClipperSolver requires MaxNodes >= 1 (ground)");
    static_assert(MaxComponents > 0, "OpAmpClipperSolver requires MaxComponents >= 1");
    static_assert(MaxDiodes > 0, "OpAmpClipperSolver requires MaxDiodes >= 1");
    static_assert(MaxOpAmps > 0, "OpAmpClipperSolver requires MaxOpAmps >= 1");

    // Convergence gates on the VOLTAGE residual only: |Δv| < voltageTol
    // (research R8, matching the diode-clippers defaults 50 / 1e-9 / 1e-12). The
    // current tolerance is NOT a gate — a diode's reverse-saturation current
    // residual can sit above any fixed currentTol even at a fully settled
    // voltage, so a current gate would spuriously reject a converged solve; it is
    // validated > 0 and reported alongside currentResidual for a caller to
    // inspect. Never silently retune N / voltageTol to hide a non-converging
    // case (FR-014).
    explicit OpAmpClipperSolver(int maxIterations = 50,
                                double voltageTol = 1e-9,
                                double currentTol = 1e-12)
        : maxIterations_(maxIterations),
          voltageTol_(voltageTol),
          currentTol_(currentTol) {
        if (maxIterations_ < 1) {
            throw std::invalid_argument(
                "opamp-clipper-solver: iteration bound N must be >= 1");
        }
        if (voltageTol_ <= 0.0 || currentTol_ <= 0.0) {
            throw std::invalid_argument(
                "opamp-clipper-solver: voltage/current tolerances must be > 0");
        }
        reset();
    }

    // Return to a cold circuit at rest: clear reactive history, the warm-start
    // port voltage, and the underlying bordered solver's state.
    void reset() noexcept {
        warmStart_ = 0.0;
        portP_ = kGround;
        portN_ = kGround;
        diodeCount_ = 0;
        reactiveCount_ = 0;
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
    // interacting nonlinearity at a distinct node pair (tripwire ii), a diode
    // population past MaxDiodes, an augmented-capacity overflow, or a singular
    // bordered system (via NullorSolver).
    NewtonStatus step(const Netlist<MaxNodes, MaxComponents>& nl, double dt) {
        if (!(dt > 0.0)) {
            throw std::invalid_argument(
                "opamp-clipper-solver: timestep dt must be > 0");
        }

        collectPort(nl);  // records the single port + its diodes; validates scope

        // Augmented-capacity guard (builder-output scope). buildAugmented
        // replaces each reactive element AND each diode with a 2-component Norton
        // companion, so the augmented count is componentCount + reactiveCount +
        // diodeCount. The AugNetlist capacity MaxComponents + 2*MaxDiodes is
        // sized for the op-amp-clipper builder output (a single Cf + up to
        // MaxDiodes feedback diodes) — provably within capacity there. A
        // reactive-heavy netlist could exceed it; fail loud HERE with a
        // descriptive message rather than let a later Netlist::add surface the
        // generic capacity error.
        constexpr int kAugCapacity = MaxComponents + 2 * MaxDiodes;
        const int projectedAug = nl.componentCount() + reactiveCount_ + diodeCount_;
        if (projectedAug > kAugCapacity) {
            throw std::runtime_error(
                "opamp-clipper-solver: augmented netlist would exceed capacity "
                "(componentCount + reactiveCount + diodeCount = " +
                std::to_string(projectedAug) + " > MaxComponents + 2*MaxDiodes = " +
                std::to_string(kAugCapacity) +
                ") — each reactive element and diode expands to a 2-component "
                "companion. This solver is sized for the op-amp-clipper builder "
                "output (one Cf + <= MaxDiodes feedback diodes); a reactive-heavy "
                "netlist needs a larger MaxDiodes or the Phase-5 general engine.");
        }

        double v = warmStart_;
        NewtonStatus status;
        double prevCurrent = totalDiodeCurrent(v);

        for (int iter = 1; iter <= maxIterations_; ++iter) {
            // Build the purely-linear augmented system: linear originals (incl.
            // the OpAmp) verbatim, reactive elements replaced by their FIXED
            // per-timestep companion, the diode string linearized at guess v.
            // NullorSolver then borders the OpAmp and solves the bordered system.
            const AugNetlist aug = buildAugmented(nl, v, dt);
            linear_.solve(aug, dt);

            if (diodeCount_ == 0) {
                // Pure linear + reactive network (no nonlinearity to iterate):
                // one bordered solve is exact.
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

        // Advance warm-start + reactive history EXACTLY ONCE, and ONLY on
        // convergence. A non-converged step leaves solver state UNCHANGED — the
        // failed iterate never contaminates the next sample's warm-start guess or
        // the reactive companion history (no hidden fallback, FR-014).
        if (status.converged) {
            warmStart_ = v;
            advanceHistory(nl, dt);
        }
        return status;
    }

    // Node voltage of the final iterate (ground == 0). Physically meaningful only
    // when the matching step() returned converged == true.
    double voltage(NodeId node) const { return linear_.voltage(node); }

    // Converged voltage across the clipper (feedback diode) port,
    // V(portP) - V(portN).
    double clipperVoltage() const {
        return linear_.voltage(portP_) - linear_.voltage(portN_);
    }

    int maxIterations() const noexcept { return maxIterations_; }
    double voltageTolerance() const noexcept { return voltageTol_; }
    double currentTolerance() const noexcept { return currentTol_; }

private:
    // The augmented, fully-linear netlist handed to NullorSolver: each reactive
    // element AND each diode is replaced by a Resistor + CurrentSource Norton
    // companion (2 slots each); linear elements (incl. the OpAmp) stay 1:1. The
    // capacity MaxComponents + 2*MaxDiodes covers the op-amp-clipper builder
    // output (one Cf + <= MaxDiodes feedback diodes) with headroom.
    using AugNetlist = Netlist<MaxNodes, MaxComponents + 2 * MaxDiodes>;

    // One diode of the clipper string with its orientation relative to the port
    // variable v = V(portP) - V(portN): orient == +1 when the diode's anode is
    // portP (vAK == v), -1 when reversed (vAK == -v).
    struct PortDiode {
        Diode diode;
        double orient;
    };

    // Scan the netlist, establish the SINGLE clipper port (the diode-string node
    // pair — the feedback (out, inMinus) pair), and validate the bounded
    // single-nonlinearity-location scope (tripwire ii). A diode-free netlist is
    // allowed: diodeCount_ == 0, no port.
    void collectPort(const Netlist<MaxNodes, MaxComponents>& nl) {
        diodeCount_ = 0;
        reactiveCount_ = 0;
        portP_ = kGround;
        portN_ = kGround;
        bool portSet = false;

        const auto comps = nl.components();
        for (std::size_t i = 0; i < comps.size(); ++i) {
            if (isReactive(comps[i])) {
                ++reactiveCount_;  // each expands to a 2-component companion below
            }
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
                throw std::runtime_error(
                    "opamp-clipper-solver: a second nonlinearity at a distinct "
                    "node pair is out of the bounded single-port scope "
                    "(bounded-charter tripwire ii) — deferred to Phase 5");
            }
            if (diodeCount_ >= MaxDiodes) {
                throw std::runtime_error(
                    "opamp-clipper-solver: diode population exceeds MaxDiodes — "
                    "out of bounded scope (deferred to Phase 5)");
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
    // originals (Resistor / VoltageSource / CurrentSource / OpAmp) are copied
    // verbatim — the OpAmp passes through and NullorSolver borders it; each
    // reactive element is replaced by its FIXED backward-Euler companion (from
    // held history — the SAME value every Newton iteration of this timestep);
    // each diode is linearized at v into its Norton companion.
    AugNetlist buildAugmented(const Netlist<MaxNodes, MaxComponents>& nl,
                              double v, double dt) const {
        AugNetlist aug;
        for (int k = 1; k < nl.nodeCount(); ++k) {
            aug.addNode();  // reproduce the node numbering (node k -> node k)
        }

        const auto comps = nl.components();
        for (std::size_t i = 0; i < comps.size(); ++i) {
            const Component& c = comps[i];
            if (const auto* cap = std::get_if<Capacitor>(&c)) {
                const double vPrev = prevNodeVoltage(cap->a) - prevNodeVoltage(cap->b);
                const Companion comp = cap->companion(dt, vPrev);
                aug.add(Resistor{cap->a, cap->b, 1.0 / comp.Geq});
                aug.add(CurrentSource{cap->a, cap->b, comp.Ieq});
            } else if (const auto* ind = std::get_if<Inductor>(&c)) {
                const double iPrev = inductorCurrent_[i];
                const Companion comp = ind->companion(dt, iPrev);
                aug.add(Resistor{ind->a, ind->b, 1.0 / comp.Geq});
                aug.add(CurrentSource{ind->a, ind->b, comp.Ieq});
            } else if (std::holds_alternative<Diode>(c)) {
                // Diode: stripped here; its companion is appended below at v.
                continue;
            } else {
                aug.add(c);  // linear element (incl. OpAmp) — verbatim
            }
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
    // voltages (owned here because the reactive elements were stripped from the
    // netlist the NullorSolver saw).
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

    NullorSolver<MaxNodes, MaxComponents + 2 * MaxDiodes, MaxOpAmps> linear_{};

    int maxIterations_;
    double voltageTol_;
    double currentTol_;

    double warmStart_ = 0.0;  // previous converged port voltage (Newton warm start)

    NodeId portP_ = kGround;
    NodeId portN_ = kGround;
    std::array<PortDiode, static_cast<std::size_t>(MaxDiodes)> diodes_{};
    int diodeCount_ = 0;
    int reactiveCount_ = 0;  // reactive elements in the last-seen netlist (each → 2 aug slots)

    // Reactive companion history owned by this solver (loop-separation):
    //   prevNodeVoltage_ — previous solved node voltages (capacitor vPrev source).
    //   inductorCurrent_ — per-component previous inductor branch currents.
    std::array<double, static_cast<std::size_t>(MaxNodes)> prevNodeVoltage_{};
    std::array<double, static_cast<std::size_t>(MaxComponents)> inductorCurrent_{};
};

}  // namespace acfx::labs::opamp_stages
