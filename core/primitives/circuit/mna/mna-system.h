#pragma once

#include "primitives/circuit/node.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

// MnaSystem — Layer 1 of the Modified Nodal Analysis primitive: the abstract,
// component-agnostic bordered linear engine (contracts/mna-system.md; research
// decisions D1/D2/D3/D5/D7). It knows nothing about resistors, op-amps, or
// netlists — only how to STAMP an augmented [G B; C 0] system over node voltages
// and branch currents and SOLVE it by Gaussian elimination with partial
// pivoting. It is the reusable linear-algebra core the lab solvers
// (linear-solver.h, augmented-solve.h) are converging on.
//
// Layout (D2/D3):
//   - Node 0 is ground and is the dropped reference: local(id) = id - 1, so the
//     non-ground nodes 1..H occupy retained rows/cols 0..H-1. Ground contributes
//     to no retained row or column.
//   - Each branch k occupies augmented row/col index MaxNodes + k (the border).
//     The retained NODE dimension is only as wide as the highest node actually
//     referenced this assembly (nodeRows_), so unreferenced node slots never
//     appear as spurious all-zero (singular) rows; the branch border is appended
//     after the retained node block when the working system is compacted.
//
// RT-safety (Principle VI): every per-solve/read method is noexcept and
// allocation-free — solve() works entirely in fixed-size std::array stack
// storage. addBranch() is the ONLY method permitted to throw, and only at plan
// time on capacity overflow.
//
// No fallback (Principle V): a singular system yields solve() == false; no gmin
// is injected, no patched result is returned, and no NaN is left in a readable
// output.
//
// C++17, standard library only; no platform or component headers (the DSP core
// knows nothing of JUCE / libDaisy / Teensy). double throughout.

namespace acfx::mna {

template <int MaxNodes, int MaxBranches>
class MnaSystem {
public:
    static_assert(MaxNodes >= 1, "MnaSystem requires MaxNodes >= 1 (ground)");
    static_assert(MaxBranches >= 0, "MnaSystem requires MaxBranches >= 0");

    // Augmented dimension ceiling: MaxNodes node slots (index 0 is ground,
    // over-provisioning the retained block by one) plus MaxBranches border
    // slots. Fixed at instantiation.
    static constexpr std::size_t kDim =
        static_cast<std::size_t>(MaxNodes) + static_cast<std::size_t>(MaxBranches);

    // Zero the matrix, RHS, solution, matrix scale, and the referenced-node
    // extent between refreshes. Does NOT reset branchCount_: the branch count is
    // topological and fixed by the plan phase (data-model.md "State"). After
    // reset(), the solved output depends only on the stamps applied before the
    // next solve() (statelessness invariant).
    void reset() noexcept {
        for (std::size_t r = 0; r < kDim; ++r) {
            a_[r].fill(0.0);
            z_[r] = 0.0;
            x_[r] = 0.0;
        }
        matScale_ = 0.0;
        nodeRows_ = 0;
    }

    // Four-corner, ground-aware conductance stamp (D2): +g on each non-ground
    // diagonal and -g on each non-ground off-diagonal. A ground-referenced
    // conductance contributes a single diagonal +g.
    void stampConductance(NodeId i, NodeId j, double g) noexcept {
        noteNode(i);
        noteNode(j);
        const int li = local(i);
        const int lj = local(j);
        if (li >= 0) {
            a_[idx(li)][idx(li)] += g;
        }
        if (lj >= 0) {
            a_[idx(lj)][idx(lj)] += g;
        }
        if (li >= 0 && lj >= 0) {
            a_[idx(li)][idx(lj)] -= g;
            a_[idx(lj)][idx(li)] -= g;
        }
    }

    // Add current `i` to node `n`'s KCL balance (RHS). No-op if `n` is ground.
    void stampRhsCurrent(NodeId n, double i) noexcept {
        noteNode(n);
        const int ln = local(n);
        if (ln >= 0) {
            z_[idx(ln)] += i;
        }
    }

    // Allocate a branch-current unknown; returns its index k in [0, MaxBranches).
    // THE ONLY throwing method (plan-time, D7): throws on capacity overflow.
    int addBranch() {
        if (branchCount_ >= MaxBranches) {
            throw std::length_error(
                "MnaSystem::addBranch: branch capacity (MaxBranches) exceeded — "
                "the branch count is fixed at instantiation");
        }
        return branchCount_++;
    }

