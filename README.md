# Nanobanana GUI

A small native shell for image generators that speak the OpenAI-compatible
`POST {base_url}/images/generations` API.

The UI is plain Win32; images are displayed in an embedded SFML 3.1 window.
No Electron, no Python, no browser: one exe and a few DLLs.

```
+--------------------------------------------------------------+
| Model   [ google/gemini-3.1-flash-image             v ]       |
| Prompt  [ multiline field, resizes with the window    ]       |
| Width [1024]   Height [768]   Count [1]                       |
| Quality [auto v]   Format [png v]   [ ] Transparent bg        |
| [ Generate ]   [ Open Output ]   [ Refresh ]                  |
| Output: ...\Output                                            |
| Status: Done: nb_20261026-055740_0_....png                    |
| Gallery                | Preview                              |
|   nb_...png            | (wheel to zoom, drag to pan)         |
+--------------------------------------------------------------+
```

## Features

- Model picker; API key and endpoint live in `config.ini`.
- Multiline prompt field that grows with the window.
- Explicit pixel size (sent as `size: "WxH"`), image count, quality and format.
- Transparent background (`background: transparent`).
- Every result is saved automatically to `Output` as
  `nb_<date-time>_<index>_<model>.<ext>`.
- Gallery of saved files, newest first.
- Preview: mouse wheel zooms towards the cursor, left-drag pans.
- Model, size, quality, format and other settings persist between runs.
- Works with responses that return `b64_json` or a `url` (the file is downloaded).
- Readable errors: the provider's message is shown instead of a generic failure.
- Request/error log in `nanobanana.log`.
- Proper DPI scaling, including moving the window between monitors.

### Keyboard shortcuts

| Keys | Action |
|---|---|
| `Ctrl+G` | Generate |
| `Ctrl+O` | Open the Output folder |
| `Ctrl+E` | Open `config.ini` in Notepad |
| `F5` | Reload `config.ini` |

## Requirements

- Windows 10/11, x64
- Visual Studio 2022 with the "Desktop development with C++" workload
- CMake 3.20+
- [SFML 3.1](https://www.sfml-dev.org/download/sfml/3.1.0/) for MSVC x64
  (3.x is required; 2.6 will not compile)

## Building

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DSFML_DIR="D:/libs/SFML/lib/cmake/SFML" `
      -DSFML_BIN_DIR="D:/libs/SFML/bin"
cmake --build build --config Release
```

The executable is `build\Release\Nanobanana_GUI.exe`. SFML DLLs are copied next
to it automatically.

`SFML_BIN_DIR` is optional: when omitted it is derived from `SFML_DIR`
(`<SFML_DIR>/../../../bin`). `SFML_DIR` can also come from the environment
variable of the same name. If you installed SFML with vcpkg the package is
found automatically.

## Configuration

Copy `config.example.ini` to `config.ini` and fill in at least `base_url`,
`api_key` and `models`.

```powershell
Copy-Item config.example.ini config.ini
```

`config.ini` is looked up next to the executable, then up to three levels up
the directory tree, so a single config in the repository root works while
developing. If the file is missing it is created with default values.

> **Important:** `config.ini` holds your API key and is listed in `.gitignore`.
> Do not commit it.

| Key | Meaning |
|---|---|
| `base_url` | API base URL, e.g. `https://api.openai.com/v1` or a proxy URL |
| `api_key` | Provider key, sent as `Authorization: Bearer <key>` |
| `endpoint` | Generation path, defaults to `/images/generations` |
| `models` | Comma-separated list shown in the model drop-down |
| `model` | Currently selected model (updated by the app) |
| `quality` | `auto` / `low` / `medium` / `high` |
| `output_format` | `png` / `jpeg` / `webp` |
| `output_dir` | Output folder, default `Output` (relative to the config) |
| `width`, `height` | Image size in pixels, sent as `size: "WxH"` |
| `n` | Images per request (1–10) |
| `verify_tls` | Verify the server TLS certificate (set `false` for self-signed proxies) |
| `timeout_sec` | HTTP request timeout in seconds |
| `send_response_format` | Send `response_format: b64_json` (not sent for `gpt-image*` models) |
| `transparent` | Transparent background checkbox state (updated by the app) |
| `extra_json` | Arbitrary JSON object merged into the request body |

Lines starting with `#` or `;` are comments. Unknown keys are preserved when
the app rewrites the file.

### Provider-specific parameters

Proxies expose different extensions. If a field is not in the UI, add it via
`extra_json` and it will be merged into the request body as-is:

```ini
extra_json={"aspect_ratio":"16:9","seed":42,"negative_prompt":"blur, watermark"}
```

Keys from `extra_json` override the ones the app builds itself.

### Proxy example

```ini
base_url=https://api.example.com/v1
endpoint=/images/generations
models=google/gemini-3.1-flash-image
model=google/gemini-3.1-flash-image
```

## Request and response

A typical request body:

```json
{
  "model": "google/gemini-3.1-flash-image",
  "prompt": "a ginger astronaut cat",
  "n": 1,
  "size": "1024x768",
  "quality": "high",
  "background": "transparent"
}
```

`quality` is only sent when it is not `auto`, and `output_format` only when it
is not `png`; default values are not duplicated in the request.

Both `{"data":[{"b64_json":"..."}]}` and `{"data":[{"url":"https://..."}]}`
responses are supported. Errors are read from `error.message` / `message` and
shown in the status line and in a dialog.

## Project layout

```
CMakeLists.txt          build script (MSVC x64, copies SFML DLLs post-build)
config.example.ini      sample configuration
LICENSE                 Unlicense (public domain)
rc/
  app.rc                file version resource
  resource.h            control and command identifiers
src/
  main.cpp              Win32 UI, message loop, background generation thread
  config.*              INI reading/writing, preserves unknown keys
  json.*                self-contained JSON parser and serializer
  base64.*              base64 encoder/decoder
  net.*                 URL parsing and HTTP(S) via SFML Network
  generator.*           request building, response parsing, file saving
  preview.*             embedded SFML window: display, zoom, pan
  util.*                UTF-8/UTF-16, paths, files, logging
```

There are no dependencies besides SFML: JSON and base64 are implemented in
the project.

## Troubleshooting

**"Failed to connect to server (TLS/network/proxy)"**
Check `base_url`, host reachability and proxy settings. If the server uses a
self-signed certificate, set `verify_tls=false`.

**HTTP 401 / 403**
Wrong key, or the key has no access to the selected model.

**HTTP 422 / 400**
The provider rejected a parameter. Most often it is a custom `size` — try a
standard value (e.g. `1024x1024`) or remove the field via `extra_json`.

**The app does not start: "sfml-graphics-3.dll was not found"**
The DLLs must sit next to the exe. CMake copies them automatically; for a
manual deployment copy them from `SFML/bin`.

**The UI looks too small or too large**
The app scales to the monitor DPI. Moving the window to a monitor with a
different scale recalculates sizes and fonts automatically.

## License

Released into the public domain under the [Unlicense](LICENSE).
