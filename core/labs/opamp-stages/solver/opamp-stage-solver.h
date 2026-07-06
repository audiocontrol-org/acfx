#pragma once

#include "primitives/circuit/netlist.h"
#include "primitives/circuit/components.h"
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
#include <string>
#include <variant>

// NullorSolver — the bounded NULLOR MNA-augmentation reference solver for the
// opamp-stages lab (US2; contracts/opamp-stage-solver.md; research R2/R5/R6/R8;
// FR-011..018). It advances an assembled op-amp-stage netlist one backward-Euler
// timestep and solves for the node voltages by BORDERING the reduced nodal
// system with one row + one column per OpAmp — the classic MNA nullor stamp:
//
//     [ G   B ] [ v ]   [ i ]
//     [ C   0 ] [ j ] = [ 0 ]
//
//   - G / i are the reduced nodal conductance matrix and RHS the component-
//     abstractions LinearSolver already assembles (resistors, capacitor/inductor
//     companions, current-source injections, fixed-node-reduced ideal sources).
//   - B  (one column per OpAmp) places +j into the KCL of the op-amp's `out`
//     node: the norator sources whatever output current the feedback network
//     demands, and that branch current j is a free unknown.
//   - C  (one row per OpAmp) imposes the nullator / virtual short
//     V(inPlus) - V(inMinus) = 0: +1 at the inPlus column, -1 at the inMinus
//     column, RHS 0 (a grounded input terminal is on the dropped ground column,
//     exactly as grounded voltage-source terminals are). The 0 block is the empty
//     coupling between the constraint row and the branch-current column.
//
// The bordered system is solved by fixed-size Gaussian elimination with partial
// pivoting (the constraint rows carry a zero diagonal, so pivoting is load-
// bearing). This realizes the op-amp EXACTLY as a nullor — never as a large-but-
// finite-gain VCVS or large-conductance divider (the forbidden gmin fallback, R4).
//
// NON-NORMATIVE, PHASE-5-SUPERSEDED LAB scaffolding; host-only, C++20 OK. It
// borrows the LinearSolver assembly approach but is a SEPARATE type (that solver
// forbids MNA branch growth, so the augmentation lives here and it is untouched).
// It must never grow into general MNA / gmin / a multi-nonlinearity engine; three
// bounded-charter tripwires (research R6) make that a checkable property:
//   (i)   OpAmp-specific augmentation. ONLY OpAmp branches get a row/column;
//         VoltageSource stays fixed-node reduction, R/C/L/CurrentSource stay
//         nodal/companion. A FLOATING (non-grounded) ideal voltage source — the
//         canonical non-OpAmp element that general MNA would branch-augment — is
//         a descriptive throw (the "becoming general MNA" signal).
//   (ii)  Single-nonlinearity-location refusal. A raw Diode in this LINEAR path
//         is a descriptive throw: the clipper (opamp-clipper-solver.h) linearizes
//         each diode into a Norton companion BEFORE the bordered solve, so this
//         solver never sees one (the >=2-nonlinearity refusal is the clipper's).
//   (iii) One row/column per op-amp, sized at instantiation: the augmented
//         dimension is a compile-time function of MaxNodes + MaxOpAmps, no dynamic
//         growth. A population beyond MaxOpAmps is a descriptive throw.
//
// WELL-POSEDNESS AUTHORITY (R5): the authoritative gate is the non-singularity
// of the bordered system at solve time — a singular augmented matrix is a
// descriptive throw; the nodal contributesConductivePath scan is only a
// conservative pre-filter.
//
// SEAM (FR-006): the solver ASSEMBLES; each component supplies its physics
// (Resistor.admittance(), Cap/Inductor.companion(), CurrentSource.current(); the
// OpAmp constraint imposed structurally). STATE (R3/R8): the solver owns reactive
// history (capacitor vPrev, inductor branch currents), advanced EXACTLY ONCE per
// solve() — that single advance is what makes the active first-order stage (Cf
// companion; FR-012) correct. NO HEAP (FR-011/013): every buffer is a std::array
// sized by the template parameters; dt <= 0 and a singular system are descriptive
// throws, never a fabricated fallback. double throughout (FR-014).

