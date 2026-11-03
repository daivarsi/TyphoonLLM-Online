# TyphoonLLM Online 00.14: stopped-run audit

The test is stopped. The app is suspended on LiveArea, with its conversation retained. No further device test or code fix was performed during this audit.

## What actually ran

The corrected continuous-chat run completed four prompts. It contains zero `CLEAR` commands and zero logged New Chat/context resets. The attempted extension to recipe, tutorial, aliens and seasonal-science prompts was not consumed by the running reader; those questions did not run and are not counted.

| Prompt | Observed result |
| --- | --- |
| Who the fuck is cloud strife | Correctly identified a Final Fantasy character. |
| What game is he from? | Correctly answered Final Fantasy VII; planner resolved Cloud from history. |
| I need help storing fresh mushrooms so they last longer. | Failed: planner searched cloud_strife and model answered about Cloud. |
| Write a short poem about moonlight. | Planner produced an unusable query; original-message lookup returned no useful evidence; ordinary local generation produced a poem. |

The first, earlier follow-up attempt included a reset and is invalid as a continuity test. It is excluded from the corrected result above.

## Confirmed cause of the mushroom failure

At 199485 ms the mushroom request was submitted. At 216557 ms the private search model produced `cloud_strife`. The client accepted it because its word-grounding check considers the union of previous topic, previous message, previous answer and current message. This check prevents new vocabulary but does not establish relevance to the current request.

The client sends only that planned query to the companion (`vita/src/phase4_generation.inc`, retrieval_query call). The companion therefore resolves and ranks against Cloud, without seeing the mushroom request. At 218372 ms it returned the Cloud Wikipedia document. At 218446 ms the client admitted it as reference material.

The answer model did receive the original mushroom request verbatim. The decoded prompt audit confirms that. However, it also received the unrelated Cloud material, and generated a Cloud answer. The universal instruction to ignore irrelevant material did not protect this small model.

This is a pipeline design failure plus a model reliability limit. It is not a typo in the mushroom prompt or a missing mushroom-specific rule. No subject-specific exception would address the general problem.

## History and output audit

The successful follow-up retained two turns, reused 195 tokens and evicted zero turns. Its planner checkpoint was restored. No new-chat reset occurred anywhere in this run.

Before answering mushrooms, two older model-context turns were evicted to fit the new input and answer reserve in the 512-token window. This is bounded context eviction, not clearing the visible chat. The 256-token fallback target can be more aggressive than the actual capacity requires in other cases; that is a separate runtime/history concern, not the demonstrated cause of this wrong query.

All four displayed answer bodies match decoding of their emitted model token IDs. There is no final-answer sentence insertion in this run. Source-like output is nevertheless possible: genuine generation does not prove independent wording or factual correctness. See stopped-token-audit.json and stopped-device-run.log for local evidence. These raw traces are not uploaded to GitHub.

## Validation limits and performance

28 host tests pass for document reading, transport, structured content, reset isolation and health checks. Those tests do not demonstrate topic-switch relevance or answer quality. The continuous-chat mushroom test fails that requirement.

Search planning took about 12.7 s, 19.1 s, 17.1 s and 38.2 s across these four requests. Heap use rose from roughly 22 MB after warmup to about 34 MB during the run, including retained checkpoint capacity. No crash/OOM was observed; these samples do not establish endurance or thermal behavior. Planning overhead is substantial.

The independent connectivity thread starts and confirms Internet reachability before model loading. A working green online badge does not establish source relevance.

## General correction required, not implemented during this audit

Preserve the original current request as an independent retrieval relevance anchor. A planned query must be a proposal, not the only question the companion ever sees. Prior chat can help resolve references, but cannot by itself validate a query for a new subject. Relevant source admission must be checked against the actual request; rejected or unusable evidence must lead to ordinary local generation. The answer remains model-generated with optional context.

Future validation must include topic switches and pronoun follow-ups in a single uninterrupted chat. No question-specific answers, phrase whitelist or subject patches are proposed. Do not claim this is fixed until that complete path is implemented and physically retested.

## Publication state

Offline 03.06 is published with its runtime VPK and checksum: https://github.com/daivarsi/TyphoonLLM/releases/tag/v03.06

Online source is public and experimental: https://github.com/daivarsi/TyphoonLLM-Online

No online 00.14 binary release is published. Its public documentation is updated to identify this confirmed topic-carryover failure. The installed online executable is the 00.14 candidate; package metadata remains 00.13 pending a full promotion. Do not describe it as a finished, validated online release.
