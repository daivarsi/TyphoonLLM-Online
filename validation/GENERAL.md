# 00.14 validation in progress

Native VitaSDK build succeeds. 28 host retrieval/transport/reset/health tests pass.
The former question-type dispatcher and canned-answer helpers are excluded.

Initial physical tests: casual rainbow wording was converted to a short query
by the local model, and the answer rephrased the source coherently. The initial
Cloud run selected extraneous trivia; generic HTML selection was then limited
to the best complete passage. The revised Cloud answer correctly identified the
Final Fantasy series. A reset accidentally invalidated the first follow-up test;
that result is not counted as a history-retention test. A corrected, zero-reset
conversation run is underway. No semantic correctness guarantee is made.

Startup health-thread creation/start and Internet connectivity are verified
before model loading. Source/planner/answer-token audits remain separate checks.
Planning added roughly 10-13 seconds in initial physical samples. No general
speed improvement is claimed for the new online pipeline.