    // Branch incidence (D3): write +1/-1 into the B block (node KCL rows x branch
    // col) and the C block (branch row x node cols) for the non-ground terminals.
    // The branch current is defined to flow from p to n; +1 couples node p, -1
    // couples node n. A ground terminal sits on the dropped column and is omitted.
    void stampBranchIncidence(int k, NodeId p, NodeId n) noexcept {
        noteNode(p);
        noteNode(n);
        const std::size_t branch = branchIndex(k);
        const int lp = local(p);
        const int ln = local(n);
        if (lp >= 0) {
            a_[idx(lp)][branch] += 1.0;  // B: +1 into node p's KCL
            a_[branch][idx(lp)] += 1.0;  // C: +1 * v(p)
        }
        if (ln >= 0) {
            a_[idx(ln)][branch] -= 1.0;  // B: -1 into node n's KCL
            a_[branch][idx(ln)] -= 1.0;  // C: -1 * v(n)
        }
    }

    // Set branch k's constraint RHS (e.g. the imposed source voltage E).
    void stampBranchValue(int k, double rhs) noexcept {
        z_[branchIndex(k)] = rhs;
    }

    // Add -r on branch k's diagonal (D3/D5). r = 0 leaves the diagonal exactly
    // zero (an ideal source / nullor constraint), which is what makes partial
    // pivoting load-bearing rather than optional.
    void stampBranchResistance(int k, double r) noexcept {
        const std::size_t branch = branchIndex(k);
        a_[branch][branch] += -r;
    }

    // Solve the assembled augmented system by Gaussian elimination with partial
    // pivoting (D5). Returns false on a singular pivot by the relative threshold
    // (D1) and leaves every readable output zeroed (no NaN leak, D7); returns
    // true and writes the solution otherwise. noexcept, allocation-free.
    bool solve() noexcept {
        x_.fill(0.0);

        const int nodeRows = nodeRows_;
        const int activeDim = nodeRows + branchCount_;
        if (activeDim <= 0) {
            matScale_ = 0.0;
            return true;  // nothing to solve; all readable outputs are 0.
        }

        // Compact the retained node block [0, nodeRows) and the branch border
        // [MaxNodes, MaxNodes + branchCount_) into a contiguous working system.
        std::array<std::array<double, kDim>, kDim> work{};
        std::array<double, kDim> rhs{};
        for (int r = 0; r < activeDim; ++r) {
            const std::size_t sr = srcIndex(r, nodeRows);
            rhs[idx(r)] = z_[sr];
            for (int c = 0; c < activeDim; ++c) {
                work[idx(r)][idx(c)] = a_[sr][srcIndex(c, nodeRows)];
            }
        }

        // Relative singular-pivot gate (D1): threshold against the matrix scale,
        // not an absolute denormal floor, so a well-posed but poorly scaled
        // system (µS conductances beside unit sources) is not misclassified. An
        // all-zero matrix (scale 0) falls back to the exact-zero floor.
        double matScale = 0.0;
        for (int r = 0; r < activeDim; ++r) {
            for (int c = 0; c < activeDim; ++c) {
                matScale = std::max(matScale, std::fabs(work[idx(r)][idx(c)]));
            }
        }
        matScale_ = matScale;
        const double pivotEps = matScale > 0.0 ? kRelEps * matScale : kAbsFloor;

        // Forward elimination with partial pivoting.
        for (int col = 0; col < activeDim; ++col) {
            int pivotRow = col;
            double pivotMag = std::fabs(work[idx(col)][idx(col)]);
            for (int r = col + 1; r < activeDim; ++r) {
                const double mag = std::fabs(work[idx(r)][idx(col)]);
                if (mag > pivotMag) {
                    pivotMag = mag;
                    pivotRow = r;
                }
            }
            if (pivotMag < pivotEps) {
                return false;  // singular; x_ already zeroed.
            }
            if (pivotRow != col) {
                swapRows(work, rhs, col, pivotRow);
            }
            const double pivot = work[idx(col)][idx(col)];
            for (int r = col + 1; r < activeDim; ++r) {
                const double factor = work[idx(r)][idx(col)] / pivot;
                if (factor == 0.0) {
                    continue;
                }
                work[idx(r)][idx(col)] = 0.0;
                for (int c = col + 1; c < activeDim; ++c) {
                    work[idx(r)][idx(c)] -= factor * work[idx(col)][idx(c)];
                }
                rhs[idx(r)] -= factor * rhs[idx(col)];
            }
        }

        // Back-substitution into a compacted solution vector.
        std::array<double, kDim> sol{};
        for (int r = activeDim - 1; r >= 0; --r) {
            double sum = rhs[idx(r)];
            for (int c = r + 1; c < activeDim; ++c) {
                sum -= work[idx(r)][idx(c)] * sol[idx(c)];
            }
            sol[idx(r)] = sum / work[idx(r)][idx(r)];
        }

        // Scatter the compacted solution back to the full augmented layout.
        for (int r = 0; r < activeDim; ++r) {
            x_[srcIndex(r, nodeRows)] = sol[idx(r)];
        }
        return true;
    }

