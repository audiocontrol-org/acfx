#pragma once

#include "primitives/circuit/netlist.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/node.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <variant>

// LinearSolver — the deliberately-naive LINEAR reference solver for the
// component-abstractions lab (contract reference-solver.md §Linear solve;
// research.md R1 fixed-node reduction, R3 backward-Euler companions; FR-013).
//
// NON-NORMATIVE, PHASE-5-SUPERSEDED SCAFFOLDING. This lives in the LAB, not the
// circuit primitive, and it exists only to make the component vocabulary
// runnable before real MNA exists. It solves the reduced nodal system by
// fixed-size Gaussian elimination with partial pivoting. Phase 5 (Modified
// Nodal Analysis + Newton + implicit integration) supersedes it entirely; this
// solver must never grow into MNA (design D3, OQ2). Do not depend on it as a
// production circuit engine.
//
// THE SEAM (FR-006): the solver ASSEMBLES; each COMPONENT SUPPLIES ITS PHYSICS.
// The solver reads Resistor.admittance(), Capacitor/Inductor.companion(dt, ...),
// and CurrentSource.current(); it never re-derives a component's constitutive
// law. Ideal VoltageSources are imposed structurally by fixed-node reduction
// (R1) — NOT a gmin/large-conductance fallback and NOT MNA branch-current
// augmentation.
//
// STATE OWNERSHIP (R3): reactive companions need history, and the component
// primitives are pure values that hold none — so this solver owns it. It keeps
// the previous solved node voltages (for capacitor companions) and the previous
// inductor branch currents (for inductor companions), advancing them after each
// step. This is exactly the "solver owns the history" resolution from R3.
//
// NO HEAP IN THE SOLVE (FR-013/011): every buffer is a std::array sized by the
// Netlist template parameters. There is no new/delete, no std::vector, and no
// allocation anywhere in solve(). Descriptive std::exceptions are thrown for
// unsupported topology (floating ideal source) or a singular reduced system —
// never a silent wrong answer or a fabricated fallback (repo standard).
//
// double throughout (FR-022). C++17. This is HOST-ONLY lab code (it is not
// required to be platform-independent), but it stays standard-library-only and
// includes no platform or harness headers.

namespace acfx::labs::component_abstractions {

template <int MaxNodes, int MaxComponents>
class LinearSolver {
public:
    static_assert(MaxNodes > 0, "LinearSolver requires MaxNodes >= 1 (ground)");
    static_assert(MaxComponents > 0, "LinearSolver requires MaxComponents >= 1");

    LinearSolver() { reset(); }

    // Clear all history: previous node voltages, previous inductor currents,
    // and the last solved node voltages all return to zero (a cold circuit at
    // rest). Call between independent transient runs.
    void reset() noexcept {
        vPrevNode_.fill(0.0);
        voltage_.fill(0.0);
        inductorCurrent_.fill(0.0);
    }

    // Advance the circuit one backward-Euler timestep of length `dt` and solve
    // for the node voltages. Reads every component's physics from `nl`, assembles
    // the reduced conductance system G'·v = i', imposes grounded ideal voltage
    // sources by fixed-node reduction, and solves by Gaussian elimination with
    // partial pivoting. On return, voltage(node) exposes the solved node voltages
    // and the reactive history is advanced for the next step.
    //
    // Preconditions: `nl` is a prepared (validated) netlist and `dt > 0`.
    // Throws std::invalid_argument on dt <= 0, std::runtime_error on a singular
    // reduced system, and std::runtime_error on a floating (non-grounded) ideal
    // voltage source (unsupported by this naive solver — see below).
    void solve(const Netlist<MaxNodes, MaxComponents>& nl, double dt) {
        if (!(dt > 0.0)) {
            throw std::invalid_argument(
                "linear-solver: timestep dt must be > 0");
        }

        const int nodeCount = nl.nodeCount();
        // Number of unknown (non-ground) node rows; node k maps to local k-1.
        const int n = nodeCount - 1;

        zeroSystem(n);
        stampComponents(nl, dt, n);
        imposeVoltageSources(nl, n);
        gaussianSolve(n);
        advanceHistory(nl, dt, nodeCount);
    }

    // Solved voltage at `node` (ground == 0 is exactly 0.0). Valid after solve().
    double voltage(NodeId node) const {
        if (node == kGround) {
            return 0.0;
        }
        if (node < 0 || node >= MaxNodes) {
            throw std::out_of_range(
                "linear-solver: node id out of range in voltage()");
        }
        return voltage_[static_cast<std::size_t>(node)];
    }

