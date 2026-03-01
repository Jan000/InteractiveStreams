# Copilot Instructions – InteractiveStreams

## Projektübersicht

**InteractiveStreams** ist eine modulare C++20-Aplikation, die interaktive Spiele für Stream-Zuschauer bereitstellt. Das Programm rendert die Spielgrafik, streamt sie über FFmpeg an Plattformen (Twitch, YouTube), empfängt Chat-Nachrichten und leitet diese als Spielsteuerung weiter. Es unterstützt **mehrere gleichzeitige Streams** mit individuellen Spielen, Auflösungen und RTMP-Zielen sowie **mehrere Chat-Kanäle** (Twitch, YouTube, Local) gleichzeitig.

---

## Architektur & Namespaces

Das Projekt nutzt den Root-Namespace `is::` mit folgenden Sub-Namespaces:

| Namespace | Verzeichnis | Verantwortlichkeit |
|-----------|-------------|-------------------|
| `is::core` | `src/core/` | Anwendungsrahmen, Konfiguration, Logging, Stream- & Channel-Verwaltung |
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

### ChatMessage Routing (Multi-Stream)
1. `ChannelManager::pollAllMessages()` sammelt Nachrichten von allen verbundenen Kanälen und tagged sie mit `channelId`
2. `Application` iteriert über alle `StreamInstance`s
3. Jeder Stream filtert Nachrichten anhand seiner `channels[]`-Liste via `ChannelManager::filterByChannels()`
4. Gefilterte Nachrichten werden an `StreamInstance::handleChatMessage()` weitergeleitet
5. Bei Vote-Modus fängt StreamInstance `!vote`-Commands ab, ansonsten Weiterleitung an `GameManager::handleChatMessage()`
6. `GameManager` delegiert an das aktive `IGame::onChatMessage()`

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

### Multi-Stream-Architektur

#### ChannelManager (`src/core/ChannelManager.h/cpp`)
Verwaltet mehrere Plattform-Kanäle (ersetzt die alte `PlatformManager`-Einzelinstanz):
```cpp
struct ChannelConfig {
    std::string id, platform, name;
    bool enabled = true;
    nlohmann::json settings;
};
```
- **CRUD**: `addChannel()`, `updateChannel()`, `removeChannel()`, `getChannel()`
- **Verbindung**: `connectChannel()`, `disconnectChannel()`, `connectAll()`
- **Messages**: `pollAllMessages()` tagged jede Nachricht mit `channelId`
- **Filterung**: `filterByChannels()` filtert Nachrichten für einen Stream anhand seiner Channel-Subscriptions
- **Local**: `ensureLocalChannel()` erstellt immer einen "local"-Kanal für Tests
- **Serialisierung**: `loadFromJson()` / `toJson()`

#### StreamInstance (`src/core/StreamInstance.h/cpp`)
Eine einzelne Stream-Instanz mit eigenem Spiel, Rendering und Encoding:
```cpp
enum class GameModeType { Fixed, Vote, Random };
enum class ResolutionPreset { Mobile, Desktop };

struct StreamConfig {
    std::string id, name, fixedGame;
    GameModeType gameMode = GameModeType::Vote;
    ResolutionPreset resolution = ResolutionPreset::Mobile;
    std::vector<std::string> channels; // Subscribed channel IDs
    EncoderSettings encoderSettings;
    int width() const;  // 1080 or 1920
    int height() const; // 1920 or 1080
};
```
- **Lazy RenderTexture**: `ensureRenderTexture()` – RT wird erst beim ersten `render()`-Call erstellt (Thread-Safety, da Erstellung über Web-API-Thread möglich)
- **Game Mode Transitions**: `updateGameMode()` erkennt `isGameOver()` und wechselt je nach Modus:
  - Fixed: Neustart des gleichen Spiels
  - Vote: Startet Vote-Phase mit Overlay
  - Random: Wählt zufälliges nächstes Spiel
- **Vote-Overlay**: `renderVoteOverlay()` zeichnet halbtransparentes Overlay mit Spiel-Karten und Vote-Zählern
- **Streaming**: `startStreaming()` / `stopStreaming()` erstellt/zerstört `StreamEncoder(EncoderSettings)`
- **Serialisierung**: `configFromJson()` / `toJson()`

#### StreamManager (`src/core/StreamManager.h/cpp`)
Verwaltet die Kollektion aller `StreamInstance`-Objekte:
- **CRUD**: `addStream()`, `updateStream()`, `removeStream()`, `getStream()`
- **Default**: `loadFromJson()` erstellt mindestens einen "default"-Stream
- **Thread-Safety**: Alle Operationen über `std::mutex` geschützt

#### EncoderSettings (`src/streaming/StreamEncoder.h`)
```cpp
struct EncoderSettings {
    std::string ffmpegPath = "ffmpeg";
    std::string outputUrl;
    int width, height, fps = 30;
    int bitrate = 4500;
    std::string preset = "veryfast";
    std::string codec = "libx264";
};
```
Jeder Stream hat eigene EncoderSettings; `StreamEncoder` akzeptiert diesen Struct im Konstruktor.

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
| cpp-httplib 0.15.3 | HTTP-Server (Regex-Pfadmuster) | `FetchContent`, `httplib::Server` |
| spdlog 1.13.0 | Logging | `FetchContent`, globaler Logger |
| FFmpeg | RTMP-Encoding | **Extern** (muss im PATH sein), über `popen()` angesteuert |

