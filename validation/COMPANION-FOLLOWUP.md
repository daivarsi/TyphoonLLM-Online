# Companion routing follow-up

## Shipped changes

The companion recognizes calculations and deductions from supplied data as a general local action. This route requires positive binary entailment, rather than winning only because other labels score worse. Prior-source passages are ranked once after current-request source collection, instead of repeatedly before it. Cache policy advances from 11 to 12. No entities, question strings or canned answers are used in production rules.

The Vita executable, weights, tokenizer and generation prompt remain unchanged. The companion was replaced while the app and its conversation remained running.

## Validation

62 host tests pass, including bounds/fragmentation, peer allowlist rejection, cache isolation, weak deduction confidence and duplicate-ranking regression controls. The protocol still uses an IP allowlist, not cryptographic authentication. The existing 16 action regressions pass. The expanded classifier check matches 33 of 36 cases; ordering events, a timed-departure question and a social acknowledgment still trigger unnecessary research. Full cases are in [companion-routing-evaluation.json](companion-routing-evaluation.json).

## Physical checks in the same conversation

| Request | Routing/source result | Generated-answer result |
| --- | --- | --- |
| Repeated multi-step shopping budget | Local decision in 1.843 seconds, zero fetched bytes. Previously spent 35 seconds on a failed lookup. | Still wrong: 240-dollar total and negative change. Routing improvement is not an arithmetic fix. |
| New capacitor definition/storage/use question | Research remained enabled; lookup returned no useful source after 12.359 seconds with an HTTP error among source failures. | Correct local explanation of electrical-energy storage and filtering/smoothing uses. This was not a successful sourced answer. |

Both outputs finished at EOS, and displayed bodies matched actual generated token IDs. No app restart or New Chat/CLEAR separated these checks from the earlier eight-turn conversation. Lookup time is not total answer time: the budget turn took 68.538 seconds, including a longer prefill after history eviction. No overall speedup is claimed from that comparison.

## Rejected experiments and remaining limits

A broader reasoning category misrouted factual follow-ups. A separate conversational-dependency classifier failed its probes. A lower passage threshold admitted weak evidence. Query relaxation and smaller passage shortlists did not establish a reliable quality gain. Those changes are excluded.

Shorter and step-by-step system prompts still produced wrong arithmetic in local diagnostics using the unchanged installed model and engine. No prompt change was deployed. Host scalar inference is not assumed to be token-identical to Vita inference; it was a rejection screen, not hardware validation.

Arithmetic, multi-step instruction completion, evidence sufficiency and retrieval coverage remain unresolved. This patch addresses specific routing and repeated-work defects; it does not turn the build into a reliable factual or reasoning assistant.
