# Include upload_bridge.js in Godot Web export

This repo uses a JS queue: `window.godotUploadQueue`.
Godot polls it via `JavaScriptBridge.eval()`.

## Option A (recommended): Custom HTML shell
1. In Godot: Project → Export…
2. Select your Web preset (or create one).
3. Enable/choose a Custom HTML Shell if available in your preset UI.
4. Copy the exported `index.html` template to your repo (or output), then add:

```html
<script src="upload_bridge.js"></script>