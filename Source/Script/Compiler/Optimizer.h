#pragma once

#include "Script/Compiler/Ir.h"

namespace schuimm::script {

// IR optimisation passes. Each pass preserves behaviour, including runtime errors: an
// instruction that may trap (integer overflow, division by zero, failed conversion, calls) is
// never removed and only folded when its constant operands provably do not trap. Each returns
// true if it changed the function.

// Sparse conditional constant propagation and folding: registers with a known constant value
// on every path are replaced by loads of that constant, foldable instructions are evaluated at
// compile time (with the VM's arithmetic, Script/Bytecode/Semantics.h), branches on constants
// become jumps, and range-step checks on known non-zero steps are dropped.
bool propagateConstants(IrFunction& function);

// Removes blocks that cannot be reached from the entry block and renumbers the rest.
bool removeUnreachableBlocks(IrFunction& function);

// Jump threading through empty blocks, branches whose targets coincide, and merging a block
// into its only predecessor when that predecessor jumps to it.
bool simplifyControlFlow(IrFunction& function);

// Removes instructions whose results are never used and that have no effect (liveness based).
bool eliminateDeadCode(IrFunction& function, const IrModule& module);

struct OptimizerSettings {
    int maximumIterations = 8; // The pipeline repeats until nothing changes or this many runs.
};

// Runs all passes on every function until a fixed point is reached.
void optimize(IrModule& module, OptimizerSettings settings = {});

} // namespace schuimm::script
