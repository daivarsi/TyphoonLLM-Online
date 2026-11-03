# Model and provenance

- Upstream: **LiquidAI/LFM2.5-230M**, Liquid AI; preserved revision `40cb2ad3b3044d5a41eee083a6103c8b523afa45`.
- Parameters: 229,693,184. Original architecture/tokenizer vocabulary retained.
- Adaptation: rank-4 LoRA, alpha 8, 970,752 trainable adapter parameters; 200 steps, batch 2, learning rate 1e-4. Merged into a separate FP32 master before conversion; no additional runtime adapters.
- Data: 1,090 procedural arithmetic/format examples with computed targets. No teacher factual/chain-of-thought targets. Recorded zero exact evaluation-prompt overlap, with intentional task-family overlap: development-set evidence, not blind generalization. Liquid AI’s undisclosed training datasets are not inferred.
- Deployment: Q8 group64 weights, dynamic Q8 activations, FP32 norms/convolution kernels, custom `.vlp` container, fully offline execution.
- Tokenizer: original Liquid tokenizer converted to binary, with retained BPE/Unicode tables. Historical 11,940-case regression matched upstream tokenization.

`model.vlp` SHA-256:
`5776ca33e876117c040addce3bfd41a6652e35c7d30d7b0948095340b723ab6d`

`tokenizer.bin` SHA-256:
`4c35695383354807cfd63e2e1574b7a24d5b792dc32d1471e26fb377f79f88c6`

Both are separately supplied Release assets, copied unchanged from preserved outputs and matched to frozen metadata. The VPK does not bundle weights.

Upstream terms: **LFM Open License v1.0**, preserved verbatim in [model notice](docs/notices/LFM-Open-License-1.0.txt), also supplied as MODEL_LICENSE.txt beside weights. Typhoon’s adaptation, runtime and application features are separate modifications. See [LICENSE_NOTES.md](LICENSE_NOTES.md) for inherited source rights gaps.
