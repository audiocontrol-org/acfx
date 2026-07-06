#pragma once

#include "primitives/circuit/netlist.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/models/opamp.h"
#include "primitives/circuit/node.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <variant>

// AugmentedSolver — the low-level bordered-matrix engine behind NullorSolver
// (opamp-stage-solver.h): the working matrix/RHS storage, the per-op-amp
// bordering stamp (the B/C block of the [G B; C 0] system — see the header
// comment of opamp-stage-solver.h for the full math), fixed-node-reduction
// pinning of grounded ideal voltage sources, and Gaussian elimination with
// partial pivoting. Split out of opamp-stage-solver.h PURELY to keep each
// header under the acfx per-file size cap (Constitution VII) and a downstream
// fleet byte-envelope — this is a behavior-preserving extraction, not a new
// abstraction: NullorSolver still owns ALL netlist physics (component
// stamping, reactive history) and composes exactly one AugmentedSolver
// instance to do the bordered linear algebra.
//
// NON-NORMATIVE, PHASE-5-SUPERSEDED LAB scaffolding; host-only, C++20 OK. It
// must stay a dumb bordered-matrix engine: no component physics, no reactive
// history — those stay in NullorSolver so the bounded-charter tripwires
// (research R6) remain checkable in one place.

namespace acfx::labs::opamp_stages {

// MaxOpAmps is the compile-time op-amp capacity; the augmented dimension is
// MaxNodes + MaxOpAmps (tripwire iii), fixed at instantiation. Template
// parameters mirror NullorSolver's exactly so the two stay in lockstep.
template <int MaxNodes, int MaxComponents, int MaxOpAmps = 1>
class AugmentedSolver {
public:
    // Augmented (bordered) dimension ceiling — MaxNodes node slots (index 0 is
    // ground, over-provisioning by one) plus MaxOpAmps branch slots. Fixed at
    // instantiation (tripwire iii).
    static constexpr std::size_t kDim =
        static_cast<std::size_t>(MaxNodes) + static_cast<std::size_t>(MaxOpAmps);

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

    // Border per OpAmp (research R2; see opamp-stage-solver.h's header comment
    // for the B/C stamp). Op-amp #k (netlist order) owns branch column and
    // constraint row index (n + k); a grounded input terminal (local == -1) is
    // on the dropped ground column and simply omitted (that is how the
    // inverting config's grounded inPlus yields the virtual ground
    // -V(inMinus) = 0).
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

    // Fixed-node reduction of node `lp` (local index) to `value` over the
    // active N x N system: move column lp into every other row's RHS (nodal
    // AND nullor constraint rows), then overwrite row lp with v[lp] = value.
    // lp is always a node column (< n) and branch columns are never pinned,
    // so B/C stays intact.
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

    // Solve the bordered system A·x = rhs in place by Gaussian elimination
    // with partial pivoting over the active N x N block, leaving the result
    // in solution_ (read back via solution()). Partial pivoting is load-
    // bearing — the [C,0] block has a zero diagonal, so a naive diagonal
    // pivot would divide by zero on a well-posed nullor system. A near-zero
    // pivot after pivoting means the bordered matrix is singular: the
    // authoritative well-posedness gate (R5), a hard descriptive error.
    void gaussianSolve(int N) {
        if (N <= 0) {
            return;
        }

        // Relative singular-pivot gate (R5 / FR-016): threshold the pivot
        // against the matrix scale, not an absolute denormal floor. A near-
        // singular bordered system yields a post-elimination pivot ~1e-14 that
        // a 1e-300 floor would pass, dividing through to a garbage voltage with
        // no throw — the silent-wrong-answer the well-posedness gate forbids.
        // Scale = max |entry| (the ±1 nullator rows keep it >= 1); 1e-12·scale
        // sits well below a healthy conductance pivot yet catches singularity.
        // All-zero matrix (scale 0) falls back to the exact-zero floor.
        double matScale = 0.0;
        for (int r = 0; r < N; ++r) {
            for (int c = 0; c < N; ++c) {
                matScale = std::max(matScale,
                    std::fabs(a_[static_cast<std::size_t>(r)]
                                [static_cast<std::size_t>(c)]));
            }
        }
        const double kPivotEps = matScale > 0.0 ? 1e-12 * matScale : 1e-300;

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
    }

    // Solved unknown at active-system row `i` (node rows 0..n-1, branch rows
    // n..N-1); valid after gaussianSolve(). NullorSolver scatters these into
    // node voltages and op-amp branch currents.
    double solution(int i) const noexcept {
        return solution_[static_cast<std::size_t>(i)];
    }

private:
    // Swap two rows of both the matrix and the RHS (over the full kDim width,
    // so no active-N bookkeeping is needed).
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

    // Bordered working matrix and RHS. Sized by kDim = MaxNodes + MaxOpAmps;
    // only the active leading N x N block is ever used.
    std::array<std::array<double, kDim>, kDim> a_{};
    std::array<double, kDim> rhs_{};
    std::array<double, kDim> solution_{};
};

}  // namespace acfx::labs::opamp_stages
