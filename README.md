# TyphoonLLM Online

**Experimental 00.18 — known answer-quality failures; not a reliable factual assistant.** The Vita generates every answer locally. A trusted-LAN companion decides whether the requested action needs research, retrieves documents when appropriate, and supplies optional source text. The 230M answer model can still ignore evidence or invent details. See [STATUS.md](STATUS.md) for separate implementation, automated-test and physical-validation results. Historical failures remain in [the previous audit](validation/GENERAL.md).

## One answer pipeline

1. The original user request goes to the companion unchanged. A pinned, pretrained zero-shot classifier distinguishes obtaining existing information, creating new content, transforming supplied text, social conversation, explicit research and deductions from supplied data. The deduction route requires positive classifier support; a weak match falls back to the existing action choices. This is learned action classification, not a question-prefix whitelist or entity exception. The former free-form Vita query planner has been removed.
2. Local tasks return without fetching source text. Research tasks use Wikipedia full-text search and actual pages found through public Mwmbl search. The previous source is considered for follow-ups but must still pass current-request relevance. Search snippets are never evidence.
3. A separate pinned relevance model orders search results for fetching, ranks actual documents, then ranks their complete source passages. Current-request relevance can admit a new topic independently of the previous source. Article and section context accompany Wikipedia sentences. The original request, retained conversation and admitted source text form the Vita model's input.
4. The Vita generates its own answer. The companion never generates chatbot answers. A missing, irrelevant, failed or oversized lookup leads to ordinary local generation. Empty or literal-question-echo sourced output retries once from the pre-turn conversation.

Neither classifier nor ranker is a truth checker. Public search coverage can be poor and providers can rate limit requests. The model may copy source wording or be wrong even when transport and source selection work. A source footer is not an accuracy certificate.

The client allows 45 seconds for lookup. The companion allows up to 35 seconds overall, including classification and ranking, with 25 seconds for collection. One active source worker prevents abandoned request accumulation. Stop remains available.

The 512-token context reserves space for a complete concise response and evicts whole old turns when necessary. Exact reference reuse requires the source text to remain in retained history. New Chat clears dialogue and reference state and resets companion caches; old in-flight results cannot refill the current result/document cache.

## Install

The online app uses **VLMNET001** and is separate from offline **VLMCHAT01**. Model weights, tokenizer and clock settings on the Vita are unchanged.

1. Install the online VPK with VitaShell after verifying title ID VLMNET001.
2. Reuse the unchanged model assets described by [offline TyphoonLLM](https://github.com/daivarsi/TyphoonLLM). Keep the existing `ux0:data/VitaLMChat/model.vlp`, `tokenizer.bin` and model license. Vita weights are not bundled.
3. On the PC, create a Python 3.12 virtual environment and run `python -m pip install -r retrieval_service/requirements.txt`.
4. Run `python setup-intent.py` and `python setup-relevance.py`. These download the pinned PC classifier (about 739 MB) and ranker (about 23 MB), and verify every downloaded file's SHA-256. They use the same ONNX runtime and do not execute downloaded Python code.
5. Run `python run-service.py --bind <PC_LAN_IP> --allow-ip <VITA_LAN_IP>`.
6. Put the PC's private IPv4 address in `ux0:data/TyphoonOnline/host.txt`. Allow TCP 28500 on the trusted private network if required and keep the companion running.

Update client and companion together. Triangle toggles web access. The connectivity indicator does not certify source or answer quality.

The protocol uses a peer IP allowlist, not cryptographic authentication or encryption. Use a trusted LAN and do not forward the port. User research queries are sent to public source providers; local-intent requests are classified on the PC without fetching web content.

## Build and validate

Set `VITASDK` and run `bash build-vita.sh`. Output: `build-vita/VitaLMChat.vpk`. Inspect its title ID because the historical filename is shared with offline builds.

Run `python -m unittest discover -s retrieval_service -p 'test_*.py'` for transport, bounds, fragmentation, peer access, source parsing, intent gating, cache isolation and fallback checks. Run `bash validation/run-native-tests.sh` with a host C compiler for reference retention, source echo and actual-session completion controls. These are not answer-quality tests.

Preserve all [file-specific licenses and provenance](THIRD_PARTY_NOTICES.md). No project-wide license is invented. PC models and runtime dependencies are downloaded separately; user conversations, credentials, caches and model weights are not included in source or release packages.
