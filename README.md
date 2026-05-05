<a name="readme-top"></a>
<h1 align="center">
  <a href="https://github.com/typeRYOON/tagcomposer/"><img src="resources/github/banner.png" alt="TagComposer"></a>
</h1>

<h4 align="center">Qt6 desktop app for composing image-generation prompts from a tagged dataset, with ComfyUI integration.</h4>

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
  <a href="#comfyui-preview-patch">ComfyUI Patch</a> •
  <a href="#building-from-source">Building</a> •
  <a href="#layout">Layout</a> •
  <a href="#starter-files">Starter Files</a> •
  <a href="#getting-started">Getting Started</a> •
  <a href="#usage">Usage</a> •
  <a href="#issues--feature-requests">Issues</a> •
  <a href="#other-backends">Other Backends</a> •
  <a href="#dependencies">Dependencies</a> •
  <a href="#license">License</a> •
  <a href="#contact">Contact</a>
</p>

---

## About The Project

**TagComposer** is a Qt6 / C++23 desktop app for managing a tagged image dataset and turning that dataset into prompts for ComfyUI. You build a library of entries (a character, a style, a scene), tag each image, then toggle entries into a composer that runs their tags through a configurable rule and variable pipeline before queueing the resulting prompt.

It is a personal tool first. The pipeline, the workflow editor, and the batch runner are all built around the way I generate images, but the underlying pieces (entries, rules, facets, workflows) are general enough to fit other setups.

<p align="center">
  <!-- TODO: drop the showcase GIF here once recorded. resources/github/ is the right place. -->
  <i>(showcase coming soon)</i>
</p>

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

> [!NOTE]
> **Network LoRA folders:** model hashing reads every byte to compute a SHA256, so a LoRA folder served from a remote machine (UNC share, NFS, mapped drive) can stall first-run hashing while the file streams across the wire. If your setup looks like this — or you have other network-specific requirements — please [message me](#contact) with the details (mount type, approximate file sizes, anything you've already tried). I'm collecting real-world setups to scope a remote-hashing helper.

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## ComfyUI Preview Patch

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

Make sure these imports exist near the top of `latent_preview.py` (add them if missing):

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

## Building from source

> [!NOTE]
> Windows is the primary supported platform and the only one currently tested. Linux and macOS builds are wired up in CMake but you'll be the first one through; if something doesn't work, [open an issue](#issues--feature-requests).

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
      └── onnxruntime.lib
  ```

With those in place, install **Qt 6.11+** from the Qt Maintenance Tool if you haven't already, then build with the MSVC kit:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The post-build hook copies `opencv_world4120.dll` and `onnxruntime.dll` next to the executable. Run `windeployqt6` to pull in the Qt runtime:

```sh
windeployqt6 --release --no-translations --no-quick-import --no-system-d3d-compiler --no-system-dxc-compiler --no-ffmpeg --skip-plugin-types qmltooling,multimedia,sqldrivers,assetimporters,designer,generic build/tagcomposer.exe
```

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
ONNX_VER=1.18.0
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

The post-build hook copies the `libonnxruntime*` files next to the executable, and `INSTALL_RPATH=$ORIGIN` is set so the runtime linker finds them without `LD_LIBRARY_PATH`. For redistribution beyond running out of the build tree, [`linuxdeployqt`](https://github.com/probonopd/linuxdeployqt) or AppImage / flatpak / `.deb` are the usual paths.

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
open build/tagcomposer.app
```

The CMakeLists already sets `MACOSX_BUNDLE TRUE`, so the output is a `.app` bundle. The dylib lands in `Contents/Frameworks/` and the rpath is wired up so the bundle is self-contained for local runs. For redistribution:

```sh
macdeployqt build/tagcomposer.app -dmg
```

> [!NOTE]
> Codesigning and notarization need an Apple Developer account. Without them, first-time launch on someone else's Mac requires right-click → Open. Out of scope for this README.

### Cross-platform caveats

- The frameless titlebar uses `Qt::FramelessWindowHint` plus a manual edge-resize implementation. It works on Windows, X11, and macOS. Linux **Wayland** sessions are likely to misbehave because the compositor controls window decorations there. Use an X11 session if you hit issues.

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
    ├── danmaku.txt               lines for the optional danmaku overlay
    ├── facets.fct                @category schema: facet names grouped into categories
    ├── global_tag_cache.json     cached wiki / category data from the danbooru API
    ├── groups.fct                @category blocks defining tag groups (composer category nav)
    ├── latent_sizes.txt          preset list for the LatentSize variable type
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

> [!IMPORTANT]
> Watch the walkthrough video before trying to use TagComposer end-to-end. The Usage section below is a quick reference, but the video covers the bigger picture and a lot of the small "why" decisions behind the workflow.
>
> **YouTube walkthrough**: <!-- TODO: paste the YouTube URL here once recorded -->_(coming soon)_

> [!NOTE]
> A GitHub wiki with longer-form docs (rule syntax reference, workflow JSON conventions, a recipe collection) is planned. Until it exists, this README and the in-app tooltips are the documentation.

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

If GitHub isn't a fit, the [Contact](#contact) section below has direct ways to reach me.

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Other Backends

ComfyUI is what I use personally, so it's the only backend wired up out of the box. I'm open to implementing prompt queueing and live previews for other backends (Forge, Auto1111, InvokeAI, SwarmUI, etc.). If you'd like to see one supported, open an issue with the **`enhancement`** label and include both of the following before tagging me:

1. **Confirmation that the backend exposes a controllable API.** HTTP, WebSocket, gRPC, anything. Without one, there is no way for TagComposer to queue prompts or stream previews.
2. **A starting pointer.** A link to the API docs, an example endpoint, a sample request body, anything that gets me past the "where do I even begin" stage. Since I don't use any of these myself, the more concrete pointers you can hand off the faster it gets done.

No promises on timeline, but a backend with a clear API and a willing requester goes on the realistic short list.

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Dependencies

- [`Qt 6`](https://www.qt.io/product/qt6) — Core, Gui, Widgets, Network, Concurrent
- [`OpenCV 4`](https://opencv.org/) — clip editor: flood-fill and mask ops
- [`ONNX Runtime`](https://onnxruntime.ai/) — auto-tagger: ONNX session for the tagging model
- [`ComfyUI`](https://github.com/comfyanonymous/ComfyUI) — runtime: TagComposer connects to a running instance over HTTP

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## License

Distributed under the GNU General Public License v3.0. See [`LICENSE`](LICENSE) for more information.

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>

---

## Contact

If something needs my direct attention, message me through one of the following.

- `Discord` — [typeRYOON](https://discord.com/)
- `Email` — 4ryoon@gmail.com

<p align="right"><sub>[ <a href="#readme-top">back to top</a> ]</sub></p>
