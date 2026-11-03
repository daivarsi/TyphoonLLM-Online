# Current validation status

**Experimental. Answer quality remains unresolved.** Bundle 00.18.1 updates the companion and documentation; the installed Vita app remains 00.18.

## Source implementation

The PC companion classifies the action, optionally retrieves documents, and supplies reference text to local Vita generation. The latest patch adds a confidence-gated calculation/deduction category and removes duplicate ranking of the previous source. The generation prompt, runtime and weights are unchanged. Lookup limits remain 45 seconds client / 35 seconds companion. The companion never writes answers.

## Local automated checks

- 62 host checks pass, covering protocol bounds, fragmentation, truncated requests, IP allowlist rejection, cache isolation, no-fetch routing, weak deduction confidence and repeated ranking.
- Native 00.18 checks passed: 24 evidence-boundary, 10 reference-history, 6 echo and 5 actual-session completion controls. The native code has not changed since those checks and the successful VitaSDK build.
- The prior 16 intent regressions pass. The expanded classifier evaluation matches 33/36 expectations; event ordering, a timed-departure question and a social acknowledgment still route unnecessarily to research. [All cases](validation/companion-routing-evaluation.json).

These checks do not establish answer quality or physical performance. The wire protocol has no cryptographic authentication. Build warnings and source-provenance limitations remain documented.

## Physical hardware validation

| Evaluation | Result |
| --- | --- |
| [Original continuous-chat run](validation/ONLINE-00.18.md) | Original writing, identity and weapon cases succeeded; storage, washing, recipe, hypotheses and source interpretation had failures. |
| [Six new multi-step tasks](validation/MULTISTEP-00.18.md) | 0/6 complete passes. Wrong calculations, incorrect priorities, missing steps and unsupported explanations. |
| [Companion follow-up](validation/COMPANION-FOLLOWUP.md) | Budget routing became local with no fetched text, but its answer remained wrong. A new capacitor answer was correct locally after retrieval failed. |

The budget routing stage changed from a 35-second failed lookup to a 1.843-second local decision. **That is not full response time.** The retest took 68.538 seconds overall, versus about 58 seconds in the earlier conversation state. Different prefill/history work prevents claiming an end-to-end speedup.

No New Chat/CLEAR separated test questions. Displayed answers were checked against emitted token IDs; private conversation traces are not published. Full VitaShell installation was verified as VLMNET001 version 00.18, with executable and metadata matching the released VPK byte-for-byte. [Installation hashes](validation/installed-00.18.json).

## Release scope

Publish as an experimental demo with these limits visible. Further routing/prompt tuning is paused. A future quality investigation should establish a trusted model/runtime baseline before adding heuristics or features; that comparison has not been completed. The older [00.14 audit](validation/GENERAL.md) remains historical evidence.
