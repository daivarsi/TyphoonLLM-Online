# Current validation status

**Experimental. Version 00.20 fixes the demonstrated weak-intent routing and prior-generated-answer feedback in the tested Cloud cases. General answer quality remains limited.**

## Source implementation

The companion supplies retrieved source text; the Vita generates every answer. Local intent requires positive binary entailment support. Evidence selection preserves bounded document context with a relevant detail. Sourced inference retains user/reference context but excludes historical generated assistant claims; the canonical conversation is preserved. Local tasks retain ordinary conversation history. Models, weights, tokenizer and clock policy are unchanged. Online remains a separate app and repository with independent data/downloads.

## Local automated checks

- 68 host tests passed, covering protocol bounds, fragmentation, truncation, peer access, cache behavior, routing and evidence packing.
- 45 existing native controls plus the new source-history filtering/commit/continuation/bounds suite passed. VitaSDK build completed with warnings.
- 44/47 routing regression cases matched expectations; three ambiguous reasoning/revision cases still search unnecessarily. Three additional controls routed as expected. This is not a blind benchmark.

These checks do not establish answer accuracy. [Full 00.20 report](validation/ONLINE-00.20.md).

## Physical hardware validation

| Evaluation | Result |
| --- | --- |
| [00.20 continuous chat](validation/ONLINE-00.20.md) | False offline Cloud identity corrected using fetched context; Buster Sword follow-up succeeded. Unrelated Ada identity used relevant evidence but lost a qualifier. Original poem and cheerful rewrite stayed local. All six outputs matched emitted tokens. |
| [00.19 separation](validation/SEPARATION-00.19.md) | Independent model path and installation verified. Generation completed but missed a two-sentence instruction. |
| [00.18 source/conversation tests](validation/ONLINE-00.18.md) | Mixed successes and failures, including contradiction of supplied sources. |
| [00.18 six multi-step tasks](validation/MULTISTEP-00.18.md) | 0/6 complete passes. Wrong arithmetic, priorities, missing steps and unsupported explanations. |
| [Earlier companion follow-up](validation/COMPANION-FOLLOWUP.md) | Budget routing became local but the answer remained wrong. |

The installed 00.20 metadata and executable match its VPK byte-for-byte: [verification](validation/installed-00.20.json). No New Chat/CLEAR separated the six 00.20 prompts. Private logs are not published; sanitized results and screenshots are included.

## Remaining limits

No broad answer-quality or speed claim is made. The earlier reduction from 35 seconds to 1.843 seconds measured only a routing stage; that earlier full answer took longer and remained incorrect. Source-grounded answers can still lose qualifications or omit requested parts. A trusted independent model/runtime baseline remains future work.