---

## Dateien modifizieren – Checkliste

### Neues Spiel hinzufügen
1. [ ] Ordner `src/games/<spielname>/` anlegen
2. [ ] IGame-Interface implementieren mit `REGISTER_GAME` Makro (inkl. `isRoundComplete()` und `isGameOver()`)
3. [ ] Quelldateien in `CMakeLists.txt` → `GAME_SOURCES` eintragen
4. [ ] Optional: Konfigurationssektion in `config/default.json` unter `games` hinzufügen
5. [ ] Optional: Spezifische Chat-Befehle in `getCommands()` zurückgeben
6. [ ] Das Spiel erscheint automatisch im Dashboard (Streams-Tab: Spiel-Auswahl, Vote-System)
7. [ ] README.md und TESTING.md aktualisieren (Spielbeschreibung, Befehle)
8. [ ] Git-Commit erstellen

### Neue Plattform hinzufügen
1. [ ] Ordner `src/platform/<plattform>/` anlegen
2. [ ] IPlatform-Interface implementieren (Thread-Safe `pollMessages()`)
3. [ ] In `ChannelManager.cpp` → `createPlatform()` Factory erweitern
4. [ ] Quelldateien in `CMakeLists.txt` → `PLATFORM_SOURCES` eintragen
5. [ ] Plattform-spezifische Settings im Dashboard-Formular hinzufügen (index.html + app.js)
6. [ ] README.md aktualisieren
7. [ ] Git-Commit erstellen

### Rendering ändern
- Haupt-Renderer: `src/rendering/Renderer.h/cpp` – SFML-Window und Vorschau
- Jeder Stream hat eigene `sf::RenderTexture` (in `StreamInstance`)
- `Renderer::displayPreview()` zeigt die Textur eines ausgewählten Streams im Vorschau-Fenster
- Neue visuelle Effekte: Eigene Renderer-Klasse erstellen und in `StreamInstance::render()` integrieren

### Streaming ändern
- Encoder: `src/streaming/StreamEncoder.h/cpp` – FFmpeg wird via `popen` als Kindprozess gestartet
- Jeder Stream hat seinen eigenen `StreamEncoder` mit eigenen `EncoderSettings`
- RGBA-Frames werden über `stdin` an FFmpeg gepiped
- Neue Streaming-Ziele: Über das Web-Dashboard (Streams-Tab) konfigurierbar

### REST API ändern
- API-Routen: `src/web/ApiRoutes.cpp` – Alle Endpunkte registriert in `registerApiRoutes()`
- Regex-Pfadmuster: `server.Get(R"(/api/streams/([^/]+))"` – Captures über `req.matches[1]`
- Neue Endpunkte: Route hinzufügen, im `registerApiRoutes()` registrieren, JSON-Response zurückgeben

---

## Wichtige Hinweise

1. **Keine externen Shader**: Aktuell werden Effekte (Bloom, Vignette) in Software berechnet. GLSL-Shader sind als Erweiterung geplant.
2. **Kein Sound**: Aktuell stumm. Ein Sound-System ist für eine spätere Phase geplant.
3. **Pixel-Format**: Jeder Stream hat eigene `sf::RenderTexture`, der Encoder erwartet RGBA-Pixeldaten.
4. **Thread-Sicherheit**: Plattform-Threads und Web-API-Threads nutzen `std::mutex`-geschützte Queues/States. Stream/Channel-CRUD vom Web-Thread durch Mutex geschützt. RenderTextures werden lazy im Main-Thread erstellt.
5. **FFmpeg-Pfad**: FFmpeg wird via `popen("ffmpeg ...")` aufgerufen – muss im System-PATH sein.
6. **Config-Pfad**: Standard ist `config/default.json`, überschreibbar per CLI-Argument.
7. **Config-Persistenz**: `POST /api/config/save` serialisiert den Laufzeitzustand (Channels, Streams) zurück in die JSON-Datei.
8. **Web-Dashboard**: Statische Dateien werden aus `dashboard/` neben der Executable geladen (Post-Build-Copy in CMake). Dashboard ist Tab-basiert: Streams, Channels, Settings, Chat Test.
9. **Commits**: Nach jeder Änderung einen beschreibenden Git-Commit erstellen. README.md und diese Datei bei Bedarf aktualisieren.

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
- Das Fenster zeigt den Frame des ausgewählten Preview-Streams (erster Stream standardmäßig)
- `Renderer::displayPreview()` skaliert die Textur mit Letterboxing ins Fenster
- Schließen des Fensters = Shutdown

### Test-Workflow
```
1. Anwendung starten
2. SFML-Fenster zeigt Live-Vorschau des ersten Streams
3. http://localhost:8080 öffnen
4. Im Dashboard "Chat Test"-Tab Befehle eingeben (!join, !attack, etc.)
5. Im Streams-Tab neue Streams erstellen, Game-Modi wählen
6. Im Channels-Tab Kanäle hinzufügen und verbinden
7. ODER: Im Terminal direkt Befehle tippen
```
