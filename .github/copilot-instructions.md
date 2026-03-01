# Copilot Instructions – InteractiveStreams

## Projektübersicht

**InteractiveStreams** ist eine modulare C++20-Aplikation, die interaktive Spiele für Stream-Zuschauer bereitstellt. Das Programm rendert die Spielgrafik, streamt sie über FFmpeg an Plattformen (Twitch, YouTube), empfängt Chat-Nachrichten und leitet diese als Spielsteuerung weiter.

---

## Architektur & Namespaces

Das Projekt nutzt den Root-Namespace `is::` mit folgenden Sub-Namespaces:

| Namespace | Verzeichnis | Verantwortlichkeit |
|-----------|-------------|-------------------|
| `is::core` | `src/core/` | Anwendungsrahmen, Konfiguration, Logging, Spiel-Verwaltung |
| `is::games` | `src/games/` | Spiel-Interface, Registry, alle Spiel-Implementierungen |
| `is::games::chaos_arena` | `src/games/chaos_arena/` | Chaos Arena-Spiel (Physik-Arena-Kampf) |
| `is::games::color_conquest` | `src/games/color_conquest/` | Color Conquest-Spiel (territoriale Eroberung) |
| `is::platform` | `src/platform/` | Chat-Plattform-Abstraktionen und -Implementierungen |
| `is::rendering` | `src/rendering/` | SFML-basiertes Rendering, Kamera, Partikel, UI |
| `is::streaming` | `src/streaming/` | FFmpeg-Encoding und RTMP-Stream-Ausgabe |
| `is::web` | `src/web/` | Eingebetteter HTTP-Server und REST-API |

---

## Code-Konventionen

### Allgemein
- **C++20** wird verwendet (Designated Initializers, `std::format` wo verfügbar, Concepts bei Bedarf)
- **Sprache**: Code auf Englisch, Kommentare auf Englisch oder Deutsch
- **Namenskonventionen**:
  - Klassen: `PascalCase` (z.B. `GameManager`, `ChaosArena`)
  - Methoden: `camelCase` (z.B. `onChatMessage`, `getState`)
  - Member-Variablen: `m_camelCase` (z.B. `m_players`, `m_physicsWorld`)
  - Konstanten: `UPPER_SNAKE_CASE` (z.B. `ARENA_WIDTH`, `MAX_PLAYERS`)
  - Namespaces: `lower_snake_case` (z.B. `chaos_arena`)
  - Dateinamen: `PascalCase.h/.cpp` (z.B. `ChaosArena.cpp`)
- **Header Guards**: Pragma once (`#pragma once`)
- **Includes**: Eigene Headers in `""`, System/Library-Headers in `<>`

### Build-System
- **CMake 3.20+** mit `FetchContent` für alle C++-Abhängigkeiten
- Quelldateien werden in Variablen gruppiert: `CORE_SOURCES`, `GAME_SOURCES`, `PLATFORM_SOURCES`, `RENDERING_SOURCES`, `STREAMING_SOURCES`, `WEB_SOURCES`
- Neue Quelldateien müssen in der entsprechenden Variablen in `CMakeLists.txt` eingetragen werden

---

## Wichtige Interfaces

### IGame (`src/games/IGame.h`)
Abstrakte Basisklasse für alle Spiele:

```cpp
class IGame {
public:
    virtual std::string id() const = 0;
    virtual std::string displayName() const = 0;
    virtual std::string description() const = 0;
    virtual void initialize() = 0;
    virtual void shutdown() = 0;
    virtual void onChatMessage(const is::platform::ChatMessage& message) = 0;
    virtual void update(double deltaTime) = 0;
    virtual void render(sf::RenderTarget& target, double interpolationAlpha) = 0;
    virtual bool isRoundComplete() const = 0;   // true wenn Runde/Phase abgeschlossen
    virtual bool isGameOver() const = 0;         // true wenn gesamtes Spiel beendet
    virtual nlohmann::json getState() const = 0;
    virtual nlohmann::json getCommands() const = 0;
};
```

Neue Spiele:
1. Erstelle Unterordner in `src/games/`
2. Erbe von `is::games::IGame`
3. Nutze `REGISTER_GAME(DeinSpiel)` Makro für automatische Registrierung
4. Keine manuelle Registrierung nötig – das Makro erzeugt einen statischen `GameRegistrar`

### IPlatform (`src/platform/IPlatform.h`)
Abstrakte Basisklasse für Chat-Plattformen:

```cpp
class IPlatform {
public:
    virtual std::string id() const = 0;
    virtual std::string displayName() const = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual std::vector<ChatMessage> pollMessages() = 0;
    virtual bool sendMessage(const std::string& text) = 0;
    virtual nlohmann::json getStatus() const = 0;
    virtual void configure(const nlohmann::json& settings) = 0;
};
```

---

## Kern-Patterns

