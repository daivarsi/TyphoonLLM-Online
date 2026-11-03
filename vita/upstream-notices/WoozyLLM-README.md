# WoozyLLM

![WoozyLLM logo](assets/Woozyllm%20logo%20title.png)

**Local LLM chat client for PlayStation Vita** — runs a SmolLM 135M Q8_0 model entirely on-device, no network required.

- **ChatGPT-style UI** — animated transitions, message bubbles, scrollable history sidebar, system keyboard (sceImeDialog) for text input.
- **Dear ImGui rendering** — custom vita2d backend, responsive touch controls. 
- **Orange-on-black theme** — with an animated sidebar drawer, chat header, and smooth input-bar halo.

---

## Features

- Offline chat with a local LLM (SmolLM 135M, quantized to Q8_0)
- Streaming token output — UI stays responsive during generation
- Chat history saved per session in `ux0:data/woozyllm/recents/`
- Smooth slide-in/out sidebar with touch-friendly hitboxes
- Impact‑font wordmark “WoozyLLM” title
- System keyboard (sceImeDialog) with live text synchronisation
- Debug log written to `ux0:data/woozyllm.log`

---

## Building

You need **vitasdk** and **ninja**:

```sh
# 1. Clone the repo
git clone https://github.com/LuckyI13I/WoozyLLM.git
cd WoozyLLM

# 2. Configure (point VITASDK at your vita sdk root)
cmake -G Ninja -B build -DVITASDK=../vitasdk

# 3. Build
cmake --build build
```

Output artifacts:
| File | Description |
|------|-------------|
| `build/WoozyLLM.vpk` | Installable PS Vita package |
| `build/WoozyLLM.self` | Bare SELF executable |
| `build/WoozyLLM.velf` | Debuggable ELF |

---

## Setup on PS Vita

1. Install **WoozyLLM.vpk** via VitaShell or similar.
2. Copy the following files into `ux0:data/woozyllm/` on your Vita:
   - `model.bin` — the SmolLM Q8_0 weights.
   - `tokenizer.bin` — the BPE tokeniser.

   (These are **not** included in the repository; see below.)

---

## Obtaining model files

WoozyLLM uses the **SmolLM2 135M** model quantized to the Q8_0 format. The tools directory includes Python scripts to convert and quantize:

```sh
# 1. Download original Safetensors from Hugging Face
#    (HuggingFaceTB/SmolLM2-135M-Instruct)

# 2. Convert to Q8_0:
python tools/convert_smollm.py path/to/model.safetensors tools/smollm/model-q80.bin

# 3. Validate:
python tools/validate_smollm.py tools/smollm/model-q80.bin
```

Expected file sizes:
| File | Size |
|------|------|
| `model.bin` | ~137 MB |
| `tokenizer.bin` | ~1 MB |

---

## Controls

| Input | Action |
|-------|--------|
| **Touch** | Tap buttons, drag to scroll, swipe to dismiss keyboard |
| **Rear touch** | Alternate input |
| **☰ (top-left)** | Open / close sidebar with history |
| **+ New Chat** | Start a fresh session |
| **Message field** | Tap to open system keyboard |

---

## Project structure

```
WoozyLLM/
├── src/                    # Application source
│   ├── main.c              # Main loop, IME handling
│   ├── imgui_ui.cpp        # Full UI (sidebar, chat, input bar, animations)
│   ├── llama_worker.c      # Async inference thread
│   ├── llama_engine.c      # Q8_0 matmul/token sampling
│   ├── ime.c / ime.h       # sceImeDialog wrapper
│   ├── dbg.c / dbg.h       # File-based debug logger
│   ├── font_inter.c        # Embedded Inter UI font
│   ├── font_impact.c       # Embedded Impact title font
│   └── assets_icons.c      # Embedded 1bpp icon masks
├── third_party/imgui_vita2d/  # Vendored Dear ImGui + vita2d backend
├── tools/                  # Model conversion & validation scripts
│   ├── embed_icons.py      # PNG → embedded 1bpp mask generator
│   ├── convert_smollm.py   # Safetensors → Q8_0 converter
│   └── validate_smollm.py  # Byte-level model validator
├── assets/                 # UI icon source PNGs
├── sce_sys/                # Vita LiveArea assets (icon0, startup, …)
└── CMakeLists.txt          # Build configuration
```

---

## Credits

- **llama2.c** by Andrej Karpathy — reference Q8_0 engine
- **[Dear ImGui](https://github.com/ocornut/imgui)** — UI framework
- **[vita2d](https://github.com/xerpi/libvita2d)** — GPU rendering library
- **[vitasdk](https://vitasdk.org/)** — PS Vita homebrew toolchain
- Fonts: **Inter** (SIL OFL 1.1), **Impact** (Microsoft)