namespace acfx::labs::opamp_stages {

// MaxOpAmps is the compile-time op-amp capacity; the augmented dimension is
// MaxNodes + MaxOpAmps (tripwire iii), fixed at instantiation.
template <int MaxNodes, int MaxComponents, int MaxOpAmps = 1>
class NullorSolver {
public:
    static_assert(MaxNodes > 0, "NullorSolver requires MaxNodes >= 1 (ground)");
    static_assert(MaxComponents > 0, "NullorSolver requires MaxComponents >= 1");
    static_assert(MaxOpAmps > 0, "NullorSolver requires MaxOpAmps >= 1");

    NullorSolver() { reset(); }

    // Clear all history and the last solve (a cold circuit at rest). Call
    // between independent transient runs.
    void reset() noexcept {
        vPrevNode_.fill(0.0);
        voltage_.fill(0.0);
        inductorCurrent_.fill(0.0);
        branchCurrent_.fill(0.0);
    }

    // Advance one backward-Euler timestep of length dt and solve the bordered
    // system: assemble the reduced nodal system (reading each component's
    // physics), border one row/column per OpAmp, impose grounded ideal voltage
    // sources by fixed-node reduction, solve by Gaussian elimination with partial
    // pivoting, advance reactive history once. On return voltage(node) and
    // branchCurrent(opampIndex) expose the solution.
    //
    // Throws std::invalid_argument on dt <= 0; std::runtime_error on an op-amp
    // population beyond MaxOpAmps (tripwire iii), a floating ideal voltage source
    // (tripwire i), a raw Diode (tripwire ii), or a singular bordered system (R5).
    void solve(const Netlist<MaxNodes, MaxComponents>& nl, double dt) {
        if (!(dt > 0.0)) {
            throw std::invalid_argument(
                "opamp-stage-solver: timestep dt must be > 0");
        }

        const int nodeCount = nl.nodeCount();
        const int n = nodeCount - 1;   // reduced node unknowns (node k -> k-1)
        const int numOpAmps = countOpAmps(nl);
        const int N = n + numOpAmps;   // bordered dimension

        zeroSystem(N);
        stampComponents(nl, dt);
        borderOpAmps(nl, n);           // B columns + C constraint rows
        imposeVoltageSources(nl, N);   // fixed-node reduction over the full system
        gaussianSolve(N, n);
        advanceHistory(nl, dt, nodeCount);
    }

    // Solved voltage at `node` (ground == 0 is exactly 0.0). Valid after solve().
    double voltage(NodeId node) const {
        if (node == kGround) {
            return 0.0;
        }
        if (node < 0 || node >= MaxNodes) {
            throw std::out_of_range(
                "opamp-stage-solver: node id out of range in voltage()");
        }
        return voltage_[static_cast<std::size_t>(node)];
    }