### Auto-Registrierung (GameRegistry)
```cpp
// In deinem Spiel-Header:
#include "games/GameRegistry.h"
class MeinSpiel : public is::games::IGame { /* ... */ };
REGISTER_GAME(MeinSpiel);
```
Das `REGISTER_GAME` Makro erzeugt ein statisches `GameRegistrar`-Objekt, das beim Programmstart den Konstruktor aufruft und das Spiel beim `GameRegistry`-Singleton anmeldet.

### Pimpl-Idiom (Application)
`Application` nutzt das Pimpl-Idiom (`struct Impl`) um Kompilierungsabhängigkeiten zu reduzieren.

### Fixed Timestep Game Loop
```
while (running) {
    accumulator += frameDeltaTime;
    while (accumulator >= FIXED_TIMESTEP) {
        pollChat();
        update(FIXED_TIMESTEP);
        accumulator -= FIXED_TIMESTEP;
    }
    double alpha = accumulator / FIXED_TIMESTEP;
    render(alpha);
    encodeFrame();
}
```
Physik und Logik laufen mit festem Zeitschritt (1/60s), Rendering interpoliert mit `alpha`.

### ChatMessage Routing
1. `PlatformManager::pollAllMessages()` sammelt Nachrichten von allen verbundenen Plattformen
2. `Application` leitet sie an `GameManager::handleChatMessage()` weiter
3. `GameManager` delegiert an das aktive `IGame::onChatMessage()`
4. Das Spiel parst die Nachricht und führt den Befehl aus

### Config-System (Dotted Keys)
Zugriff über Punkt-getrennte Schlüssel:
```cpp
auto width = config.get<int>("rendering.width", 1080);
config.set("platforms.twitch.channel", "mein_kanal");
```
Die `navigate()`-Methode traversiert das JSON-Objekt entlang der Punkte.

### Spielwechsel (GameManager)
Der `GameManager` unterstützt drei Wechsel-Modi:
```cpp
enum class SwitchMode { Immediate, AfterRound, AfterGame };
gameManager->requestSwitch("color_conquest", SwitchMode::AfterRound);
```
- **Immediate**: Sofortiger Wechsel via `loadGame()`
- **AfterRound**: Pending-State gesetzt, `checkPendingSwitch()` prüft in der Main-Loop via `isRoundComplete()`
- **AfterGame**: Pending-State gesetzt, `checkPendingSwitch()` prüft via `isGameOver()`

Thread-Sicherheit: Web-API-Threads setzen den Pending-State über `std::mutex`, der eigentliche `loadGame()`-Call erfolgt nur im Main-Thread.

---

## Chaos Arena – Interna

### Spielphasen
`GamePhase::Lobby` → `Countdown` → `Battle` → `RoundEnd` → `GameOver`

### Physik (Box2D)
- `b2Vec2 gravity(0.0f, 15.0f)` – Schwerkraft nach unten (positive Y = unten in Box2D)
- `PIXELS_PER_METER = 48.0f` – Umrechnung Box2D-Meter ↔ SFML-Pixel
- Spieler sind dynamische Bodies mit Foot-Sensor für Bodenerkennung
- Projektile nutzen CCD (Continuous Collision Detection)

### Chat-Befehl-Parsing
Befehle in `ChaosArena::onChatMessage()`:
- `!join` / `!play` → Spieler hinzufügen
- `!left` / `!l` / `!a` → Spieler nach links impulsen
- `!right` / `!r` / `!d` → Spieler nach rechts impulsen
- `!jump` / `!j` / `!w` / `!up` → Springen / Doppelsprung
- `!attack` / `!hit` / `!atk` → Melee-Angriff
- `!special` / `!sp` / `!ult` → Projektil (5s Cooldown)
- `!dash` / `!dodge` → Dash mit I-Frames (3s Cooldown)
- `!block` / `!shield` / `!def` → Block aktivieren

---

## Color Conquest – Interna

### Spielphasen
`GamePhase::Lobby` → `Countdown` → `Voting` → `RoundEnd` → ... (30 Runden) → `GameOver`

### Grid-System
- 24×40 Zellen, jede gehört einem Team oder ist neutral (Owner::None)
- Teams starten in den Ecken mit 3×3 Blöcken
- Expansion: Nur Grenzzellen können erobert werden
- Rendering: Einfache Farbflächen, kein Box2D

### Chat-Befehl-Parsing
Befehle in `ColorConquest::onChatMessage()`:
- `!join [team]` / `!play` → Team beitreten (red/blue/green/yellow oder auto)
- `!up` / `!u` / `!w` / `!north` → Stimme für Expansion nach oben
- `!down` / `!d` / `!s` / `!south` → Stimme für Expansion nach unten
- `!left` / `!l` / `!a` / `!west` → Stimme für Expansion nach links
- `!right` / `!r` / `!e` / `!east` → Stimme für Expansion nach rechts
- `!emote [text]` → Team-Emote

---

## Abhängigkeiten & externe Tools

