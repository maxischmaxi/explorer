# Explorer Browser

Minimaler Webbrowser in C mit OpenGL-Rendering.

## Build

```
make          # Build
make run      # Build + starten
make dev      # Build + starten mit Debug-Port 9222
make clean    # Build-Artefakte löschen
```

## Projekt-Struktur

```
src/
  main.c              # Entry-Point, GLFW Event-Loop, Argument-Parsing
  renderer.h/c        # OpenGL Text/Rect/Image-Rendering, FreeType
  url_bar.h/c         # URL-Eingabefeld mit Clipboard, Selection, Shortcuts
  content_view.h/c    # HTML-Content-Rendering, Scrolling, Link-Hover
  html_layout.h/c     # Layout-Engine (Block, Inline, Flex), DOM-Walk
  css_engine.h/c      # CSS-Parsing + Cascade via lexbor, Computed Styles
  http.h/c            # HTTP GET mit manuellen Redirects, net_log
  fetch_manager.h/c   # Paralleles Resource-Fetching (curl_multi, 6 parallel)
  resource_fetch.h/c  # HTML-Parsing für Resource-URLs (CSS, JS, Bilder, Fonts)
  image_cache.h/c     # Bild-Dekodierung (stb_image) + GL-Textur-Cache
  js_engine.h/c       # QuickJS JavaScript-Engine
  devtools.h/c        # DevTools-Panel (Network-Tab)
  net_log.h/c         # Network-Request-Log
  scrollbar.h/c       # macOS-Style Scrollbars
  debug_server.h/c    # WebSocket Debug-Server

third_party/
  quickjs/            # QuickJS JS-Engine (statische Lib)
  lexbor/             # HTML5-Parser + CSS-Engine (statische Lib)
  stb/                # stb_image + stb_image_write (Header-only)
```

## Abhängigkeiten

- GLFW3, OpenGL, FreeType2, libcurl (via pkg-config)
- QuickJS, lexbor, stb (in third_party/)

## WebSocket Debug-Interface

Der Browser kann mit `--debug-port <port>` gestartet werden um einen WebSocket Debug-Server zu aktivieren:

```
./build/explorer --debug-port 9222
# oder:
make dev
```

### Verbindung

```javascript
const ws = new WebSocket("ws://127.0.0.1:9222");
```

### Befehle

Alle Befehle werden als JSON über WebSocket gesendet.

#### Screenshot

Erstellt einen PNG-Screenshot des aktuellen Browser-Fensters.

```json
{"command": "screenshot", "path": "/tmp/screenshots"}
```

Antwort:
```json
{"ok": true, "path": "/tmp/screenshots/screenshot_1711670400.png", "width": 1024, "height": 768}
```

Der `path`-Parameter gibt den Ordner an in dem der Screenshot abgelegt wird. Standard: `/tmp`.

#### Navigate

Navigiert den Browser zu einer URL.

```json
{"command": "navigate", "url": "https://example.com"}
```

Antwort:
```json
{"ok": true, "url": "https://example.com"}
```

## MCP Screenshot-Tool

Das Projekt enthält einen MCP-Server unter `tools/mcp-screenshot/` der über `.mcp.json` konfiguriert ist. Damit kann Claude direkt Screenshots vom Browser machen.

**Voraussetzung:** Browser muss mit `make dev` laufen (Debug-Port 9222).

**Tools:**
- `screenshot` — Macht einen Screenshot und gibt ihn als Bild zurück. Screenshots werden in `screenshots/` abgelegt.
- `navigate` — Navigiert den Browser zu einer URL. Parameter: `url` (string).

## Keyboard-Shortcuts

| Shortcut | Aktion |
|---|---|
| Ctrl+L | URL-Bar fokussieren + Text selektieren |
| F5 / Ctrl+R | Seite neu laden |
| F12 / Ctrl+Shift+I | DevTools ein/ausblenden |
| Ctrl+Q | Browser beenden |
| Escape | URL-Bar defokussieren / Laden stoppen / Beenden |
| Space / Page Down | Seite runter scrollen |
| Shift+Space / Page Up | Seite hoch scrollen |
| Home / End | Zum Seitenanfang / -ende |
| Ctrl+C/X/V | Kopieren / Ausschneiden / Einfügen (URL-Bar) |
| Ctrl+A | Alles selektieren (URL-Bar) |
| Ctrl+Backspace | Wort löschen (URL-Bar) |
