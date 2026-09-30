<a name="readme-top"></a>
<h1 align="center">
  <a href="https://github.com/typeRYOON/tagcomposer/"><img src="resources/github/banner.png" alt="TagComposer"></a>
</h1>

<h4 align="center">A concept-cluster database for ComfyUI — keep your characters, styles, and scenes in a tagged library, then turn any combination into a prompt.</h4>

<p align="center">
    <a href="https://github.com/typeRYOON/tagcomposer/commits/main">
    <img src="https://img.shields.io/github/last-commit/typeRYOON/tagcomposer.svg?style=flat-square&logo=github&logoColor=white"
         alt="GitHub last commit">
    <a href="https://github.com/typeRYOON/tagcomposer/issues">
    <img src="https://img.shields.io/github/issues-raw/typeRYOON/tagcomposer.svg?style=flat-square&logo=github&logoColor=white"
         alt="GitHub issues">
    <a href="https://github.com/typeRYOON/tagcomposer/pulls">
    <img src="https://img.shields.io/github/issues-pr-raw/typeRYOON/tagcomposer.svg?style=flat-square&logo=github&logoColor=white"
         alt="GitHub pull requests">
    <a href="https://github.com/typeRYOON/tagcomposer/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/typeRYOON/tagcomposer.svg?style=flat-square&logo=github&logoColor=white"
         alt="GitHub License">
</p>

<p align="center">
  <a href="#about-the-project">About</a> •
  <a href="#features">Features</a> •
  <a href="#prerequisites">Prerequisites</a> •
  <a href="#releases">Releases</a> •
  <a href="#building-from-source">Building</a> •
  <a href="#optional-comfyui-live-preview-patch">Live Preview Patch</a> •
  <a href="#layout">Layout</a> •
  <a href="#starter-files">Starter Files</a> •
  <a href="#getting-started">Getting Started</a> •
  <a href="#usage">Usage</a> •
  <a href="#issues--feature-requests">Issues</a> •
  <a href="#other-backends">Other Backends</a> •
  <a href="#dependencies">Dependencies</a> •
  <a href="#license">License</a> •
  <a href="#community--contact">Community</a>
</p>

---

## About The Project

**TagComposer** is for people who keep their own library of character references, style references, and scene setups for AI image generation, and want a way to actually generate from that library, not just stash references and copy-paste prompts from text files.

You build a library of entries (a character, a style, a scene), tag each image, and optionally pin a LoRA and its trigger word to the entry. Toggling an entry into the composer pulls its tags through a configurable rule and variable pipeline, drops its LoRA onto the active stack, and adds its trigger word to the prompt, then queues the result to ComfyUI. Adding a character means one click instead of remembering which LoRA goes with it, what strength to set, and what trigger phrase to paste. Built in Qt6 / C++23.

It is a personal tool first. The pipeline, the workflow editor, and the batch runner are all built around the way I generate images, but the underlying pieces (entries, rules, facets, workflows) are general enough to fit other setups.

<p align="center">
  <!-- TODO: drop the showcase GIF here once recorded. resources/github/ is the right place. -->
  <i>(showcase coming soon)</i>
</p>

> [!IMPORTANT]
> **TagComposer is for tag-based prompting** (e.g. `1girl, red dress, looking at viewer`), not natural-language captions (e.g. `a woman in a red dress`). If your model is trained on Danbooru/e621/Gelbooru-style tags, this is built for you. Caption-trained models will technically work, but the rule engine, autocomplete, and facet system all assume comma-separated tags.

> [!NOTE]
> **Other backends work for the tag and LoRA side.** Only prompt queueing and live previews are ComfyUI-specific; the entry library, tag and LoRA management, rule pipeline, and composer are all backend-agnostic. The composer has a "copy prompt" button, so you can build prompts in TagComposer and paste into Forge, Auto1111, SwarmUI, or anywhere else.

