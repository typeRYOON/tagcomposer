# Reddit post

Drop-in copy for r/StableDiffusion, r/comfyui, r/sdforall, r/civitai, etc. Pick a title, paste the body, attach 3-5 screenshots. Replace `[LINK]` with the repo / download page.

---

## Title options

- TagComposer - a desktop tag + prompt workspace that drives ComfyUI (organized tag library, facet-aware prompt rules, LoRA management, prompt history)
- I built a Qt app for managing tags / LoRAs / prompts and queueing them straight to ComfyUI
- TagComposer: tag your tags, write rules over the metadata, and let it build the prompt for you

## Body

I've been building **TagComposer** - a cross-platform desktop app that sits in front of a running ComfyUI instance and handles the part I always found tedious: organizing tags, managing LoRAs, and turning all that into a prompt without copy-pasting from a dozen text files.

**The core idea: facets.** You assign metadata ("facets") to individual tags - what character a tag belongs to, what body part, what clothing category, whether it's "search-only", etc. Once your tags carry that metadata, you can write **rules** over it: "whenever a hair-color tag is present, force it to X", "drop any tag marked search-only before it reaches the prompt", "if there's no eye-color tag, add one". The composer runs your active tags through that rule pipeline and buckets the result into readable category groups. It's basically a small, fast, prompt-aware transform layer that you configure once.

**What's in it:**

- **Entry library** - one "entry" per character/concept/whatever you're working on: images, tag lists, notes, an optional bound LoRA. A search bar with a real query language (`red hair, -nsfw | blue hair, sort:title`, `has:lora`, `tags:>50`, etc.) to find them.
- **Prompt composer** - tags -> rules -> variables -> a final prompt, with weights, deactivation, undo/redo, and a live preview.
- **ComfyUI integration** - select a workflow JSON, bind its `__placeholder__` variables (seeds, latent sizes, image inputs, wildcards, ...), hit Run. Watches the WebSocket for progress + preview images, pulls the final output back into an inline preview.
- **LoRA stack management** - per-LoRA model/clip strengths, auto-healing of file paths, a stack viewer wired straight into the workflow's `__lora_*__` slots.
- **Prompt history** - every prompt you push is logged for the session: inspect what was actually sent, re-queue it (same seed), save it as a named state, or restore the whole composer to that state.
- **Dataset helpers** - a tab cluster of dataset-prep tools: Danbooru tag co-occurrence mining (PMI-ranked), batch auto-tagging with ONNX taggers, a single-image .txt sidecar editor, batch tag operations, and a download-folder watcher that dedups via perceptual hash.
- **In-app Danbooru wiki** - right-click any tag to read its wiki page (with example post thumbnails) without leaving the app.
- **Image input editor** - crop/mask/trim tool for img2img / ControlNet / inpaint reference inputs.
- A full [wiki]([LINK]) covering every page.

**Honest caveats:**

- Built for use *with* ComfyUI - it's a frontend/workspace, not a generation backend. Can use with other backends, just no support for api control. You point it at a running ComfyUI instance (local or remote).
- The facet system is powerful but you have to populate it - tags don't come pre-classified (unless you use my starter files which have most common tags defined with my subjective framing of what facets make up each tag). There's a triage workflow for that (the composer flags untagged tags, the facet editor lets you blow through them fast), but it is upfront work.
- It's a personal project; expect rough edges.

Screenshots: [attach the tile view, the composer with a few category groups, the workflow editor, the prompt history page, and the dataset-helpers tag cluster]

Repo / download / wiki: **[LINK]**

Happy to answer questions in the comments. Feedback very welcome - especially on the facet/rules model, since that's the part I most want to get right.
