# Discord messages

Short copy for Discord. A few variants for different channel sizes. Replace `[LINK]`. (No emoji in here - add your own to taste; Discord conventions vary by server.)

---

## Variant A - showcase / self-promo channel (medium)

**TagComposer** - a Windows desktop app I've been building that sits in front of ComfyUI and handles the boring part: organizing tags, managing LoRAs, building the prompt, queueing it, and keeping a history of what you sent.

The core trick is "facets" - you tag your tags with metadata (which character, which body part, search-only, etc.), then write rules over that metadata: "drop search-only tags before they hit the prompt", "force the hair color to X", "add an eye color if there isn't one". The composer runs your tags through those rules live and buckets the result into readable category groups.

Also in it: an entry library with a real search query language, ComfyUI workflow-variable binding (seeds / latent sizes / image inputs / wildcards), LoRA stack management with per-LoRA strengths, a session prompt history you can re-queue or restore from, an in-app Danbooru wiki, a crop/mask editor for img2img inputs, and a dataset-prep tab (Danbooru tag-cluster mining, batch auto-tagging, sidecar editing).

Windows-first, needs a running ComfyUI to point at, and the facet system is real upfront work to populate (there's a fast triage flow for it). It's a personal project so expect rough edges - feedback very welcome.

Repo / download / wiki: [LINK]

---

## Variant B - general chat / one-liner (short)

Made a thing: **TagComposer**, a cross-platform desktop frontend for ComfyUI - organized tag library + facet-aware prompt rules + LoRA management + prompt history, all queueing straight to your ComfyUI instance. Personal project, Windows-first, feedback welcome -> [LINK]

---

## Variant C - "what is it" follow-up reply (when someone asks)

It's a desktop workspace, not a generation backend - ComfyUI does the actual image gen, this is where you organize what goes into the prompt. Main pieces:

- **Tag library**: one "entry" per character/concept, with images + tags + notes + a bound LoRA. Search bar has a real query language (`has:lora`, `red hair | blue hair`, `tags:>50`, `sort:title`, etc.).
- **Composer**: tags -> facet-based rules -> a final prompt, with weights, undo/redo, live preview.
- **ComfyUI**: bind your workflow JSON's `__placeholder__` tokens to typed variables, hit Run, watch the progress over the WebSocket.
- **LoRA stack**: per-LoRA strengths, path auto-healing, wired into the workflow's lora slots.
- **History**: every prompt you push, with re-queue / save-as-state / restore-to-composer.
- **Dataset tools**: tag-cluster mining, batch auto-tagging, sidecar editing, dedup'd image collection.

Full feature breakdown is in the wiki: [LINK]
