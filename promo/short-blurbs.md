# Short blurbs

Grab-bag of short copy for the places that don't get a full post: GitHub repo description, social bios, forum signatures, the README opening line, character-limited posts, etc. Replace `[LINK]` where it appears.

---

## One-line tagline (pick one)

- A desktop workspace for tags, LoRAs, and prompts that drives ComfyUI.
- Tag your tags, write rules over the metadata, let it build the prompt - then queue it to ComfyUI.
- Organized tag library + facet-aware prompt rules + LoRA management + prompt history, wired straight into ComfyUI.
- The prompt-building workspace ComfyUI doesn't ship with.

## GitHub repo "About" / short description (under ~120 chars)

`Desktop workspace for AI image tags, LoRAs, and prompts - facet-aware rule pipeline, ComfyUI integration, prompt history.`

## GitHub README opening paragraph

> **TagComposer** is a Qt6 / C++23 desktop app that sits in front of a running ComfyUI instance. ComfyUI does the generation; TagComposer is the workspace where you organize a tag library, manage LoRAs, build prompts through a facet-aware rule pipeline, queue them to ComfyUI, and keep a session history of what you sent. Windows-first; Linux/macOS planned. Full docs in the [wiki]([LINK]).

## Twitter / X / Bluesky (under ~280 chars)

Released **TagComposer** - a Windows desktop workspace that sits in front of ComfyUI: organized tag library with a real search query language, facet-aware prompt rules, LoRA stack management, prompt history with re-queue/restore, dataset-prep tools. Free, open. [LINK]

## Even shorter (under ~140 chars)

TagComposer: desktop tag/LoRA/prompt workspace -> ComfyUI. Facet-aware rules, search query language, prompt history, dataset tools. [LINK]

## Forum signature / footer

> TagComposer - desktop tag + prompt workspace for ComfyUI: [LINK]

## "Elevator pitch" (2-3 sentences, for a comment reply or a HN-style post)

TagComposer is a desktop app for the organize-and-prompt side of AI image generation: a tag library where you classify your tags with metadata ("facets"), a rule engine that transforms prompts based on that metadata (drop search-only tags, force a hair color, inject a missing eye color, etc.), LoRA stack management, and a session prompt history you can re-queue or roll the composer back to. It drives a running ComfyUI instance for the actual generation - select a workflow JSON, bind its variables, hit Run, watch the progress over the WebSocket. Windows-first, personal project, [docs in the wiki]([LINK]).

## Feature checklist (for a "Features" section anywhere)

- Entry library: images + tags + notes + bound LoRA per entry, with a query language (`has:lora`, `red hair | blue hair`, `tags:>50`, `sort:title`, ...).
- Facet system: per-tag metadata, a `match -> action` rule engine over it, facet-based category bucketing of the prompt.
- Prompt composer: weights, deactivation, replacement variables, 50-deep undo/redo, live preview.
- ComfyUI integration: workflow-JSON variable binding (seeds, latent sizes, image inputs, wildcards), `__positive__` / `__lora_*__` built-ins, run-time validation, WebSocket progress + preview.
- LoRA management: per-LoRA model/clip strengths, path auto-healing, stack viewer wired to workflow lora slots.
- Prompt history: session log of every push; re-queue (same seed), save as named state, restore composer.
- Image input editor: crop / mask / trim tool for img2img / ControlNet / inpaint reference inputs.
- Dataset helpers: Danbooru tag-cluster mining (PMI-ranked), batch auto-tagging (ONNX), sidecar editing, batch tag ops, dedup'd image collection.
- In-app Danbooru wiki: right-click a tag, read its wiki page with example post thumbnails.
- Import/export: move curated entry sets between machines as portable folders with facet remapping.
- Full wiki: per-page documentation at [LINK].

## Where to post (checklist)

- r/StableDiffusion, r/comfyui, r/sdforall, r/civitai
- Civitai article
- ComfyUI Discord (showcase / community-creations channel), and any AI-art Discords you're in
- GitHub repo "About" + README
- X / Bluesky / Mastodon (whichever you use)
- Hacker News "Show HN" (if you want the developer-audience angle - lean on the Qt/C++ architecture + the rule-engine design)
- Any LoRA training / dataset-prep communities (the dataset-helpers tab is the hook there)
