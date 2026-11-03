# Online app separation — 00.19

## Source implementation

Online retains its separate repository, VLMNET001 title and TyphoonLLM Online bubble. Model and tokenizer reads now use only `ux0:data/TyphoonOnline/`; there is no fallback to offline storage. Settings, logs and test files already used this directory. The visible header names Online explicitly, and the build emits `TyphoonLLM-Online.vpk`.

The Online release hosts its own unchanged model, tokenizer and exact model license as separate downloads. The offline app and data are not dependencies. No model, generation, retrieval, protocol or clock-policy changes were made.

## Local validation

- VitaSDK build completed, with existing compiler warnings.
- Native checks passed: 24 evidence boundaries, 10 reference-history boundaries, 6 literal-echo controls, 5 actual-session completion controls.
- Model and tokenizer hashes match the documented originals. Source inspection confirms both paths are Online-only.
- Companion and protocol are unchanged; the prior 62 host checks remain historical evidence, not newly measured answer quality.

## Physical hardware validation

- Full VitaShell installation: VLMNET001 / 00.19 / TyphoonLLM Online.
- Installed metadata and executable match the package byte-for-byte; see [package verification](installed-00.19.json).
- Startup log records `MODEL path=ux0:data/TyphoonOnline/model.vlp` and successful model loading. The explicit Online header renders without overlap.
- Both Online asset hashes were verified on the device after copying; the original offline executable retains SHA256 `e1e6ffa084645fd6499998a30ce755cfe1ce395d088c8573c2b1e3e7b8e7e32d`.
- Before update, the physical screen showed an empty welcome screen. Its log and screen were preserved privately before closing Online. The smoke test uses no CLEAR/New Chat command.

The smoke prompt was “Explain how a bicycle brake slows the wheel in two short sentences.” The app completed at EOS with 21 generated tokens, no web context and no cancellation. The decoded token stream matches the displayed answer: “A bicycle brake slows the wheel by applying force to the brake pads, which exert pressure on the rim.” This is plausible for rim brakes, but misses the two-sentence instruction and an explicit friction explanation. The test queue completed and the single-turn chat was left intact. Prior answer-quality failures remain applicable; see [screenshots](../docs/SCREENSHOTS.md) and [STATUS](../STATUS.md).