    // Previous-step branch current stored for the inductor at component index
    // `componentIndex` (backward-Euler companion history). Exposed mainly for
    // tests/inspection; zero for any non-inductor slot.
    double inductorCurrent(int componentIndex) const {
        if (componentIndex < 0 || componentIndex >= MaxComponents) {
            throw std::out_of_range(
                "linear-solver: component index out of range");
        }
        return inductorCurrent_[static_cast<std::size_t>(componentIndex)];
    }

private:
    // ---- assembly ---------------------------------------------------------

    // Zero the working conductance matrix and RHS over the active n x n block.
    void zeroSystem(int n) noexcept {
        for (int r = 0; r < n; ++r) {
            rhs_[static_cast<std::size_t>(r)] = 0.0;
            auto& row = g_[static_cast<std::size_t>(r)];
            for (int c = 0; c < n; ++c) {
                row[static_cast<std::size_t>(c)] = 0.0;
            }
        }
    }

    // Local (0-based, ground-excluded) matrix index for a node, or -1 for
    // ground. Node k (k >= 1) lives at local row/col k-1.
    static constexpr int local(NodeId node) noexcept {
        return isGround(node) ? -1 : (node - 1);
    }

    // Standard nodal conductance stamp of a scalar G between nodes a and b.
    // Ground (local == -1) contributes no row/col (node 0 is the reference).
    void stampConductance(NodeId a, NodeId b, double G) noexcept {
        const int la = local(a);
        const int lb = local(b);
        if (la >= 0) {
            g_[static_cast<std::size_t>(la)][static_cast<std::size_t>(la)] += G;
        }
        if (lb >= 0) {
            g_[static_cast<std::size_t>(lb)][static_cast<std::size_t>(lb)] += G;
        }
        if (la >= 0 && lb >= 0) {
            g_[static_cast<std::size_t>(la)][static_cast<std::size_t>(lb)] -= G;
            g_[static_cast<std::size_t>(lb)][static_cast<std::size_t>(la)] -= G;
        }
    }

    // Inject a current `I` into the RHS following the +a / -b convention:
    // +I into node a, -I into node b. Ground rows are dropped.
    void injectCurrent(NodeId a, NodeId b, double I) noexcept {
        const int la = local(a);
        const int lb = local(b);
        if (la >= 0) {
            rhs_[static_cast<std::size_t>(la)] += I;
        }
        if (lb >= 0) {
            rhs_[static_cast<std::size_t>(lb)] -= I;
        }
    }

    // Stamp every component by reading ITS OWN physics. VoltageSources are NOT
    // stamped here — they are imposed structurally afterward (fixed-node
    // reduction). Diodes are NOT stamped: nonlinearity is the Newton layer's
    // job (T017 newton-clipper), which drives its own linearized companion; a
    // pure linear solve does not touch the diode.
    void stampComponents(const Netlist<MaxNodes, MaxComponents>& nl, double dt,
                         int n) noexcept {
        (void)n;
        const auto comps = nl.components();
        for (std::size_t i = 0; i < comps.size(); ++i) {
            const Component& c = comps[i];
            if (const auto* r = std::get_if<Resistor>(&c)) {
                // Resistor: pure conductance G = 1/R (admittance()).
                stampConductance(r->a, r->b, r->admittance());
            } else if (const auto* cap = std::get_if<Capacitor>(&c)) {
                // Capacitor backward-Euler Norton companion (R3): the branch
                // current from a to b is i = Geq*(v_a - v_b) - Ieq, so Geq is a
                // conductance and the history term moves to the RHS as +Ieq at a
                // / -Ieq at b. vPrev is the previous across-cap voltage, which
                // this solver owns.
                const double vPrev = nodePrev(cap->a) - nodePrev(cap->b);
                const Companion comp = cap->companion(dt, vPrev);
                stampConductance(cap->a, cap->b, comp.Geq);
                injectCurrent(cap->a, cap->b, comp.Ieq);
            } else if (const auto* l = std::get_if<Inductor>(&c)) {
                // Inductor backward-Euler Norton companion (R3), the dual of the
                // capacitor: i = Geq*(v_a - v_b) - Ieq with Ieq = -iPrev, so the
                // same conductance + RHS stamp shape applies. iPrev is the stored
                // branch current for THIS component slot.
                const double iPrev =
                    inductorCurrent_[static_cast<std::size_t>(i)];
                const Companion comp = l->companion(dt, iPrev);
                stampConductance(l->a, l->b, comp.Geq);
                injectCurrent(l->a, l->b, comp.Ieq);
            } else if (const auto* cs = std::get_if<CurrentSource>(&c)) {
                // Current source: a pure RHS contribution, +I at p / -I at n.
                injectCurrent(cs->p, cs->n, cs->current());
            }
            // VoltageSource: handled by imposeVoltageSources() (fixed-node
            // reduction). Diode: intentionally skipped (Newton layer, T017).
        }
    }

