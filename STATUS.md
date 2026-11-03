# 00.18 validation status

## Source implementation

The companion performs pretrained action classification before any source lookup. Source ranking remains separate. The unreliable free-form local planner and its experimental decoding constraints were removed. All answer generation remains on the Vita. Lookup deadlines are 45 seconds client / 35 seconds companion. Full-response reservation, complete-turn eviction, reference reuse and one echo retry remain in place. New Chat invalidates in-flight source results.

## Local automated tests

59 host tests pass, including bounds, fragmentation, truncated requests, peer allowlist rejection, intent gating without fetching, and in-flight reset isolation. Native tests pass: 24 evidence-boundary controls, 10 reference-history controls, 6 echo controls and 5 actual-session completion controls. VitaSDK compilation succeeds with existing style/truncation warnings. Compilation is not physical validation. The protocol has no cryptographic authentication.

The actual intent model passed 22 action-classification probes, including separately worded creative requests and factual questions about fiction. This is a limited evaluation, not a guarantee for arbitrary inputs.

## Physical validation

The 00.18 continuous-chat run and three same-chat diagnostic retests are complete. See [the per-case report](validation/ONLINE-00.18.md). Original writing fetched no source text and produced a complete poem. Character identity and the weapon follow-up were correct. Before the companion ranking correction, the mushroom topic switch and washing follow-up failed retrieval and produced incorrect local answers; the local scrambled-egg recipe was also incorrect. The Python file-reading example was complete and valid by inspection. The general ranking correction subsequently selected relevant mushroom-storage text and a complete recipe in live PC probes. A same-chat physical storage retest received the correct source tokens but contradicted their explicit instruction against airtight containers. The autumn-leaf answer likewise reversed the pigment-masking relationship despite correct source input. The alien-theories lookup reached its deadline and local generation invented or misapplied theories. These are answer-quality failures, not transport successes. Earlier 00.17 interrupted runs failed intent recognition and are not counted as successful validation. No test questions are separated by New Chat or CLEAR.

The corrected recipe retest completed with source ingredients and a shortened stovetop procedure, but omitted some steps. The build remains experimental with demonstrated answer-quality failures.

Full VitaShell installation verified: VLMNET001 version 00.18. Installed metadata and executable match the VPK byte-for-byte; hashes are in [installed-00.18.json](validation/installed-00.18.json). This is the same executable used in the physical run.
