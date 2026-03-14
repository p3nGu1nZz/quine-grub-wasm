# Neural Network Policy – RL Trainer

The bootloader maintains an on-device reinforcement-learning policy that
predicts kernel quality from opcode statistics and guides the mutation
engine toward longer-surviving kernels.

---

## Architecture (current)

```
Input (kFeatSize = 1024 floats)
  │  indices 0-255  : WASM opcode frequency counts or opcode one-hot when
  │                    sequence training is active
  │  indices 256-1023: reserved for future features (currently zero)
  ▼
Layer 0  Dense  1024 → 32     (compact input projection)
  ▼
Layer 1  Dense   32 → 64      (feeds LSTM)
  ▼
Layer 2  LSTM    64 → 64      (temporal context; state reset on each new
                               telemetry entry during training)
  ▼
Layer 3  Dense   64 → 32      (dimensionality reduction)
  ▼
Layer 4  Dense   32 → 1       (scalar reward prediction)
```

The network has been dramatically shrunk from its original 1024‑wide
layout.  Aside from the reduced layer widths the only remaining recurrent
component is a single LSTM with a 64‑element hidden state; its weights are
currently untouched by weight updates for simplicity.

All dense weights are zero-initialised (so `forward(zeros) = 0`).
LSTM gate weights use Xavier-uniform initialisation
(`±sqrt(6/(inSize+hiddenSize))`) with a fixed LCG seed for
reproducibility; LSTM biases are zero.

---

## Feature Extraction (`src/core/feature.cpp`)

`Feature::extract(entry)` decodes the kernel base64 string, parses the
WASM code section, counts per-opcode occurrences, and returns a
`kFeatSize`-element float vector.  Indices 256-1023 are currently zero
and reserved for future features (e.g. section-size statistics).

---

## Reward & Loss (`src/core/train.cpp`, `src/core/loss.cpp`)

The previous regression-style reward scheme has been replaced by a
straightforward *next-token prediction* objective.

| Symbol | Value |
|--------|-------|
| vocabSize | 256 possible WASM opcodes + a handful of special tokens |
| target | next opcode in sliding window sampled from telemetry sequence |
| loss   | cross‑entropy between the network’s softmax output and the true token |
| avgLoss| exponential moving average used for progress reporting (α = 0.1) |

Weights corresponding to examples drawn from kernels that survived many
generations are given extra importance either by repeated sampling or by
attaching a multiplicative weight; entries from trapped kernels may be
assigned a special `BAD` token instead of the real next opcode.

---

## Training Update

Training now mimics the way modern language models are trained.  Each
evolutionary telemetry entry contains the full opcode sequence of the
kernel that executed that generation.  The host treats these sequences as a
corpus for a tiny autoregressive RNN, generating training examples by
sliding a fixed-length window (typical length 32–128) over the raw bytes.

For every position in the sequence we use the preceding tokens as input and
try to predict the next opcode.  The network's forward pass produces a
vocabulary-sized probability distribution; the cross‑entropy loss with the
true next opcode is computed and back‑propagated immediately.  The LSTM
layer naturally carries context across positions, allowing the model to
learn common subroutine patterns and control flows.

A modest **replay buffer** (default capacity 256 entries) keeps recent
telemetry sequences around so that training minibatches sample both the latest
and slightly older examples.  `Trainer::observe()` always updates on the
current entry first and then randomly samples one buffer element for a
second update.  Stale entries are discarded FIFO when the buffer fills.

When the automatic training trigger fires (see `App` documentation) the
buffer is cleared via `Trainer::reset()` before any new observations are
processed, ensuring that loss statistics and past gradients do not leak into
the fresh cycle.  We do **not** reset the network weights unless the user
explicitly loads a model from disk.

Weight updates remain simple SGD/Adam steps applied only to **dense** layers;
the LSTM gate weights are still left untouched for now.  A typical update
loop looks like:

```
vector<float> probs = m_policy.forward(features);
float loss = crossEntropy(probs, trueToken);
// back‑prop through dense layers only (in-place update using lr * grad)
```

This scheme completely avoids back-propagation through long-generation
histories, keeping updates fast and memory costs low.  It also aligns
naturally with the eventual goal of exporting the learned weights into the
kernel itself, where the same RNN can be executed on-device during
mutation time.

---

## Model Persistence

`Trainer::save(path)` / `Trainer::load(path)` serialise the full network
state including layer type (0 = DENSE, 1 = LSTM), in/out sizes, all
weights, and all biases.  The LSTM weight tensor has size
`4 × (in + hidden) × hidden`; the bias tensor has size `4 × hidden`.
A mismatch in type, in-size, or out-size causes `load()` to return
`false`.

---

## Neural Matrix Host Import (existing)

The kernel may call `env.record_weight(ptr, len)` to ship a serialised
weight blob back to the host via the `WeightCallback` supplied to
`WasmKernel::bootDynamic`.  This mechanism is independent of the C++
policy above and is intended for future kernel-side learning.

Future host imports (currently unimplemented):

- `env.add_edge(ptr, len, target, weight)` – append a graph edge.
- `env.query_weight(ptr, len, target) → f32` – query a transition weight.
- `env.serialize_matrix(ptr) → len` – write the compressed adjacency
  graph to linear memory.

---

## Future Work

- **BPTT for LSTM** – implement back-propagation through time so the
  LSTM gate weights are trained.
- **Richer features** – populate indices 256-1023 with section-size
  statistics, base64 entropy, generation delta, etc.
- **Experience inheritance** – propagate a compressed weight snapshot
  from parent to child kernel via `env.spawn`.
- **Compression** – spatial hashing / delta encoding for large graphs.

## Automatic Training Trigger

The host application now includes a simple scheduling mechanism: once the
system has successfully executed **50 generations** during an evolution run,
execution is halted, the accumulated telemetry is reloaded, and a training
phase begins automatically using the newest data.  This removes the need
for manual mode-switching and keeps the training dataset narrowly focused
on the most recent evolutionary events.

### Checkpointing & GUI Feedback

When training finishes (either via the automatic trigger or a direct
`enableEvolution()` call), the application begins a short countdown before
writing a checkpoint file named `model_checkpoint.dat` under the telemetry
root.  The countdown lasts three `App::update()` ticks; during that interval
`App::savingModel()` returns `true` and `App::saveProgress()` reports a
fractional progress value.  The GUI's training scene displays a secondary
progress bar labeled "Saving model..." while the countdown is active, and
transition to the evolution scene is delayed until after the checkpoint is
written.  This gives users visual confirmation that the model weights are
being persisted and avoids sudden screen flips.