> [!NOTE]
> **Not just image generation.** The workflow JSON is whatever you put in it — TagComposer just fills `__PLACEHOLDER__` tokens and queues the result. Upscaling pipelines, ControlNet runs, video / animation workflows, mask-only previews, latent-only experiments, anything ComfyUI itself can run will work. The composer is tag-shaped, but `__positive__` is just one variable among many; wire it (or skip it) however your workflow needs.

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Features

- **Tile view** for browsing the entry library, with one-click toggling to push an entry's tags into the composer.
- **Tag pipeline** with rule actions (skip, add, replace, flag, delete), facet-based categorization, `$VAR$` substitution, and per-tag weights.
- **Saved states** that snapshot the composer's tags, weights, deactivations, rule toggles, variable values, workflow selection, and LoRA stack into a named preset with an optional preview thumbnail.
- **Workflow editor** for ComfyUI JSON templates, with typed variables (Seed, String, Integer, Float, DirSearch, LatentSize, Image, Wildcard). Wildcards pick a fresh tag bundle on every run and route it through the composer pipeline so rules and replacement vars apply.
- **Batch runner** that takes a tile-view query and queues one prompt per matched entry, with that entry's own LoRA appended to the active stack.
- **Image inputs** with a built-in clip editor for masks and crops, suitable for img2img and inpainting workflows.
- **Facet editor** for assigning tags to facets and groups so the rule engine, category nav, and prompt builder can structure them.
- **Danbooru wiki** lookup, accessible from any tag's right-click menu.
- **Output viewer** that browses ComfyUI's generated images by entry.
- **Auto-tagger** for batch tagging entries via ONNX models you drop into `data/models/`.
- **Collector** for scraping reference images into named collections, with pHash-based dedupe.

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Prerequisites

- `Windows 10 / 11` — primary supported platform, MSVC toolchain.
- `Linux / macOS` — not tested yet, check the Building from source section.
- `ComfyUI` — a running instance, local or remote, reachable over HTTP.
- `Danbooru tag CSV` — optional, used by the search-bar autocomplete.

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Releases

