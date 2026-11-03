# TyphoonLLM Online

**Known serious issue:** the search planner can carry an old conversation topic into an unrelated request, retrieve the wrong document, and derail the answer. This experimental source is under audit; 00.14 has no published binary release. See [the audit](validation/GENERAL.md).

Experimental web-assisted local LLM chat for a homebrew-capable PlayStation Vita. The Vita runs LFM2.5-230M locally; a trusted LAN companion retrieves public documents. No cloud model, search API key or paid search account is required.

This is a separate app (VLMNET001) from offline TyphoonLLM (VLMCHAT01). Candidate version: 00.14. Model weights and tokenizer are unchanged. This is application/runtime work, not new model training.

## How answers work

The same local model privately proposes a short search query from the message and conversation. There is no question-prefix whitelist or category-specific final-answer template. An unusable plan falls back to searching the original message. The companion resolves documents through Wikipedia or public Mwmbl search, reads actual sources, and supplies bounded reference material. Search snippets rank results; they are not evidence.

The original user message and optional reference material go into one answer prompt. The local model generates the answer. Source URLs appear separately in smaller text. The app does not insert a source extract as the final answer. The model can nevertheless copy wording, ignore facts or invent details.

Missing, failed or oversized retrieval falls back to ordinary local generation. A successful lookup does not guarantee relevance or accuracy. Stop cancels. New chat clears conversation tokens, planner history and remembered source facts; the next lookup clears the companion source cache.

## Install

1. Install the online VPK using VitaShell. Its title ID must be VLMNET001.
2. Reuse unchanged model assets from https://github.com/daivarsi/TyphoonLLM/releases/tag/v03.05. Place model.vlp, tokenizer.bin and MODEL_LICENSE.txt in ux0:data/VitaLMChat/. Weights are not bundled in the VPK.
3. Install Python 3.12 on the PC, then run: python -m pip install -r retrieval_service/requirements.txt
4. Run: python run-service.py --bind <PC_LAN_IP> --allow-ip <VITA_LAN_IP>
5. Allow inbound TCP 28500 on your trusted private network if Windows Firewall asks. Keep the companion running. Write the PC private IPv4 address on one line in ux0:data/TyphoonOnline/host.txt.
6. Launch the app. Triangle toggles web access. The header reads Typhoon LLM with a small online/offline/checking label. Green means verified connectivity. Connectivity is checked independently of lookup success.

The companion sends queries to public providers and fetches public HTTPS pages. Keep its port on a trusted LAN; do not expose it to the Internet. Bind and allowlist actual device addresses. Providers may change or rate limit requests. No credentials are bundled.

## Build and test

Set VITASDK and run bash build-vita.sh in a compatible shell. Output: build-vita/VitaLMChat.vpk. Preserve fonts, art, notices and source subdirectories. The historical output filename is shared with offline builds: inspect title IDs before installing.

Host tests: python -m unittest discover -s retrieval_service -p 'test_*.py'

These check source reading, structured data, transport bounds, reset isolation and connectivity probes. They do not validate model accuracy. See validation/GENERAL.md for completed physical tests. Historical 00.13 checks are not evidence for this rewrite.

## Runtime and limits

The model context is 512 tokens including the answer reserve. Reference material is at most 1024 UTF-8 bytes and must fit the actual tokenizer budget. Complete code/recipe data is rejected if it cannot fit. Generic HTML selection supplies one best complete passage to reduce distraction; it does not turn an excerpt into a complete tutorial.

Fixed instruction prefixes cache attention KV and all recurrent convolution state. Private search planning checkpoints and restores live chat state. Planning adds memory and latency. Intermediate prompt vocabulary projections are skipped when their scores would be discarded; final prompt and every generated token retain full scoring. No clock override is applied.

The small model can hallucinate, copy sources, omit steps or answer irrelevantly even with correct evidence. Internet access does not fix those model limitations. No semantic correctness guarantee is made. See MODEL_AND_PROVENANCE.md, THIRD_PARTY_NOTICES.md and LICENSE_NOTES.md. No blanket project license is invented.