    // Solved norator output branch current for the op-amp at augmentation index
    // `opampIndex` (netlist order). Exposed for tests/inspection; valid after
    // solve().
    double branchCurrent(int opampIndex) const {
        if (opampIndex < 0 || opampIndex >= MaxOpAmps) {
            throw std::out_of_range(
                "opamp-stage-solver: op-amp index out of range");
        }
        return branchCurrent_[static_cast<std::size_t>(opampIndex)];
    }

private:
    // Augmented (bordered) dimension ceiling — MaxNodes node slots (index 0 is
    // ground, over-provisioning by one) plus MaxOpAmps branch slots. Fixed at
    // instantiation (tripwire iii).
    static constexpr std::size_t kDim =
        static_cast<std::size_t>(MaxNodes) + static_cast<std::size_t>(MaxOpAmps);
    // Count OpAmp components and enforce the compile-time capacity (tripwire
    // iii): a population beyond MaxOpAmps would need a matrix wider than the
    // instantiated arrays — exactly the "no dynamic growth" bound.
    int countOpAmps(const Netlist<MaxNodes, MaxComponents>& nl) const {
        const auto comps = nl.components();
        int count = 0;
        for (std::size_t i = 0; i < comps.size(); ++i) {
            if (std::holds_alternative<OpAmp>(comps[i])) {
                ++count;
            }
        }
        if (count > MaxOpAmps) {
            throw std::runtime_error(
                "opamp-stage-solver: op-amp population (" +
                std::to_string(count) + ") exceeds MaxOpAmps (" +
                std::to_string(MaxOpAmps) +
                ") — the bordered dimension is fixed at instantiation "
                "(bounded-charter tripwire iii, no dynamic growth)");
        }
        return count;
    }
    // Zero the working matrix and RHS over the active N x N bordered block.
    void zeroSystem(int N) noexcept {
        for (int r = 0; r < N; ++r) {
            rhs_[static_cast<std::size_t>(r)] = 0.0;
            auto& row = a_[static_cast<std::size_t>(r)];
            for (int c = 0; c < N; ++c) {
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
    void stampConductance(NodeId a, NodeId b, double G) noexcept {
        const int la = local(a);
        const int lb = local(b);
        if (la >= 0) {
            a_[static_cast<std::size_t>(la)][static_cast<std::size_t>(la)] += G;
        }
        if (lb >= 0) {
            a_[static_cast<std::size_t>(lb)][static_cast<std::size_t>(lb)] += G;
        }
        if (la >= 0 && lb >= 0) {
            a_[static_cast<std::size_t>(la)][static_cast<std::size_t>(lb)] -= G;
            a_[static_cast<std::size_t>(lb)][static_cast<std::size_t>(la)] -= G;
        }
    }

    // Inject a current I into the RHS: +I at node a, -I at node b (ground
    // dropped).
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

    // Stamp every component by reading ITS OWN physics into the G block.
    // VoltageSource (fixed-node reduction) and OpAmp (borderOpAmps) are handled
    // afterward. A raw Diode is out of scope (tripwire ii): the clipper linearizes
    // diodes before calling, so one here is a topology error, not silently skipped.
    void stampComponents(const Netlist<MaxNodes, MaxComponents>& nl,
                         double dt) {
        const auto comps = nl.components();
        for (std::size_t i = 0; i < comps.size(); ++i) {
            const Component& c = comps[i];
            if (const auto* r = std::get_if<Resistor>(&c)) {
                stampConductance(r->a, r->b, r->admittance());
            } else if (const auto* cap = std::get_if<Capacitor>(&c)) {
                const double vPrev = nodePrev(cap->a) - nodePrev(cap->b);
                const Companion comp = cap->companion(dt, vPrev);
                stampConductance(cap->a, cap->b, comp.Geq);
                injectCurrent(cap->a, cap->b, comp.Ieq);
            } else if (const auto* l = std::get_if<Inductor>(&c)) {
                const double iPrev =
                    inductorCurrent_[static_cast<std::size_t>(i)];
                const Companion comp = l->companion(dt, iPrev);
                stampConductance(l->a, l->b, comp.Geq);
                injectCurrent(l->a, l->b, comp.Ieq);
            } else if (const auto* cs = std::get_if<CurrentSource>(&c)) {
                injectCurrent(cs->p, cs->n, cs->current());
            } else if (std::holds_alternative<Diode>(c)) {
                throw std::runtime_error(
                    "opamp-stage-solver: a raw Diode reached the linear bordered "
                    "solve — the nonlinear clipper must linearize each diode into "
                    "a Norton companion before this solve (bounded-charter "
                    "tripwire ii; nonlinear coupling lives in opamp-clipper-"
                    "solver.h)");
            }
            // VoltageSource: imposeVoltageSources(). OpAmp: borderOpAmps().
        }
    }

    // Border per OpAmp (research R2; see the file header for the B/C stamp).
    // Op-amp #k (netlist order) owns branch column and constraint row index
    // (n + k); a grounded input terminal (local == -1) is on the dropped ground
    // column and simply omitted (that is how the inverting config's grounded
    // inPlus yields the virtual ground -V(inMinus) = 0).
    void borderOpAmps(const Netlist<MaxNodes, MaxComponents>& nl, int n) {
        const auto comps = nl.components();
        int k = 0;
        for (std::size_t i = 0; i < comps.size(); ++i) {
            const auto* op = std::get_if<OpAmp>(&comps[i]);
            if (op == nullptr) {
                continue;
            }
            const std::size_t branch = static_cast<std::size_t>(n + k);

            // B: +j into the KCL of `out`.
            const int lo = local(op->out);
            if (lo >= 0) {
                a_[static_cast<std::size_t>(lo)][branch] += 1.0;
            }

            // C: V(inPlus) - V(inMinus) = 0.
            const int lp = local(op->inPlus);
            const int lm = local(op->inMinus);
            if (lp >= 0) {
                a_[branch][static_cast<std::size_t>(lp)] += 1.0;
            }
            if (lm >= 0) {
                a_[branch][static_cast<std::size_t>(lm)] -= 1.0;
            }
            rhs_[branch] = 0.0;
            ++k;
        }
    }

    // Impose every grounded ideal voltage source by fixed-node reduction (R1):
    // pin the node to V (its own row becomes v[p] = V; its column is multiplied
    // out into every other row's RHS, INCLUDING the nullor constraint rows). A
    // FLOATING (non-grounded) source is the tripwire-i throw (see file header).
    void imposeVoltageSources(const Netlist<MaxNodes, MaxComponents>& nl,
                              int N) {
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
                    "opamp-stage-solver: a floating (non-grounded) ideal voltage "
                    "source would need branch-current augmentation — only OpAmp "
                    "branches are augmented here (bounded-charter tripwire i; "
                    "this is the 'becoming general MNA' signal)");
            }
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
                throw std::runtime_error(
                    "opamp-stage-solver: voltage source pins the ground node");
            }
            pinNode(lp, value, N);
        }
    }