| Dependency | Zweck | Zugriff |
|-----------|-------|---------|
| SFML 2.6.1 | Rendering, Fenster | `FetchContent`, Namespace `sf::` |
| Box2D 2.4.1 | 2D-Physik | `FetchContent`, Prefix `b2` |
| nlohmann/json 3.11.3 | JSON-Konfiguration | `FetchContent`, `nlohmann::json` |
| cpp-httplib 0.15.3 | HTTP-Server | `FetchContent`, `httplib::Server` |
| spdlog 1.13.0 | Logging | `FetchContent`, globaler Logger |
| FFmpeg | RTMP-Encoding | **Extern** (muss im PATH sein), über `popen()` angesteuert |

---

## Dateien modifizieren – Checkliste

### Neues Spiel hinzufügen
1. [ ] Ordner `src/games/<spielname>/` anlegen
2. [ ] IGame-Interface implementieren mit `REGISTER_GAME` Makro (inkl. `isRoundComplete()` und `isGameOver()`)
3. [ ] Quelldateien in `CMakeLists.txt` → `GAME_SOURCES` eintragen
4. [ ] Optional: Konfigurationssektion in `config/default.json` hinzufügen
5. [ ] Optional: Spezifische Chat-Befehle in `getCommands()` zurückgeben
6. [ ] Dashboard-JS: Spiel-spezifische Phasen, Quick-Commands und Stats in `app.js` registrieren
7. [ ] README.md und TESTING.md aktualisieren (Spielbeschreibung, Befehle)
8. [ ] Git-Commit erstellen

### Neue Plattform hinzufügen
1. [ ] Ordner `src/platform/<plattform>/` anlegen
2. [ ] IPlatform-Interface implementieren (Thread-Safe `pollMessages()`)
3. [ ] In `PlatformManager.cpp` → `registerBuiltinPlatforms()` registrieren
4. [ ] Quelldateien in `CMakeLists.txt` → `PLATFORM_SOURCES` eintragen
5. [ ] Konfiguration in `config/default.json` hinzufügen
6. [ ] README.md aktualisieren
7. [ ] Git-Commit erstellen

### Rendering ändern
- Haupt-Renderer: `src/rendering/Renderer.h/cpp` – SFML-Window und RenderTexture
- Offscreen-Rendering: `Renderer::getFrameData()` liefert RGBA-Pixel für FFmpeg
- Neue visuelle Effekte: Eigene Renderer-Klasse erstellen und in `Renderer` integrieren

### Streaming ändern
- Encoder: `src/streaming/StreamEncoder.h/cpp` – FFmpeg wird via `popen` als Kindprozess gestartet
- RGBA-Frames werden über `stdin` an FFmpeg gepiped
- Neue Streaming-Ziele: In `config/default.json` unter `streaming.targets[]` hinzufügen

---

## Wichtige Hinweise

1. **Keine externen Shader**: Aktuell werden Effekte (Bloom, Vignette) in Software berechnet. GLSL-Shader sind als Erweiterung geplant.
2. **Kein Sound**: Aktuell stumm. Ein Sound-System ist für Phase 2 geplant.
3. **Pixel-Format**: Der Renderer arbeitet mit `sf::RenderTexture`, der Encoder erwartet RGBA-Pixeldaten.
4. **Thread-Sicherheit**: Plattform-Threads und Web-API-Threads nutzen `std::mutex`-geschützte Queues/States. Spielwechsel-Requests vom Web-Thread werden im Main-Thread ausgeführt.
5. **FFmpeg-Pfad**: FFmpeg wird via `popen("ffmpeg ...")` aufgerufen – muss im System-PATH sein.
6. **Config-Pfad**: Standard ist `config/default.json`, überschreibbar per CLI-Argument.
7. **Web-Dashboard**: Statische Dateien werden aus `dashboard/` neben der Executable geladen (Post-Build-Copy in CMake).
8. **Commits**: Nach jeder Änderung einen beschreibenden Git-Commit erstellen. README.md und diese Datei bei Bedarf aktualisieren.

---

## Lokales Testen

### Local Platform (`src/platform/local/LocalPlatform.h`)
Die `LocalPlatform` ist standardmäßig aktiviert und bietet drei Wege, Chat-Nachrichten lokal zu injizieren:

1. **Web-Dashboard**: `POST /api/chat` mit `{"username": "...", "text": "..."}`
   - Das Dashboard hat ein integriertes Chat-UI mit Quick-Buttons
2. **Konsolen-Input**: Direkteingabe im Terminal (optional, per Config steuerbar)
   - Format: `!befehl` oder `[username] !befehl`
3. **REST API**: Programmatisch via curl oder HTTP-Client

### Lokale Vorschau
- Der `Renderer` erstellt automatisch ein SFML-Fenster als lokale Vorschau
- Das Fenster zeigt exakt den gleichen Frame, der an FFmpeg gestreamt wird
- Auto-Skalierung bei Fenstergrößenänderung
- Schließen des Fensters = Shutdown

### Test-Workflow
```
1. Anwendung starten
2. SFML-Fenster zeigt Live-Vorschau
3. http://localhost:8080 öffnen
4. Im Dashboard "Local Chat" Befehle eingeben (!join, !attack, etc.)
5. ODER: Im Terminal direkt Befehle tippen
```
