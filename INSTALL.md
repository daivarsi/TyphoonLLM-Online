# Install experimental bundle 00.18.1

Bundle **00.18.1** updates the PC companion and documentation. The VPK remains the same verified **00.18 / VLMNET001** executable. Existing 00.18 users can update only the companion. Read [STATUS.md](STATUS.md) before evaluating answer quality.

## Download and verify

Get the VPK, `TyphoonLLM-Online-00.18.1-source.zip` and `SHA256SUMS.txt` from [this release](https://github.com/daivarsi/TyphoonLLM-Online/releases/tag/v00.18.1). Extract the source archive and open PowerShell in its root, containing `run-service.py`. The older 00.18 source archive does not include the companion follow-up.

Run `Get-FileHash -Algorithm SHA256 <downloaded-file>` and compare with the checksum file. The unchanged VPK hash is:

```text
5f6b8f8efb527905b5bb3d8bb56ea75edfe3ec0cfc3bb5bf34aad8ac380e2b53
```

## Vita files

This assumes a Vita already able to install homebrew using VitaShell. No firmware, boot configuration or taiHEN changes are part of this installation.

1. Stage `TyphoonLLM-Online-00.18.vpk` under `ux0:data/TyphoonOnline/` and install using VitaShell. Verify title **VLMNET001**, version **00.18**.
2. Keep existing model files unchanged. If absent, download [model.vlp](https://github.com/daivarsi/TyphoonLLM/releases/download/v03.05/model.vlp), [tokenizer.bin](https://github.com/daivarsi/TyphoonLLM/releases/download/v03.05/tokenizer.bin), and [MODEL_LICENSE.txt](https://github.com/daivarsi/TyphoonLLM/releases/download/v03.05/MODEL_LICENSE.txt). Copy them into `ux0:data/VitaLMChat/`. Installing the offline VPK is not required.
3. Verify model/tokenizer hashes against [MODEL_AND_PROVENANCE.md](MODEL_AND_PROVENANCE.md). They are not bundled in the online VPK.

Online **VLMNET001** is separate from offline **VLMCHAT01**. Save any conversation you want to retain before closing or reinstalling the app; persistence across restart is not promised.

## Windows companion

The documented environment is 64-bit Python 3.12 on Windows. Use an AVX2-capable x86-64 PC for the pinned relevance artifact. Internet access is needed for dependencies, model downloads and public document retrieval.

From the extracted source root:

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r retrieval_service/requirements.txt
.\.venv\Scripts\python.exe setup-intent.py
.\.venv\Scripts\python.exe setup-relevance.py
```

No environment activation is needed. The scripts download fixed revisions into `models/intent` and `models/relevance`, verify hashes, and do not execute downloaded Python code. The two main model files total about 762 MB; dependencies and other files require additional storage. Direct dependency versions are pinned, but this is not a lock of every transitive dependency.

Replace these example addresses, then start the companion:

```powershell
$pcAddress = "192.168.1.10"
$vitaAddress = "192.168.1.20"
.\.venv\Scripts\python.exe run-service.py --bind $pcAddress --allow-ip $vitaAddress
```

Keep the process running. Allow inbound TCP **28500** for Python on the trusted private network if needed. Do not forward the port. The peer IP allowlist provides no cryptographic authentication or encryption.

Create a plain ASCII/UTF-8 file containing only the PC's private IPv4 address and copy it to `ux0:data/TyphoonOnline/host.txt`. For example, the contents would be `192.168.1.10` if that is the actual address. Restart the online app after changing this configuration. Triangle toggles web access.

## Check setup separately from quality

```powershell
.\.venv\Scripts\python.exe run-service.py --help
.\.venv\Scripts\python.exe -m unittest discover -s retrieval_service -p "test_*.py"
.\.venv\Scripts\python.exe validation/check-intent.py
```

The 62 host checks cover implementation behavior. The last command checks 16 classifier regressions against the installed model. Neither establishes answer accuracy. The connectivity indicator confirms reachability, not a correct source or answer.

Optional `--audit` and `--cache` files can contain queries and fetched text. They are not required for normal operation; review them before sharing.

## Update an existing 00.18 setup

1. Stop the old companion with Ctrl+C.
2. Extract the 00.18.1 source into a separate directory. Create its virtual environment and install its requirements.
3. Run both model setup scripts, or reuse verified model directories with `--intent-model <path>` and `--ranker-model <path>`.
4. Start the new companion with the same PC/Vita addresses. If an optional cache is reused, the new policy excludes previous-version entries.

No Vita reinstall is needed for existing 00.18 users, and its current chat can remain open during this companion-only update. Older app versions must first update to the matching 00.18 VPK.

## Troubleshooting

- **Missing models:** run both setup scripts or supply the model-directory flags. Do not bypass a failed checksum.
- **No connection:** check the process, addresses, `host.txt`, firewall and allowlist. Omitting `--bind` keeps the server on loopback.
- **No source or timeout:** providers may be unavailable or no useful passage may be found. The app falls back to local generation, which can be wrong.
- **Incorrect answer:** a demonstrated limitation, not proof of a setup fault. Read the physical test reports in [STATUS.md](STATUS.md).