    // Fixed-node reduction of node `lp` (local index) to `value` over the active
    // N x N system: move column lp into every other row's RHS (nodal AND nullor
    // constraint rows), then overwrite row lp with v[lp] = value. lp is always a
    // node column (< n) and branch columns are never pinned, so B/C stays intact.
    void pinNode(int lp, double value, int N) noexcept {
        for (int r = 0; r < N; ++r) {
            if (r == lp) {
                continue;
            }
            auto& row = a_[static_cast<std::size_t>(r)];
            rhs_[static_cast<std::size_t>(r)] -=
                row[static_cast<std::size_t>(lp)] * value;
            row[static_cast<std::size_t>(lp)] = 0.0;
        }
        auto& prow = a_[static_cast<std::size_t>(lp)];
        for (int c = 0; c < N; ++c) {
            prow[static_cast<std::size_t>(c)] = 0.0;
        }
        prow[static_cast<std::size_t>(lp)] = 1.0;
        rhs_[static_cast<std::size_t>(lp)] = value;
    }
    // Solve the bordered system A·x = rhs in place by Gaussian elimination with
    // partial pivoting over the active N x N block, then scatter: node rows
    // 0..n-1 -> node voltages 1..n; branch rows n..N-1 -> op-amp output currents.
    // Partial pivoting is load-bearing — the [C,0] block has a zero diagonal, so
    // a naive diagonal pivot would divide by zero on a well-posed nullor system.
    // A near-zero pivot after pivoting means the bordered matrix is singular: the
    // authoritative well-posedness gate (R5), a hard descriptive error.
    void gaussianSolve(int N, int n) {
        voltage_.fill(0.0);
        branchCurrent_.fill(0.0);
        if (N <= 0) {
            return;
        }

        constexpr double kPivotEps = 1e-300;

        for (int col = 0; col < N; ++col) {
            int pivotRow = col;
            double pivotMag =
                std::fabs(a_[static_cast<std::size_t>(col)]
                            [static_cast<std::size_t>(col)]);
            for (int r = col + 1; r < N; ++r) {
                const double mag =
                    std::fabs(a_[static_cast<std::size_t>(r)]
                                [static_cast<std::size_t>(col)]);
                if (mag > pivotMag) {
                    pivotMag = mag;
                    pivotRow = r;
                }
            }
            if (pivotMag < kPivotEps) {
                throw std::runtime_error(
                    "opamp-stage-solver: singular augmented (nullor-bordered) "
                    "system (zero pivot) — the op-amp stage is ill-posed (no "
                    "feedback path or a degenerate virtual-short constraint). "
                    "This is the authoritative well-posedness gate (research R5).");
            }
            if (pivotRow != col) {
                swapRows(col, pivotRow);
            }

            const double pivot =
                a_[static_cast<std::size_t>(col)][static_cast<std::size_t>(col)];
            for (int r = col + 1; r < N; ++r) {
                auto& rrow = a_[static_cast<std::size_t>(r)];
                const double factor =
                    rrow[static_cast<std::size_t>(col)] / pivot;
                if (factor == 0.0) {
                    continue;
                }
                rrow[static_cast<std::size_t>(col)] = 0.0;
                const auto& prow = a_[static_cast<std::size_t>(col)];
                for (int c = col + 1; c < N; ++c) {
                    rrow[static_cast<std::size_t>(c)] -=
                        factor * prow[static_cast<std::size_t>(c)];
                }
                rhs_[static_cast<std::size_t>(r)] -=
                    factor * rhs_[static_cast<std::size_t>(col)];
            }
        }

        for (int r = N - 1; r >= 0; --r) {
            double sum = rhs_[static_cast<std::size_t>(r)];
            const auto& row = a_[static_cast<std::size_t>(r)];
            for (int c = r + 1; c < N; ++c) {
                sum -= row[static_cast<std::size_t>(c)] *
                       solution_[static_cast<std::size_t>(c)];
            }
            solution_[static_cast<std::size_t>(r)] =
                sum / row[static_cast<std::size_t>(r)];
        }

        // Scatter: node rows -> node voltages, branch rows -> op-amp currents.
        for (int r = 0; r < n; ++r) {
            voltage_[static_cast<std::size_t>(r + 1)] =
                solution_[static_cast<std::size_t>(r)];
        }
        for (int k = 0; k < N - n; ++k) {
            branchCurrent_[static_cast<std::size_t>(k)] =
                solution_[static_cast<std::size_t>(n + k)];
        }
    }

