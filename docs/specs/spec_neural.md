# Neural Network Policy — quine-grub-wasm

The application maintains an on-device reinforcement-learning policy that
predicts kernel quality from opcode statistics and guides the mutation
engine toward longer-surviving kernels.

---

## Architecture

```
Input (kFeatSize = 1024 floats)
  │  indices   0-255 : normalised WASM opcode histogram
  │  indices 256-511 : opcode bigram counts
  │  indices 512-523 : structural metadata (trap flag, generation, drop ratio, …)
  │  indices 640-1023: positional opcode sequence (first 384 ops, zero-padded)
  ▼
Layer 0  Dense  1024 → 32     (input projection; ReLU)
  ▼
Layer 1  Dense   32 → 64      (feeds LSTM; ReLU)
  ▼
Layer 2  LSTM    64 → 64      (temporal context; single-step BPTT in training)
  ▼
Layer 3  Dense   64 → 32      (dimensionality reduction; ReLU)
  ▼
Layer 4  Dense   32 → 1       (scalar reward prediction; ReLU)
```

### Weight Initialisation

All dense and LSTM gate weights use **Xavier-uniform** initialisation:

```
bound = sqrt(6 / (fan_in + fan_out))
weights ~ Uniform(-bound, bound)
```

Zero initialisation (used previously) caused symmetry breaking — every neuron
in a layer computed the same gradient, preventing learning.  Xavier init
ensures diverse starting gradients and avoids dead ReLUs.

---

## Feature Extraction (`src/nn/feature.cpp`)

`Feature::extract(entry)` decodes the kernel base64, parses the WASM code
section via `extractCodeSection()`, and fills a 1024-element float vector:

| Slot range | Content |
|---|---|
| 0–255 | Per-opcode frequency (histogram), normalised by sequence length |
| 256–511 | Bigram counts for consecutive opcode pairs |
| 512–523 | Structural metadata: trap flag, generation (normalised), drop ratio, diversity, sequence length, positional encoding |
| 640–1023 | Raw positional sequence — first 384 opcodes, zero-padded |

`Feature::extractSequence(entry)` returns the raw `std::vector<uint8_t>` of
code-section opcodes for the trainer's sequence branch and for `Advisor::score()`.

---

## Reward & Loss (`src/nn/train.cpp`, `src/nn/loss.cpp`)

Training uses a scalar regression objective.  The training target for each
telemetry entry is:

```
target = normReward − clamp(qualityPenalty / 50, 0, 0.2)
```

where `normReward = rawReward / maxReward` and `qualityPenalty` is the
non-generation portion of `Loss::compute()` (trap penalty + drop ratio +
diversity + length penalties).

`Loss::compute()` components:

| Signal | Effect |
|--------|-------|
| `-generation` | Primary reward — more generations lowers loss |
| Trap penalty | +8.0 when `trapCode` is non-empty |
| Drop-ratio penalty | Proportional when >40 % of opcodes are `drop` (0x1A) |
| Diversity penalty | Proportional when unique-opcode count is below 64 |
| Short-sequence penalty | +0.5 per instruction below 8 |
| Long-sequence reward | −up to 2.0 for sequences longer than 32 instructions |

`avgLoss` is an exponential moving average of `lastLoss` with decay `kEmaDecay = 0.90`.

---

## Training Update (Backpropagation)

`Trainer::observe(entry)` performs a full SGD step using **chain-rule
backpropagation** through the entire network, including the LSTM layer.

### Dense layer update

For each dense layer `l` (output to input):

```
δ[L] = 2 * (prediction − target)              // MSE output gradient
δ[l] = (W[l+1]ᵀ @ δ[l+1]) ⊙ ReLU'(a[l+1])  // chain rule
W[l] -= lr * δ[l] ⊗ a[l]ᵀ                    // weight update
b[l] -= lr * δ[l]                             // bias update
```

`ReLU'(a) = 1 if a > 0 else 0` — the derivative mask is applied elementwise.

### LSTM single-step BPTT

The LSTM gate cache saved during the forward pass (`f`, `i`, `g`, `o` gate
post-activation values; `c_prev`; concatenated input+hidden `xh`) is used
to compute gate gradients from `dh` (the delta propagated from the next layer):

```
dc       = dh * o * (1 − tanh²(c_t))
do_raw   = dh * tanh(c_t) * o * (1 − o)
df_raw   = dc * c_prev * f * (1 − f)
di_raw   = dc * g * i * (1 − i)
dg_raw   = dc * i * (1 − g²)
```

All four gate weight matrices are updated.  `dxh[:in]` is propagated backward
as the input delta for the preceding dense layer.  Gate gradients use the
original (pre-update) weight matrix to avoid modifying the matrix mid-step.

### Gradient clipping & weight decay

Each per-element weight delta is clamped to `±kGradClip = 0.5`, then
L2 weight decay `kWeightDecay = 2e-5` is applied to every weight.

---

## Replay Buffer

`Trainer` keeps a ring buffer (default capacity 256 entries) of recent
telemetry.  `observe()` trains on the current entry first, then randomly
samples one buffer element for an extra update.  Entries are evicted FIFO
when the buffer fills.

---

## Model Persistence

`Trainer::save(path)` / `Trainer::load(path)` serialise the full network state:

- Layer metadata: type (DENSE=0, LSTM=1), in/out sizes
- All weights and biases (space-separated floats, one layer per line)
- For LSTM layers: hidden state `lstmH` and cell state `lstmC` (two extra lines)
- Training stats: observation count, EMA loss, max reward

A size/type mismatch on load returns `false` without modifying the network.
LSTM state is restored so checkpoint round-trips produce identical forward-pass outputs.

---

## Neural Matrix Host Import

Kernels call `env.record_weight(a, b)` to report values (integers for
KERNEL_GLOB, floats for KERNEL_SEQ) back to the host via the `WeightCallback`
supplied to `WasmKernel::bootDynamic`.  `bootDynamic` tries both `(i32, i32)`
and `(f32, f32)` signatures automatically to avoid link errors.

---

## Automatic Training Trigger

After every `kAutoTrainGen` successful generations the app pauses evolution,
reloads telemetry, and enters a supervised training phase.  `Trainer::reset()`
clears stats and the replay buffer at the start of each cycle (weights are
preserved).  A `kSaveWaitSteps`-tick countdown then writes
`model_checkpoint.dat` before returning to evolution.

---

## Future Work

- **Adaptive training schedule** — adjust the cycle length based on loss
  plateau detection and buffer staleness.
- **Experience inheritance** — propagate a compressed weight snapshot from
  parent kernel to child via `env.spawn`.
- **Kernel-side weights** — export the trained policy into the WASM binary
  so mutations can be guided on-device.
