# Install TyphoonLLM Online 00.20

Online is a separate app and repository. Offline TyphoonLLM is not required. Answer quality remains experimental; read [STATUS.md](STATUS.md).

## Download and verify

Get `TyphoonLLM-Online-00.20.vpk`, `TyphoonLLM-Online-00.20-source.zip`, `model.vlp`, `tokenizer.bin`, `MODEL_LICENSE.txt` and `SHA256SUMS.txt` from [the Online release](https://github.com/daivarsi/TyphoonLLM-Online/releases/tag/v00.20). All downloads are hosted in this repository's release.

Run `Get-FileHash -Algorithm SHA256 <downloaded-file>` and compare with `SHA256SUMS.txt`. Model provenance and expected hashes are in [MODEL_AND_PROVENANCE.md](MODEL_AND_PROVENANCE.md). Preserve the model license.

## Vita files

This assumes a Vita already able to install homebrew using VitaShell. No firmware, boot configuration or taiHEN changes are part of installation.

1. Create `ux0:data/TyphoonOnline/`. Copy `model.vlp`, `tokenizer.bin` and `MODEL_LICENSE.txt` there (about 245 MB total). They are separate downloads, not embedded in the VPK.
2. Stage `TyphoonLLM-Online-00.20.vpk` in that directory and install using VitaShell. Verify title **VLMNET001**, version **00.20**, name **TyphoonLLM Online**.
3. Open its own **TyphoonLLM Online** bubble. Configure the PC companion below for web retrieval.

Online stores models, settings and logs only in `ux0:data/TyphoonOnline/`. Offline **VLMCHAT01** and its data are independent. Neither app is required to install the other.

## Windows companion

The documented environment is 64-bit Python 3.12 on Windows. Use an AVX2-capable x86-64 PC for the pinned relevance artifact. Internet access is needed for dependencies, model downloads and public document retrieval.

Extract `TyphoonLLM-Online-00.20-source.zip`, then open PowerShell in its root (the directory containing `run-service.py`):

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

The 68 host checks cover implementation behavior. The last command checks 16 classifier regressions against the installed model. Neither establishes answer accuracy. The connectivity indicator confirms reachability, not a correct source or answer.

Optional `--audit` and `--cache` files can contain queries and fetched text. They are not required for normal operation; review them before sharing.

## Update an existing Online installation

1. Copy the verified `model.vlp`, `tokenizer.bin` and `MODEL_LICENSE.txt` to `ux0:data/TyphoonOnline/`. You may copy your existing matching files; leave offline originals intact.
2. Save any conversation you need, close Online and install the 00.20 VPK. This updates only **VLMNET001**. Existing Online settings remain in its data directory; chat persistence is not promised.
3. Use the companion from this release. Both the app and companion must be updated for the complete fix. Dependencies and model files are unchanged from 00.19; existing verified downloads may be reused.

The app does not fall back to the offline data directory. Missing Vita model files must be placed in Online's own folder.

## Troubleshooting

- **Missing models:** run both setup scripts or supply the model-directory flags. Do not bypass a failed checksum.
- **No connection:** check the process, addresses, `host.txt`, firewall and allowlist. Omitting `--bind` keeps the server on loopback.
- **No source or timeout:** providers may be unavailable or no useful passage may be found. The app falls back to local generation, which can be wrong.
- **Incorrect answer:** a demonstrated limitation, not proof of a setup fault. Read the physical test reports in [STATUS.md](STATUS.md).
