# Online 01.00 — URL handling and original-header restoration

## Confirmed failure and source fix

The failed identity request did reach research. A search result contained an unescaped space in its Wikipedia path. Python raised `http.client.InvalidURL`, which was outside the collector's exception handling. The source worker exited without a result, discarding collected candidates; the Vita then generated locally. This was not an intent-classification failure.

Search-result URLs and redirect targets now encode spaces/Unicode, preserve existing percent escapes and reserved delimiters, and validate length, scheme, authority and public address restrictions. HTTP protocol exceptions are isolated at collection boundaries so a bad secondary page does not discard good evidence. Cache policy is 14. There are no entity-specific URL fixes or canned answers.

The Vita's fallback label now says “No usable web context; answering locally”. The model can still make unsupported statements about its capabilities; the label describes the actual pipeline outcome.

## Original header

The exact pre-separation header text, size, font selection and positioning are restored: `Typhoon LLM`, size 32 at (75,14), with the original logo and separate connection badge. The added `Online` title word and size-28 font rendering are removed. The independent VLMNET001 app/LiveArea name, repository and model folder remain separate. AGENTS.md records this design constraint.

## Automated and local validation

- 77 companion tests passed, including URL spaces/Unicode/IDNA, existing escapes, query delimiters, redirect normalization, control/authority/size rejection, private-address blocking, failure isolation, protocol bounds/fragmentation and peer access checks.
- The existing 45 native controls and source-history filtering/commit/continuation/bounds suite passed. VitaSDK build completed with warnings.
- A live PC retest completed in about 7.14 seconds and returned a real source, rather than crashing. This single diagnostic is not a performance claim.
- Models, tokenizer, generation behavior and routing policy are unchanged from 00.20. The 44/47 routing regression result remains historical evidence; those three misses are not claimed fixed.

## Physical hardware validation

With the corrected companion and the existing 00.20 conversation retained, the exact failed request was repeated. It admitted 127 bytes of source text from Wikipedia's Kazumi Yumoto page and completed at EOS. The actual emitted token text matched the displayed answer. No New Chat/CLEAR or Vita restart separated the original failure from this companion retest.

The first name is ambiguous: this does not establish that the selected novelist was the person intended. The output also added an unsupported “prominent figure” statement. The URL crash is resolved in this case; answer accuracy is not certified by successful retrieval.

The latest conversation was saved privately before installing 01.00. Full VitaShell installation metadata and executable match the package byte-for-byte: [verification](installed-01.00.json). The original header was visually verified on the physical Vita. Five subsequent requests ran in one continuous 01.00 chat without New Chat/CLEAR: Cloud identity, its weapon follow-up, Ada Lovelace, a short rain poem and a cheerful rewrite. The first three used fetched evidence; the final two stayed local. All five ended at EOS and matched decoded emitted tokens. The Cloud answers identified the Final Fantasy series and Buster Sword. The Ada answer again lost the source qualifier, and the poem retained the “In de” typo. See [sanitized outputs](device-01.00.json) and [current screenshots](../docs/SCREENSHOTS.md). The five-turn chat remained intact and the test queue was retired.

Earlier releases and their screenshots remain unmodified historical records. The [00.20 six-turn report](ONLINE-00.20.md) and [multi-step failures](MULTISTEP-00.18.md) remain applicable. No broad accuracy, reliability or speed improvement is claimed.