    // Solved voltage at `node` (ground -> 0.0). Total over [0, MaxNodes); an
    // out-of-range id is a plan-time precondition violation, never a hot-path
    // throw (D7).
    double nodeVoltage(NodeId node) const noexcept {
        const int ln = local(node);
        if (ln < 0) {
            return 0.0;
        }
        return x_[idx(ln)];
    }

    // Solved branch current for branch k. Total over [0, branchCount_).
    double branchCurrent(int k) const noexcept {
        return x_[branchIndex(k)];
    }

private:
    // Relative singular-pivot threshold (D1) and the exact-zero fallback floor
    // used only when the matrix scale is itself zero.
    static constexpr double kRelEps = 1e-12;
    static constexpr double kAbsFloor = 1e-300;

    // Local (0-based, ground-excluded) matrix index for a node, or -1 for
    // ground. Node k (k >= 1) lives at local row/col k - 1.
    static constexpr int local(NodeId node) noexcept {
        return isGround(node) ? -1 : (node - 1);
    }

    // Augmented storage index of branch k (the border row/col).
    static constexpr std::size_t branchIndex(int k) noexcept {
        return static_cast<std::size_t>(MaxNodes) + static_cast<std::size_t>(k);
    }

    // Narrowing-free std::array index from a validated non-negative int.
    static constexpr std::size_t idx(int i) noexcept {
        return static_cast<std::size_t>(i);
    }

    // Map a compacted working index to its augmented storage index: the first
    // nodeRows entries are the retained node block (identity), the rest are the
    // branch border starting at MaxNodes.
    static constexpr int srcIndex(int w, int nodeRows) noexcept {
        return w < nodeRows ? w : (MaxNodes + (w - nodeRows));
    }

    // Grow the retained-node extent to include a referenced non-ground node.
    // nodeRows_ ends up equal to the highest referenced node id, i.e. the count
    // of retained node rows (locals 0..nodeRows_-1).
    void noteNode(NodeId n) noexcept {
        if (!isGround(n) && n > nodeRows_) {
            nodeRows_ = n;
        }
    }

    // Swap two rows of both the working matrix and the RHS (over the full kDim
    // width; no active-dimension bookkeeping needed).
    static void swapRows(std::array<std::array<double, kDim>, kDim>& m,
                         std::array<double, kDim>& v, int a, int b) noexcept {
        std::array<double, kDim>& ra = m[idx(a)];
        std::array<double, kDim>& rb = m[idx(b)];
        for (std::size_t c = 0; c < kDim; ++c) {
            const double t = ra[c];
            ra[c] = rb[c];
            rb[c] = t;
        }
        const double tv = v[idx(a)];
        v[idx(a)] = v[idx(b)];
        v[idx(b)] = tv;
    }

    // ---- fixed-capacity state (NO heap, data-model.md MnaSystem fields) ------

    // Augmented matrix, RHS, and solution. Sized by kDim = MaxNodes + MaxBranches;
    // only the active (retained nodes + branches) block is used per solve.
    std::array<std::array<double, kDim>, kDim> a_{};
    std::array<double, kDim> z_{};
    std::array<double, kDim> x_{};

    // Branches allocated so far (0..MaxBranches); fixed by the plan phase and
    // intentionally preserved across reset().
    int branchCount_ = 0;

    // Largest |entry| seen in the active block this solve (for the D1 threshold).
    double matScale_ = 0.0;

    // Highest referenced non-ground node id this assembly == count of retained
    // node rows. Reset each assembly so an unreferenced node never manifests as
    // a spurious singular row.
    int nodeRows_ = 0;
};

}  // namespace acfx::mna