    // Impose every grounded ideal voltage source by fixed-node reduction (R1):
    // a node pinned to a KNOWN voltage V has its column multiplied out into the
    // RHS of the other equations, and its own row replaced by v[p] = V exactly.
    // No gmin, no MNA branch augmentation.
    //
    // v1 validation circuits drive from a single grounded source, so the source's
    // negative terminal is ground and node p is pinned to V. A FLOATING
    // (non-grounded) ideal source is NOT supported by this naive solver — we
    // throw rather than silently return a wrong answer.
    void imposeVoltageSources(const Netlist<MaxNodes, MaxComponents>& nl,
                              int n) {
        const auto comps = nl.components();
        for (std::size_t i = 0; i < comps.size(); ++i) {
            const auto* v = std::get_if<VoltageSource>(&comps[i]);
            if (v == nullptr) {
                continue;
            }
            const bool pGround = isGround(v->p);
            const bool nGround = isGround(v->n);
            if (!pGround && !nGround) {
                throw std::runtime_error(
                    "linear-solver: floating ideal voltage source not supported "
                    "by the reference solver (grounded sources only)");
            }
            // Normalize so that `pinned` is the non-ground node and `value` is
            // the voltage it is pinned to. If n is ground: v[p] = V. If p is
            // ground: v[n] = -V (since v_p - v_n = V and v_p = 0).
            NodeId pinned;
            double value;
            if (nGround) {
                pinned = v->p;
                value = v->V;
            } else {
                pinned = v->n;
                value = -v->V;
            }
            const int lp = local(pinned);
            if (lp < 0) {
                // Both terminals ground — a degenerate short-to-ground source.
                // A prepared netlist should not present this; treat as ill-posed.
                throw std::runtime_error(
                    "linear-solver: voltage source pins the ground node");
            }
            pinNode(lp, value, n);
        }
    }

    // Fixed-node reduction of the single known node `lp` (local index) to `value`
    // over the active n x n system: move column lp into the RHS of every other
    // row, then overwrite row lp with the trivial equation v[lp] = value.
    void pinNode(int lp, double value, int n) noexcept {
        for (int r = 0; r < n; ++r) {
            if (r == lp) {
                continue;
            }
            auto& row = g_[static_cast<std::size_t>(r)];
            rhs_[static_cast<std::size_t>(r)] -=
                row[static_cast<std::size_t>(lp)] * value;
            row[static_cast<std::size_t>(lp)] = 0.0;
        }
        auto& prow = g_[static_cast<std::size_t>(lp)];
        for (int c = 0; c < n; ++c) {
            prow[static_cast<std::size_t>(c)] = 0.0;
        }
        prow[static_cast<std::size_t>(lp)] = 1.0;
        rhs_[static_cast<std::size_t>(lp)] = value;
    }

    // ---- solve ------------------------------------------------------------

    // Solve the reduced system G·x = rhs in place by Gaussian elimination with
    // partial pivoting over the active n x n block. Writes the solution into the
    // node-indexed voltage_ vector (local row r -> node r+1); ground stays 0.
    // A near-zero pivot means a singular system (a well-posed, prepared circuit
    // should never hit this) and is a hard, descriptive error.
    void gaussianSolve(int n) {
        // Ground is always exactly 0.
        voltage_.fill(0.0);
        if (n <= 0) {
            return;
        }

        constexpr double kPivotEps = 1e-300;

        for (int col = 0; col < n; ++col) {
            // Partial pivot: pick the row (>= col) with the largest |entry|.
            int pivotRow = col;
            double pivotMag =
                std::fabs(g_[static_cast<std::size_t>(col)]
                            [static_cast<std::size_t>(col)]);
            for (int r = col + 1; r < n; ++r) {
                const double mag =
                    std::fabs(g_[static_cast<std::size_t>(r)]
                                [static_cast<std::size_t>(col)]);
                if (mag > pivotMag) {
                    pivotMag = mag;
                    pivotRow = r;
                }
            }
            if (pivotMag < kPivotEps) {
                throw std::runtime_error(
                    "linear-solver: singular reduced system (zero pivot) — the "
                    "circuit is ill-posed for the reference solver");
            }
            if (pivotRow != col) {
                swapRows(col, pivotRow);
            }

            // Eliminate this column below the pivot.
            const double pivot =
                g_[static_cast<std::size_t>(col)][static_cast<std::size_t>(col)];
            for (int r = col + 1; r < n; ++r) {
                auto& rrow = g_[static_cast<std::size_t>(r)];
                const double factor =
                    rrow[static_cast<std::size_t>(col)] / pivot;
                if (factor == 0.0) {
                    continue;
                }
                rrow[static_cast<std::size_t>(col)] = 0.0;
                const auto& prow = g_[static_cast<std::size_t>(col)];
                for (int c = col + 1; c < n; ++c) {
                    rrow[static_cast<std::size_t>(c)] -=
                        factor * prow[static_cast<std::size_t>(c)];
                }
                rhs_[static_cast<std::size_t>(r)] -=
                    factor * rhs_[static_cast<std::size_t>(col)];
            }
        }

        // Back-substitution into local solution, then scatter to node indices.
        for (int r = n - 1; r >= 0; --r) {
            double sum = rhs_[static_cast<std::size_t>(r)];
            const auto& row = g_[static_cast<std::size_t>(r)];
            for (int c = r + 1; c < n; ++c) {
                sum -= row[static_cast<std::size_t>(c)] *
                       solution_[static_cast<std::size_t>(c)];
            }
            solution_[static_cast<std::size_t>(r)] =
                sum / row[static_cast<std::size_t>(r)];
        }
        for (int r = 0; r < n; ++r) {
            // Local row r corresponds to node r + 1.
            voltage_[static_cast<std::size_t>(r + 1)] =
                solution_[static_cast<std::size_t>(r)];
        }
    }

