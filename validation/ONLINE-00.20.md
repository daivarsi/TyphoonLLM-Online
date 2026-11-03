# Online 00.20 — routing and source-grounding update

## What changed

The failing lowercase identity request gave weak support to every intent hypothesis, yet the old classifier treated the highest unsupported creative label as a local-writing decision. The explicit lookup then supplied an isolated weapon-design sentence while the model retained its previous false identity as an assistant turn.

The update compares binary entailment margins and requires positive support for local decisions. General editing/social paraphrases preserve those actions; no named-entity rules, question-prefix rules, canned answers, training or model-weight changes were added. Source selection retains the chosen document's opening context alongside its best detail when both fit the 850-byte budget. Cache policy is 13.

For sourced inference only, closed historical assistant replies are excluded from the model input. User requests and fetched reference material are retained within the context window. The canonical conversation retains actual generated replies, and local tasks use ordinary conversation history. Current evidence is always included explicitly. Generated tokens are committed back to the canonical ledger, including across continuations. Normal capacity eviction still applies; this is not unlimited memory.

## Local checks

- 68 companion tests passed, including protocol bounds, fragmentation, malformed/truncated requests, peer IP access rejection, cache behavior, weak-intent handling and evidence packing. The protocol still has no cryptographic authentication or encryption.
- All 45 existing native controls passed, plus a new source-history suite covering unchanged canonical input, retained user context/current assistant role, excluded historical assistant claims, continuation commits and bounds.
- VitaSDK build completed; compiler warnings remain. [Installed package hashes](installed-00.20.json).
- [47 routing regressions](routing-00.20.json): 44 matched expectations. Misses: ordering supplied events, a timed-departure calculation, and making a prior option cheaper. These are regression examples, not a blind benchmark. Three additional controls (Ada identity, original poem, cheerful rewrite) routed as expected on the PC and Vita.
- CPU diagnostics with the recorded failed conversation showed that clearer source-priority instructions and quoted history still repeated the false identity. Excluding old generated claims while retaining user/reference context corrected the identity. The combined identity-and-weapon request still omitted the weapon in that diagnostic. CPU scalar results are not claimed to be bit-identical to Vita execution.

## Physical Vita validation

Full VitaShell installation was verified as **VLMNET001 / 00.20**. Installed executable and metadata match the package byte-for-byte. The previous 00.19 conversation was saved privately before the required restart. All six following turns ran in one continuous 00.20 chat, with no New Chat/CLEAR between them.

| Turn | Request and observed result |
| --- | --- |
| 1 — offline baseline | Web access deliberately disabled. “Who is cloud strife” generated a false TV-series identity. This is a failed baseline, not a pass. |
| 2 — sourced correction | Web access enabled; same wording. Fetched text from SmashWiki was admitted. Answer: “Cloud Strife is a character from Square Enix's Final Fantasy series.” The earlier false answer remained visible, but its generated claims were absent from sourced inference. |
| 3 — follow-up | “What weapon does he use?” admitted source text and answered “Cloud uses a Buster Sword.” Referent retained through the source topic. |
| 4 — unrelated subject | “Who is ada lovelace.” admitted relevant text and answered “Ada Lovelace is the first computer programmer.” Basic identification succeeded, but the response omitted the source's “often considered” qualifier. |
| 5 — original writing | A short rain poem generated locally with zero fetched source bytes. It contains the typo “In de”. |
| 6 — conversational rewrite | “Make that poem more cheerful.” stayed local, retained the poem's subject and generated a cheerful rewrite. Zero fetched bytes. |

All six ended at EOS, without cancellation. Decoding the actual emitted token IDs matched all six displayed response bodies. The source and identity correction were checked in the actual model input, not inferred from successful transport or the green connection indicator. See [sanitized prompts and outputs](device-00.20.json) and [all screenshots](../docs/SCREENSHOTS.md).

The companion was restarted between turns 3 and 4 to load a Python-bool return compatibility correction; decision values did not change. The Vita conversation was not restarted or cleared. At completion the six-turn conversation remained available and no test queue was active.

## Limits

This fixes the demonstrated routing/previous-answer feedback failure in the tested cases. It does not establish general factual reliability. The 00.18 six-task evaluation remains 0/6 complete passes; arithmetic and multi-step generation were not fixed by this update. Source fidelity, qualifier preservation, ambiguous routing, provider availability and limited context remain imperfect. The combined-question diagnostic omitted one requested item. No latency or broad accuracy improvement is claimed.
