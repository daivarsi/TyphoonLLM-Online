# Third-party notices

- Liquid AI / LFM2.5-230M: [LFM Open License v1.0](docs/notices/LFM-Open-License-1.0.txt).
- WoozyLLM (`LuckyI13I/WoozyLLM`): [pin](vita/upstream-notices/PIN.json) and [original README](vita/upstream-notices/WoozyLLM-README.md). No top-level license preserved.
- llama2.c/GGML runtime lineage: source attribution retained; [GGML license](vita/upstream-notices/GGML-LICENSE) and [pin](vita/upstream-notices/GGML-PIN.json).
- Unicode: [license](vita/upstream-notices/UNICODE-LICENSE.txt).
- Dear ImGui: historical source retained with [license](vita/upstream-notices/ImGui-LICENSE); release target uses CPU framebuffer UI.
- VitaSDK: toolchain/platform stubs and common display components. Original comments retained under vita/common; SDK not redistributed.
- Manrope, Instrument Serif and retained Inter: SIL OFL notices and font provenance in [fonts](vita/fonts/). Official URLs/hashes retained in typhoon-font-provenance.json.
- Artwork: preserved generated storm engraving and Typhoon masters in branding; [provenance](docs/ARTWORK_PROVENANCE.md). No new artwork license is invented.
- Offline facts: GeoNames CC BY 4.0, PubChem/NCBI and cited primary factual sources in [references.txt](vita/references.txt). Dated snapshot, not live geopolitical advice.

See [LICENSE_NOTES.md](LICENSE_NOTES.md). File-specific notices remain intact.

- Relevance ranking: cross-encoder/ms-marco-MiniLM-L6-v2, Apache-2.0 as declared in the exact upstream [model card](docs/notices/relevance-model-card.md). [Pinned revision and file hashes](docs/notices/relevance-model-pin.json). Model weights are downloaded separately; the model ranks source passages and never generates chatbot answers. Python dependencies retain their upstream licenses and are installed separately from pinned requirements.

- Intent classification: MoritzLaurer/deberta-v3-base-zeroshot-v2.0, MIT as declared in the exact upstream [model card](docs/notices/intent-model-card.md). [Pinned revision and file hashes](docs/notices/intent-model-pin.json). It classifies the requested action; it never generates answers. This model is downloaded separately, not redistributed in the package.