Pre-built Windows binaries are on the [GitHub Releases page](https://github.com/typeRYOON/tagcomposer/releases). Download the archive, extract it somewhere with write access, and run the executable. Drop in a [starter pack](#starter-files) so `data/` next to the executable is populated, then continue with [Getting Started](#getting-started).

Linux and macOS aren't yet shipped as prebuilt binaries. For those platforms, [build from source](#building-from-source).

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Building from source

> [!NOTE]
> macOS builds are untested; if something doesn't work, [open an issue](#issues--feature-requests).

Common to every platform:

1. Clone the repo
   ```sh
   git clone https://github.com/typeRYOON/tagcomposer.git
   cd tagcomposer
   ```
2. Install **Qt 6.11 or newer**. Required modules: Core, Gui, Widgets, Network, Concurrent, WebSockets.

### Windows

OpenCV and ONNX Runtime aren't checked into the repo (their prebuilt trees come out to roughly 1.24 GB combined). Grab the matching Windows prebuilts and drop them under `third_party/`:

- **OpenCV 4.12.0** → `third_party/opencv/`
  Download [`opencv-4.12.0-windows.exe`](https://github.com/opencv/opencv/releases/download/4.12.0/opencv-4.12.0-windows.exe). It's a 7-zip self-extractor: when it prompts for an extract path, point it at `third_party/`. The result should be `third_party/opencv/build/x64/vc16/bin/opencv_world4120.dll` (and friends).

- **ONNX Runtime 1.25.1** → `third_party/onnxruntime/`
  Download [`onnxruntime-win-x64-1.25.1.zip`](https://github.com/microsoft/onnxruntime/releases/download/v1.25.1/onnxruntime-win-x64-1.25.1.zip). Extract it, rename the inner `onnxruntime-win-x64-1.25.1/` folder to `onnxruntime/`, and move it into `third_party/`. The expected layout is:
  ```
  third_party/onnxruntime/
  ├── include/
  └── lib/
      ├── onnxruntime.dll
      ├── onnxruntime.lib
      └── onnxruntime_providers_shared.dll
  ```

With those in place, install **Qt 6.11+** from the Qt Maintenance Tool if you haven't already, using the **MSVC 2022 64-bit** kit. (The OpenCV pack only ships `vc16` binaries, so the build needs MSVC, not MinGW.)

You'll also need a Visual Studio C++ toolchain: either **Visual Studio 2022** or the free [**Visual Studio 2022 Build Tools**](https://visualstudio.microsoft.com/downloads/). Install the **"Desktop development with C++"** workload.

Open the **"x64 Native Tools Command Prompt for VS 2022"** from the Start menu (a regular `cmd` won't have `cl.exe` on PATH), `cd` into the project folder, then build with CMake pointed at your Qt install:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\6.11.0\msvc2022_64"
cmake --build build
```

Adjust `CMAKE_PREFIX_PATH` to match your Qt version and kit (e.g. `C:\Qt\6.12.0\msvc2022_64`).

The post-build step copies the OpenCV and ONNX Runtime DLLs next to the executable and runs `windeployqt` to pull in the Qt runtime, so `build/tagcomposer.exe` runs as is.

> [!NOTE]
> If launch fails with `VCRUNTIME140.dll was not found` (or `MSVCP140.dll`, etc.), install Microsoft's Visual C++ Redistributable: [`vc_redist.x64.exe`](https://aka.ms/vs/17/release/vc_redist.x64.exe)

### Linux

Toolchain + Qt + OpenCV from your package manager:

```sh
sudo apt install build-essential cmake ninja-build \
    qt6-base-dev qt6-websockets-dev libqt6concurrent6 \
    libopencv-dev
```

ONNX Runtime has no convenient apt package. Drop a prebuilt into `third_party/onnxruntime/`:

```sh
ONNX_VER=1.24.4
curl -L -o ort.tgz \
    https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VER}/onnxruntime-linux-x64-${ONNX_VER}.tgz
mkdir -p third_party
tar xzf ort.tgz -C third_party/
mv third_party/onnxruntime-linux-x64-${ONNX_VER} third_party/onnxruntime
```

Build:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tagcomposer
```

CMake's build-tree RPATH points at `third_party/onnxruntime/lib`, so the binary runs from the build tree without `LD_LIBRARY_PATH`. For redistribution beyond that, [`linuxdeployqt`](https://github.com/probonopd/linuxdeployqt) or AppImage / flatpak / `.deb` are the usual paths.

### macOS

Qt, OpenCV, and ONNX Runtime via Homebrew:

```sh
brew install qt opencv ninja cmake onnxruntime
```

Symlink Homebrew's ONNX Runtime into `third_party/` so the same path works as on Windows:

```sh
mkdir -p third_party
ln -s "$(brew --prefix onnxruntime)" third_party/onnxruntime
```

Build (point CMake at the Homebrew Qt prefix):

```sh
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/tagcomposer
```

The output is a plain executable, not a `.app` bundle. Bundling for redistribution isn't set up yet.

### Cross-platform caveats

- The frameless titlebar uses `Qt::FramelessWindowHint` plus a manual edge-resize implementation. It works on Windows, X11, and macOS. Linux **Wayland** sessions are likely to misbehave because the compositor controls window decorations there. Use an X11 session if you hit issues.

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Optional: ComfyUI Live Preview Patch

> [!IMPORTANT]
> Optional but recommended. Without this, TagComposer's live preview pane stays empty during generation. The patch makes ComfyUI broadcast each diffusion step's preview image over the WebSocket as a JSON message; the standard ComfyUI web client ignores the extra message, so nothing else changes for you.

In your ComfyUI install, open `ComfyUI/latent_preview.py` and replace the `prepare_callback` function with the version below:

```python
def prepare_callback(model, steps, x0_output_dict=None):
    preview_format = "JPEG"
    if preview_format not in ["JPEG", "PNG"]:
        preview_format = "JPEG"

    previewer = get_previewer(model.load_device, model.model.latent_format)

    pbar = comfy.utils.ProgressBar(steps)

    def callback(step, x0, x, total_steps):
        if x0_output_dict is not None:
            x0_output_dict["x0"] = x0

        preview_bytes = None
        if previewer:
            preview_bytes = previewer.decode_latent_to_preview_image(preview_format, x0)
            if preview_bytes:
                fmt, image, _ = preview_bytes

                buffer = io.BytesIO()
                image.save(buffer, format=fmt)

                b64 = base64.b64encode(buffer.getvalue()).decode()

                PromptServer.instance.send_sync(
                    "preview",
                    {"image": b64, "total_steps": total_steps, "step": step},
                    None
                )
        pbar.update_absolute(step + 1, total_steps, preview_bytes)

    return callback
```

Make sure these imports exist near the top of `ComfyUI/latent_preview.py` (add them if missing):

```python
import base64, io
from server import PromptServer
```

> [!NOTE]
> Restart ComfyUI after the edit so the new function is in process. A workflow reload alone won't pick it up.
>
> Re-apply this patch whenever you update ComfyUI; a `git pull` over the install will overwrite `latent_preview.py`.

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Layout

> [!NOTE]
> All user data lives under `data/` next to the executable. Entries are folders, the rest of the config is plain text and JSON so you can edit it by hand when the GUI is in your way.

```
data/
├── collections/        named scrape destinations from the Collector page
├── entry/              one folder per entry: images + __entry.json
├── models/             ONNX auto-tagger models (one folder per model)
├── states/             saved composer states (id/state.json + preview)
├── workflow_inputs/    cached image inputs uploaded to ComfyUI
├── workflows/          ComfyUI workflow JSON templates
└── system/
    ├── cluster_filters.fct       facet rule sets for dataset-helper tag clustering
    ├── danbooru.csv              danbooru tag list (search-bar autocomplete)
    ├── facets.fct                @category schema: facet names grouped into categories
    ├── groups.fct                @group blocks defining tag groups (composer category nav)
    ├── latent_sizes.txt          preset list for the LatentSize variable type
    ├── profiles.fct              named group orders and facet format sets
    ├── rules.fct                 rule engine: match expressions + actions
    ├── session.json              last-session restore: composer + LoRA state on app close
    ├── settings.json             user preferences, ComfyUI host, paths
    ├── tag_definitions.fct       per-tag facet assignments (rewritten on shutdown)
    ├── vars.fct                  $NAME$ to value variable definitions
    └── workflows.json            workflow file list + per-workflow variables
```

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Starter Files

A first-run `data/` folder is a lot of empty files. To skip that, grab one of the starter packs below and drop its contents into `data/` next to the executable.

- **Tag defs only** — populated `system/` (rules, facets, groups, vars, danbooru CSV, tag definitions) with everything else empty. Use this if you want to bring your own entries from the start.
- **Tag defs + sample entries** — the same `system/` plus a small `entry/` library so you have something to tag-toggle against while you learn the workflow.

> **Downloads**: <!-- TODO: paste the link to the starter-files folder/release here -->_(coming soon)_

> [!NOTE]
> Both packs ship the same `system/` files. The only difference is whether `entry/` is pre-populated. Drop the unzipped folder over `data/` and overwrite when prompted.

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Getting Started

### Quick start

1. **Get TagComposer.** Download a [pre-built release](#releases) on Windows, or [build from source](#building-from-source). Place the executable somewhere with write access.
2. **Drop in a [starter pack](#starter-files)** so `data/` next to the executable is populated.
3. **Configure ComfyUI.** Open the Settings page and enter your ComfyUI host (e.g. `127.0.0.1:8188`). The status dot turns green when the connection is up.
4. **Toggle an entry into the composer** from Tile View.
5. **Hit Run.** The active workflow's tokens get filled in and the prompt is queued to ComfyUI.

That's the core loop. The [Usage](#usage) section walks through the rest — saved states, the rule engine, batch runs, the workflow editor.

> [!NOTE]
> A YouTube walkthrough and a GitHub wiki (rule syntax reference, workflow JSON conventions, recipes) are planned. Until they exist, this README and the in-app tooltips are the documentation.
>
> **YouTube walkthrough**: <!-- TODO: paste the YouTube URL here once recorded -->_(coming soon)_

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Usage

> [!IMPORTANT]
> TagComposer needs a running ComfyUI instance to actually generate anything. The rest of the app (tagging, rule editing, state management) works fully offline.

A typical session:

1. **Set up ComfyUI.** Open the Settings page and enter the ComfyUI host (e.g. `127.0.0.1:8188`). Paste an API key if your instance needs one. The status dot goes green when the connection is up.
2. **Drop a workflow JSON** onto the workflow list in the composer's right sidebar. The Workflow Editor page lets you define typed variables that map to `__PLACEHOLDER__` tokens in the JSON.
3. **Build a library** in the Tile View. Each entry holds one or more images, a tag list per image, and an optional LoRA. Use the import dialog to bulk-add image folders.
4. **Push tags into the composer** by toggling an entry's "Composer Toggle" button. The pipeline expands variables, runs rules, and produces a categorized tag list on the composer page.
5. **Hit Run.** The active workflow's JSON gets its tokens filled in, `__positive__` is replaced with the rule-processed prompt, and the request is queued to ComfyUI.
6. **Save the state** (composer + workflow vars + LoRA stack) once you have a setup worth coming back to. States survive restarts and load with one click.
7. **Run a batch** from the Workflow Editor's Batch panel. Enter a tile-view query and TagComposer queues one prompt per matched entry, merging that entry's tags into the composer prompt and stacking that entry's LoRA on top.

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Issues / Feature Requests

If you hit a bug, please open a [GitHub issue](https://github.com/typeRYOON/tagcomposer/issues/new) with a short repro and, where relevant, the rule / workflow JSON involved.

For feature requests, open the issue with the **`enhancement`** label so it sorts into the right bucket.

If GitHub isn't a fit, the [Community & Contact](#community--contact) section below has direct ways to reach me.

> [!NOTE]
> **Known: network LoRA folders stall first-run hashing.** Model hashing reads every byte to compute a SHA256, so a LoRA folder served from a remote machine (UNC share, NFS, mapped drive) can take a long time on the first scan. If your setup looks like this — or you have other network-specific requirements — please [message me](#community--contact) with the details (mount type, approximate file sizes, anything you've already tried). I'm collecting real-world setups to scope a remote-hashing helper.

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Other Backends

ComfyUI is the only backend wired up so far. If you'd like to see Forge, Auto1111, InvokeAI, SwarmUI, or another backend supported, open an issue with the **`enhancement`** label and include a link to the backend's API docs and a sample request body. Backends with a clear API and an interested requester move up the list fastest.

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Dependencies

- [`Qt 6`](https://www.qt.io/product/qt6) — Core, Gui, Widgets, Network, Concurrent, WebSockets
- [`OpenCV 4`](https://opencv.org/) — auto-tagger and collector: image preprocessing and pHash dedupe
- [`ONNX Runtime`](https://onnxruntime.ai/) — auto-tagger: ONNX session for the tagging model
- [`ComfyUI`](https://github.com/comfyanonymous/ComfyUI) — runtime: TagComposer connects to a running instance over HTTP

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## License

Distributed under the GNU Affero General Public License v3.0. See [`LICENSE`](LICENSE) for more information.

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Community & Contact

- **Discord** — [Join the TagComposer server](https://discord.gg/W5jPbAGU2X) for questions, sharing setups, and feature discussion.
- **GitHub Issues** — [Bug reports and feature requests](https://github.com/typeRYOON/tagcomposer/issues).
- **Email** — for anything else: `4ryoon@gmail.com`

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>