    // Swap two rows of both the matrix and the RHS (over the full kDim width, so
    // no active-N bookkeeping is needed).
    void swapRows(int a, int b) noexcept {
        auto& ra = a_[static_cast<std::size_t>(a)];
        auto& rb = a_[static_cast<std::size_t>(b)];
        for (std::size_t c = 0; c < kDim; ++c) {
            const double t = ra[c];
            ra[c] = rb[c];
            rb[c] = t;
        }
        const double tr = rhs_[static_cast<std::size_t>(a)];
        rhs_[static_cast<std::size_t>(a)] = rhs_[static_cast<std::size_t>(b)];
        rhs_[static_cast<std::size_t>(b)] = tr;
    }
    // Advance reactive history for the next timestep EXACTLY ONCE (research
    // R3/R8): store the fresh node voltages as the capacitors' vPrev and
    // recompute each inductor's branch current. This single advance is what keeps
    // the active first-order stage correct (one bordered solve per step).
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
            inductorCurrent_[i] = comp.Geq * vDiff + iPrev;
        }
        for (int k = 0; k < nodeCount; ++k) {
            vPrevNode_[static_cast<std::size_t>(k)] =
                voltage_[static_cast<std::size_t>(k)];
        }
    }

    // Previous-step voltage at a node (ground == 0), the capacitor vPrev source.
    double nodePrev(NodeId node) const noexcept {
        if (isGround(node)) {
            return 0.0;
        }
        return vPrevNode_[static_cast<std::size_t>(node)];
    }
    // Bordered working matrix and RHS. Sized by kDim = MaxNodes + MaxOpAmps;
    // only the active leading N x N block is ever used.
    std::array<std::array<double, kDim>, kDim> a_{};
    std::array<double, kDim> rhs_{};
    std::array<double, kDim> solution_{};

    // Node-indexed solved voltages (index 0 == ground == 0.0) and solved norator
    // branch currents (one per op-amp, netlist order).
    std::array<double, static_cast<std::size_t>(MaxNodes)> voltage_{};
    std::array<double, static_cast<std::size_t>(MaxOpAmps)> branchCurrent_{};

    // Reactive companion history owned by the solver (research R3): previous
    // solved node voltages (capacitor vPrev) and per-component inductor currents.
    std::array<double, static_cast<std::size_t>(MaxNodes)> vPrevNode_{};
    std::array<double, static_cast<std::size_t>(MaxComponents)> inductorCurrent_{};
};

}  // namespace acfx::labs::opamp_stages
