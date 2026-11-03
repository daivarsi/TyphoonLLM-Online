# Online 00.18 physical validation

The same Vita conversation ran nine queued questions, followed by three manually typed diagnostic retests. No New Chat or CLEAR separated questions. The companion was updated between questions 7 and 8; the Vita process and dialogue were retained. Repeated questions are diagnostic checks, not independent style scores.

| Case | Observed outcome |
| --- | --- |
| Original creative writing | Complete original poem; no fetched source bytes or source footer. |
| Character identity | Correct game and creator from relevant source text. |
| New-information follow-up | Correct weapon; antecedent retained. |
| Unrelated storage topic, before rank correction | No admitted evidence; incorrect local storage advice. |
| Washing follow-up, before correction | No admitted evidence; incorrect local advice. |
| Recipe, before correction | Local answer invented unsuitable ingredients and oven procedure. Continuation completed, but content failed. |
| Python file reading | Complete, syntactically valid example by inspection; no fetched evidence. |
| Extraterrestrial-life hypotheses | Lookup deadline; local answer misapplied or invented theories. |
| Autumn leaf color | Correct source was supplied; generated explanation reversed the masking relationship. |
| Storage retest, after rank correction | Correct relevant source tokens reached the model; generated answer contradicted the explicit instruction against airtight containers. |
| Washing retest, after correction | Correct source topic retained, but selected passage did not answer the washing question. Generated answer was unsupported and incorrect. |
| Recipe retest | Completed at EOS with the fetched ingredients and a shortened stovetop method. Omitted the explicit transfer into the pan and removal-from-heat guidance; improved over the local recipe but not a complete procedural-fidelity pass. |

The source correction ranks discovered documents before passages, filters unfinished prose fragments, and permits current-request relevance independently of stale context. These are general changes, not entity exceptions. Live PC source probes recovered the storage and recipe evidence and retained the weapon passage. This does not make the full assistant reliable: both evidence selection and source-conditioned generation still have demonstrated failures.

All 14 generation segments across 12 questions matched the displayed bodies exactly. Private token logs were decoded with the unchanged tokenizer and compared with displayed response bodies. Intermediate continuation segments are not counted as separate questions. User logs are not included in this public repository.

## Automated checks

59 host checks pass, including protocol bounds, fragmentation, truncation, IP allowlist rejection, reset isolation, local-intent no-fetch behavior, complete-fragment filtering and document ordering with stale context. Native checks pass: 24 evidence-boundary, 10 reference-history, 6 echo and 5 actual-session completion controls. The protocol has no cryptographic authentication. Compilation and these checks do not establish hardware answer quality.