    // Swap two rows of both the matrix and the RHS.
    void swapRows(int a, int b) noexcept {
        auto& ra = g_[static_cast<std::size_t>(a)];
        auto& rb = g_[static_cast<std::size_t>(b)];
        for (std::size_t c = 0; c < static_cast<std::size_t>(MaxNodes); ++c) {
            const double t = ra[c];
            ra[c] = rb[c];
            rb[c] = t;
        }
        const double tr = rhs_[static_cast<std::size_t>(a)];
        rhs_[static_cast<std::size_t>(a)] = rhs_[static_cast<std::size_t>(b)];
        rhs_[static_cast<std::size_t>(b)] = tr;
    }

    // ---- history advance --------------------------------------------------

    // After the solve, advance the reactive history for the next step: store the
    // fresh node voltages as the capacitors' vPrev, and recompute each inductor's
    // branch current from the new node voltages (i^n = Geq*(v_a - v_b) + iPrev).
    void advanceHistory(const Netlist<MaxNodes, MaxComponents>& nl, double dt,
                        int nodeCount) noexcept {
        const auto comps = nl.components();
        for (std::size_t i = 0; i < comps.size(); ++i) {
            const auto* l = std::get_if<Inductor>(&comps[i]);
            if (l == nullptr) {
                continue;
            }
            const double iPrev = inductorCurrent_[i];
            const Companion comp = l->companion(dt, iPrev);
            const double vDiff = voltage(l->a) - voltage(l->b);
            // i^n = Geq*v^n + iPrev  (Ieq = -iPrev), the backward-Euler update.
            inductorCurrent_[i] = comp.Geq * vDiff + iPrev;
        }
        // Capacitor history: the previous across-cap voltage is read from the
        // node voltages, so store the whole solved node-voltage vector.
        for (int k = 0; k < nodeCount; ++k) {
            vPrevNode_[static_cast<std::size_t>(k)] =
                voltage_[static_cast<std::size_t>(k)];
        }
    }

    // Previous-step voltage at a node (ground == 0), used to form capacitor vPrev.
    double nodePrev(NodeId node) const noexcept {
        if (isGround(node)) {
            return 0.0;
        }
        return vPrevNode_[static_cast<std::size_t>(node)];
    }

    // ---- fixed-capacity state (NO heap) -----------------------------------

    // Working conductance matrix and RHS for the reduced system. Sized by
    // MaxNodes; only the active (nodeCount-1) leading block is ever used.
    std::array<std::array<double, MaxNodes>, MaxNodes> g_{};
    std::array<double, MaxNodes> rhs_{};
    std::array<double, MaxNodes> solution_{};

    // Node-indexed solved voltages (index 0 == ground == 0.0).
    std::array<double, MaxNodes> voltage_{};

    // Reactive companion history owned by the solver (R3):
    //   vPrevNode_   — previous solved node voltages (capacitor vPrev source).
    //   inductorCurrent_ — per-component previous inductor branch currents.
    std::array<double, MaxNodes> vPrevNode_{};
    std::array<double, MaxComponents> inductorCurrent_{};
};

}  // namespace acfx::labs::component_abstractions
