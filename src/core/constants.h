#pragma once

#include "types.h"
#include <string>

// A valid minimal WASM binary encoded in Base64.
// This module exports a 'run' function and imports 'env.log' AND 'env.grow_memory'.
// It acts as a bridge: host writes source to WASM memory, module echoes it back via log.
//
// WAT Source:
// (module
//   (type $t0 (func (param i32 i32)))
//   (type $t1 (func (param i32)))
//   (import "env" "log" (func $log (type $t0)))
//   (import "env" "grow_memory" (func $grow_memory (type $t1)))
//   (memory (export "memory") 1)
//   (func (export "run") (param $ptr i32) (param $len i32)
//     (call $log (local.get $ptr) (local.get $len))
//     (nop)
//   )
// )
//
// Hex: 0061736d01000000010a0260027f7f0060017f00021d0203656e76036c6f67000003656e760b67726f775f6d656d6f72790001030201000503010001071002066d656d6f727902000372756e00020a0b010900200020011000010b
static const std::string KERNEL_GLOB =
    "AGFzbQEAAAABCgJgAn9/AGABfwACHQIDZW52A2xvZwAAA2Vudgtncm93X21lbW9yeQABAwIBAAUDAQAB"
    "BxACBm1lbW9yeQIAA3J1bgACCgsBCQAgACABEAABCw==";

// A simple recurrent sequence-prediction kernel used for early
// experiments with in-kernel learning.  It maintains two mutable globals
// (`h1` and `h2`), updates them each invocation, and reports their values
// back to the host via the `record_weight` import.  Training on the host
// will interpret these as a tiny 2×2 weight matrix.
//
// The corresponding WAT source lives in `src/wasm/sequence_model.wat`.
static const std::string KERNEL_SEQ =
    "AGFzbQEAAAABCwJgAn19AGACf38AAhUBA2Vudg1yZWNvcmRfd2VpZ2h0AAADAgEBBhECfQFDAAAAAAt9AUMAAAAACwcHAQNydW4AAQoeARwAIwBDzczMPZIkACMBQ6RwfT+UJAEjACMBEAAL";

static const BootConfig DEFAULT_BOOT_CONFIG = {
    /* memorySizePages */ 1,
    /* autoReboot      */ true,
    /* rebootDelayMs   */ 2000,
};
