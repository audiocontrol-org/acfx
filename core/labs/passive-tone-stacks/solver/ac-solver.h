#pragma once

#include "primitives/circuit/components.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

// solveAC — the deliberately-bounded LINEAR complex `.ac` reference solver for
// the passive-tone-stacks lab (contracts/ac-solver.md; research.md R6; FR-011).
//
// HOST-ONLY, NON-NORMATIVE. This lives in the LAB, not the tone-stack primitive,
// and it exists only to read the frequency response of a passive netlist the
// builders assemble. It computes the EXACT continuous-time transfer function
// H(jω) = V(outNode) / V(inNode) by phasor nodal analysis: it stamps each
// component's admittance at jω — Resistor → 1/R, Capacitor → jωC, Inductor →
// 1/(jωL) — imposes the ideal input VoltageSource by fixed-node reduction (the
// same R1 method LinearSolver uses), and solves by complex Gaussian elimination
// with partial pivoting over std::complex<double>.
//
// THE SEAM (FR-006, carried into the frequency domain): the solver ASSEMBLES;
// each COMPONENT SUPPLIES ITS PHYSICS. It reads Resistor.R / Capacitor.C /
// Inductor.L to form the admittance; it never re-derives a component's law.
//
// THE BOUNDARY (FR-013): this is a LINEAR complex solve — NOT Modified Nodal
// Analysis (no source-branch-current unknowns), NOT a Newton loop, NOT
// trapezoidal integration. The "lab must never grow into MNA" boundary from
// component-abstractions carries forward verbatim. Phase 5/6 supersede this;
// it must never grow into them.
//
// NO HEAP (contracts/ac-solver.md): every working buffer is a fixed-size
// std::array sized by the Netlist template parameters — no new/delete, no
// std::vector, no allocation anywhere in solveAC. Descriptive std::exceptions
// are thrown for out-of-contract arguments (non-positive/non-finite ω, an
// invalid transfer-function node), an unsupported floating source, or an
// EXACTLY/catastrophically singular reduced system (a zero pivot) — never a
// silent wrong answer or a fabricated fallback (repo standard). Note: the zero-
// pivot guard catches an exactly-singular system, not a merely ill-conditioned
// one; the well-posed FMV/Baxandall stacks this lab validates never approach
// that regime.
//
// std::complex<double> throughout. This is HOST-ONLY lab code (it is not
// required to be platform-independent), but it stays standard-library-only and
// includes no platform or harness headers. C++17.

