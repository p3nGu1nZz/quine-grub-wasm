(module
  ;; tiny 2-state recurrent kernel with weight callback
  (import "env" "record_weight" (func $record_weight (param f32) (param f32)))
  (global $h1 (mut f32) (f32.const 0))
  (global $h2 (mut f32) (f32.const 0))
  (func (export "run") (param $ptr i32) (param $len i32)
    ;; update hidden states each invocation
    (global.set $h1 (f32.add (global.get $h1) (f32.const 0.1)))
    (global.set $h2 (f32.mul (global.get $h2) (f32.const 0.99)))
    ;; report current hidden values as "weights" back to host
    (call $record_weight (global.get $h1) (global.get $h2))
  )
)