namespace acfx::labs::passive_tone_stacks {

using Complex = std::complex<double>;

namespace detail {

// Local (0-based, ground-excluded) matrix index for a node, or -1 for ground.
// Node k (k >= 1) lives at local row/col k-1 (mirrors LinearSolver::local).
constexpr int local(NodeId node) noexcept {
    return isGround(node) ? -1 : (node - 1);
}

}  // namespace detail

// Compute H(jω) = V(outNode) / V(inNode) for a prepared passive `nl` at angular
// frequency `omega`. See the contract for the full behavior.
//
// Preconditions: `nl` is a prepared (validated) netlist; `omega > 0`; `inNode`
// is driven (directly or indirectly) by an ideal VoltageSource so V(inNode) is
// non-zero. Throws std::runtime_error on a floating ideal source, a singular
// system at `omega`, or a zero input-node voltage.
template <int MaxNodes, int MaxComponents>
Complex solveAC(const Netlist<MaxNodes, MaxComponents>& nl, double omega,
                NodeId inNode, NodeId outNode) {
    using detail::local;
    using Idx = std::size_t;

    const int nodeCount = nl.nodeCount();

    // Fail loud on out-of-contract arguments before touching the system — never
    // a silent wrong answer (AUDIT-BARRAGE codex-01/02). `omega` must be finite
    // and > 0 (the phasor admittances jωC and 1/(jωL) are meaningless at ω = 0),
    // and the requested transfer-function nodes must be real nodes of `nl`
    // (a stale/invalid outNode must not fabricate H = 0, and node ≥ MaxNodes
    // must not read out of bounds).
    if (!std::isfinite(omega) || !(omega > 0.0)) {
        throw std::invalid_argument(
            "ac-solver: omega must be finite and > 0 (got " +
            std::to_string(omega) + ")");
    }
    if (!isValidNode(inNode, nodeCount) || !isValidNode(outNode, nodeCount)) {
        throw std::invalid_argument(
            "ac-solver: inNode/outNode out of range [0, nodeCount = " +
            std::to_string(nodeCount) + ")");
    }

    // Number of unknown (non-ground) node rows; node k maps to local k-1.
    const int n = nodeCount - 1;

    // Working complex system Y·v = i, sized by the template parameter. Stack
    // only — no heap (contracts/ac-solver.md).
    std::array<std::array<Complex, static_cast<Idx>(MaxNodes)>,
               static_cast<Idx>(MaxNodes)>
        y{};
    std::array<Complex, static_cast<Idx>(MaxNodes)> rhs{};
    for (int r = 0; r < n; ++r) {
        rhs[static_cast<Idx>(r)] = Complex(0.0, 0.0);
        for (int c = 0; c < n; ++c) {
            y[static_cast<Idx>(r)][static_cast<Idx>(c)] = Complex(0.0, 0.0);
        }
    }

    // Standard nodal admittance stamp of a complex Y between nodes a and b.
    const auto stampY = [&y](NodeId a, NodeId b, Complex adm) noexcept {
        const int la = local(a);
        const int lb = local(b);
        if (la >= 0) {
            y[static_cast<Idx>(la)][static_cast<Idx>(la)] += adm;
        }
        if (lb >= 0) {
            y[static_cast<Idx>(lb)][static_cast<Idx>(lb)] += adm;
        }
        if (la >= 0 && lb >= 0) {
            y[static_cast<Idx>(la)][static_cast<Idx>(lb)] -= adm;
            y[static_cast<Idx>(lb)][static_cast<Idx>(la)] -= adm;
        }
    };

    // Inject a current into the RHS following the +a / -b convention.
    const auto injectCurrent = [&rhs](NodeId a, NodeId b, Complex cur) noexcept {
        const int la = local(a);
        const int lb = local(b);
        if (la >= 0) {
            rhs[static_cast<Idx>(la)] += cur;
        }
        if (lb >= 0) {
            rhs[static_cast<Idx>(lb)] -= cur;
        }
    };

    // Stamp every component by reading ITS OWN physics at jω. VoltageSources are
    // imposed structurally afterward (fixed-node reduction). Diodes are not part
    // of a passive tone stack and carry no linear admittance — skipped.
    const auto comps = nl.components();
    for (Idx i = 0; i < comps.size(); ++i) {
        const Component& c = comps[i];
        if (const auto* r = std::get_if<Resistor>(&c)) {
            stampY(r->a, r->b, Complex(1.0 / r->R, 0.0));  // R -> 1/R
        } else if (const auto* cap = std::get_if<Capacitor>(&c)) {
            stampY(cap->a, cap->b, Complex(0.0, omega * cap->C));  // C -> jωC
        } else if (const auto* l = std::get_if<Inductor>(&c)) {
            stampY(l->a, l->b, Complex(0.0, -1.0 / (omega * l->L)));  // L -> 1/(jωL)
        } else if (const auto* cs = std::get_if<CurrentSource>(&c)) {
            injectCurrent(cs->p, cs->n, Complex(cs->current(), 0.0));
        }
        // VoltageSource: fixed-node reduction below. Diode: not in a passive stack.
    }

    // Impose every grounded ideal voltage source by fixed-node reduction (R1):
    // a node pinned to a KNOWN voltage has its column moved into the RHS of the
    // other rows, and its own row replaced by v = value. No gmin, no MNA.
    for (Idx i = 0; i < comps.size(); ++i) {
        const auto* v = std::get_if<VoltageSource>(&comps[i]);
        if (v == nullptr) {
            continue;
        }
        const bool pGround = isGround(v->p);
        const bool nGround = isGround(v->n);
        if (!pGround && !nGround) {
            throw std::runtime_error(
                "ac-solver: floating ideal voltage source not supported "
                "(grounded sources only)");
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
                "ac-solver: voltage source pins the ground node");
        }
        const Complex vc(value, 0.0);
        for (int r = 0; r < n; ++r) {
            if (r == lp) {
                continue;
            }
            rhs[static_cast<Idx>(r)] -=
                y[static_cast<Idx>(r)][static_cast<Idx>(lp)] * vc;
            y[static_cast<Idx>(r)][static_cast<Idx>(lp)] = Complex(0.0, 0.0);
        }
        for (int c = 0; c < n; ++c) {
            y[static_cast<Idx>(lp)][static_cast<Idx>(c)] = Complex(0.0, 0.0);
        }
        y[static_cast<Idx>(lp)][static_cast<Idx>(lp)] = Complex(1.0, 0.0);
        rhs[static_cast<Idx>(lp)] = vc;
    }

    // Complex Gaussian elimination with partial pivoting over the active block.
    std::array<Complex, static_cast<Idx>(MaxNodes)> sol{};
    if (n > 0) {
        constexpr double kPivotEps = 1e-300;
        for (int col = 0; col < n; ++col) {
            int pivotRow = col;
            double pivotMag = std::abs(y[static_cast<Idx>(col)][static_cast<Idx>(col)]);
            for (int r = col + 1; r < n; ++r) {
                const double mag = std::abs(y[static_cast<Idx>(r)][static_cast<Idx>(col)]);
                if (mag > pivotMag) {
                    pivotMag = mag;
                    pivotRow = r;
                }
            }
            if (pivotMag < kPivotEps) {
                throw std::runtime_error(
                    "ac-solver: singular reduced system at omega = " +
                    std::to_string(omega) + " (ill-posed passive network)");
            }
            if (pivotRow != col) {
                for (int c = 0; c < n; ++c) {
                    std::swap(y[static_cast<Idx>(col)][static_cast<Idx>(c)],
                              y[static_cast<Idx>(pivotRow)][static_cast<Idx>(c)]);
                }
                std::swap(rhs[static_cast<Idx>(col)], rhs[static_cast<Idx>(pivotRow)]);
            }
            const Complex pivot = y[static_cast<Idx>(col)][static_cast<Idx>(col)];
            for (int r = col + 1; r < n; ++r) {
                const Complex factor =
                    y[static_cast<Idx>(r)][static_cast<Idx>(col)] / pivot;
                if (factor == Complex(0.0, 0.0)) {
                    continue;
                }
                y[static_cast<Idx>(r)][static_cast<Idx>(col)] = Complex(0.0, 0.0);
                for (int c = col + 1; c < n; ++c) {
                    y[static_cast<Idx>(r)][static_cast<Idx>(c)] -=
                        factor * y[static_cast<Idx>(col)][static_cast<Idx>(c)];
                }
                rhs[static_cast<Idx>(r)] -= factor * rhs[static_cast<Idx>(col)];
            }
        }
        for (int r = n - 1; r >= 0; --r) {
            Complex sum = rhs[static_cast<Idx>(r)];
            for (int c = r + 1; c < n; ++c) {
                sum -= y[static_cast<Idx>(r)][static_cast<Idx>(c)] * sol[static_cast<Idx>(c)];
            }
            sol[static_cast<Idx>(r)] = sum / y[static_cast<Idx>(r)][static_cast<Idx>(r)];
        }
    }

    // Node voltage (ground == 0). Valid after the solve above.
    const auto voltage = [&sol](NodeId node) noexcept -> Complex {
        if (isGround(node)) {
            return Complex(0.0, 0.0);
        }
        return sol[static_cast<Idx>(node - 1)];
    };

    const Complex vin = voltage(inNode);
    if (std::abs(vin) < 1e-300) {
        throw std::runtime_error(
            "ac-solver: input node voltage is zero; cannot form H(jω)");
    }
    return voltage(outNode) / vin;
}

}  // namespace acfx::labs::passive_tone_stacks
