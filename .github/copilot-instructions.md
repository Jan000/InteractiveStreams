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
| `is::games::gravity_brawl` | `src/games/gravity_brawl/` | Gravity Brawl-Spiel (Physik-Brawler mit Gravitations-Shifts) |
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
Abstrakte Basisklasse für alle Spiele mit integriertem Text-Layout-System:

```cpp
enum class TextAlign { Left, Center, Right };

/// Beschreibt ein konfigurierbares UI-Text-Element (Positionen in % des Render-Targets)
struct TextElement {
    std::string id;              // Eindeutige ID
    std::string label;           // UI-Label im Dashboard
    float x = 50.f, y = 5.f;    // Position in % der Breite/Höhe
    int fontSize = 24;
    TextAlign align = TextAlign::Center;
    bool visible = true;
    std::string color;           // RGBA hex "#RRGGBBAA" oder "#RRGGBB", leer = Spiel-Default
};

struct ResolvedText {
    float px, py;        // Pixel-Koordinaten
    unsigned fontSize;
    TextAlign align;
    bool visible;
    std::string color;   // RGBA hex
};

class IGame {
public:
    // Identifikation
    virtual std::string id() const = 0;
    virtual std::string displayName() const = 0;
    virtual std::string description() const = 0;
    virtual int maxPlayers() const { return 0; }  // 0 = unbegrenzt

    // Font-Skalierung (per-Stream konfigurierbar)
    void setFontScale(float scale);
    float fontScale() const;

    // Lifecycle
    virtual void initialize() = 0;
    virtual void shutdown() = 0;

    // Input/Update/Render
    virtual void onChatMessage(const is::platform::ChatMessage& message) = 0;
    virtual void update(double deltaTime) = 0;
    virtual void render(sf::RenderTarget& target, double interpolationAlpha) = 0;

    // Spielstatus
    virtual bool isRoundComplete() const = 0;   // true wenn Runde/Phase abgeschlossen
    virtual bool isGameOver() const = 0;         // true wenn gesamtes Spiel beendet
    virtual nlohmann::json getState() const = 0;
    virtual nlohmann::json getCommands() const = 0;

    // Leaderboard (für In-Game-Rangliste)
    virtual std::vector<std::pair<std::string, int>> getLeaderboard() const { return {}; }

    // Per-Game Settings
    virtual void configure(const nlohmann::json& settings) {}  // Apply game-specific settings
    virtual nlohmann::json getSettings() const { return {}; }  // Return current settings

    // Chat-Feedback Callback
    using ChatFeedbackCallback = std::function<void(const std::string&)>;
    void setChatFeedback(ChatFeedbackCallback cb);

    // Text-Layout-System (Dashboard-konfigurierbar)
    const std::vector<TextElement>& textElements() const;
    void applyTextOverrides(const nlohmann::json& arr);  // Übernimmt Dashboard-Overrides
    nlohmann::json textElementsJson() const;              // Serialisiert aktuelle Elemente

protected:
    void sendChatFeedback(const std::string& message);

    // Text-Element registrieren (im Konstruktor aufrufen)
    void registerTextElement(const std::string& id, const std::string& label,
                             float x, float y, int fontSize,
                             TextAlign align = TextAlign::Center,
                             bool visible = true,
                             const std::string& defaultColor = "");

    // Hex-Farbe parsen ("#RRGGBBAA" oder "#RRGGBB" → sf::Color)
    static bool parseHexColor(const std::string& hex, sf::Color& out);

    // Element auflösen zu Pixel-Koordinaten
    ResolvedText resolve(const std::string& id, float targetW, float targetH) const;

    // Layout auf sf::Text anwenden (Position, Ausrichtung, Farbe)
    static void applyTextLayout(sf::Text& text, const ResolvedText&);
};
```

Neue Spiele:
1. Erstelle Unterordner in `src/games/`
2. Erbe von `is::games::IGame`
3. Nutze `REGISTER_GAME(DeinSpiel, "spiel_id")` Makro für automatische Registrierung
4. Keine manuelle Registrierung nötig – das Makro erzeugt einen statischen `GameRegistrar`
5. Registriere Text-Elemente im Konstruktor via `registerTextElement()`

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
    virtual nlohmann::json getCurrentSettings() { return {}; }  // Aktuelle Settings für Dashboard
};
```

### ChatMessage (`src/platform/ChatMessage.h`)
Struktur für Chat-Nachrichten und Stream-Events:

```cpp
struct ChatMessage {
    std::string platformId;       // "twitch", "youtube", "local"
    std::string channelId;        // Kanal-ID
    std::string userId;           // Plattform-spezifische User-ID
    std::string displayName;
    std::string avatarUrl;        // Optionales Profilbild (URL)
    std::string text;             // Nachrichtentext
    std::string eventType;        // Stream-Event-Typ (leer = normale Nachricht)
    int amount = 0;               // Monetärer Betrag (Micros/Cents)
    std::string currency;         // Währungscode (z.B. "USD")
    bool isModerator = false;
    bool isSubscriber = false;
    double timestamp = 0.0;

    static std::string makeUserId(const std::string& platform, const std::string& rawId);
};
```

**Event-Typen:**
| eventType | Quelle | Beschreibung |
|-----------|--------|-------------|
| `"yt_subscribe"` | YouTube gRPC/REST | Neuer Abonnent |
| `"yt_superchat"` | YouTube gRPC/REST | Super Chat (monetär) |
| `"twitch_sub"` | Twitch IRC | Neues Abo |
| `"twitch_bits"` | Twitch IRC | Bits-Spende |
| `"twitch_channel_points"` | Twitch IRC | Kanalpunkte-Einlösung |
| `""` (leer) | Alle | Normale Chat-Nachricht |

---

## Kern-Patterns

### Auto-Registrierung (GameRegistry)
```cpp
// In deinem Spiel-Header:
#include "games/GameRegistry.h"
class MeinSpiel : public is::games::IGame { /* ... */ };
REGISTER_GAME(MeinSpiel, "mein_spiel");
```
Das `REGISTER_GAME(GameClass, GameId)` Makro erzeugt ein statisches `GameRegistrar`-Objekt, das beim Programmstart den Konstruktor aufruft und das Spiel beim `GameRegistry`-Singleton anmeldet.

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

### Chat-Feedback (IGame → Chat)
Spiele können Bestätigungsnachrichten an Zuschauer senden:
```cpp
// In IGame.h:
using ChatFeedbackCallback = std::function<void(const std::string&)>;
void setChatFeedback(ChatFeedbackCallback cb) { m_chatFeedback = std::move(cb); }

// In Spiel-Code aufrufen:
sendChatFeedback("⚔️ " + name + " joined the arena!");
```
- `GameManager::setChatFeedback()` speichert den Callback und installiert ihn bei jedem `loadGame()`
- `StreamInstance`-Konstruktor setzt den Callback auf `ChannelManager::sendMessageToChannel()` für alle subscribed Channels
- Chaos Arena: Feedback bei Join und Rundengewinn
- Color Conquest: Feedback bei Join und Game Over

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
    std::vector<std::string> enabledGames; // Game filter for Vote/Random (empty = all)
    EncoderSettings encoderSettings;

    // Scoreboard Overlay
    double scoreboardCycleSecs = 10.0;   // Seconds per scoreboard panel
    double scoreboardFadeSecs = 1.0;     // Crossfade duration between panels
    int scoreboardChatInterval = 120;    // Seconds between chat scoreboard posts (0=disabled)

    // Per-Game Overrides (per Stream konfigurierbar)
    std::map<std::string, std::string> gameDescriptions;  // game_id → custom description
    std::map<std::string, std::string> gameTwitchCategories; // game_id → Twitch category
    std::map<std::string, std::string> gameYoutubeTitles;   // game_id → YouTube stream title
    std::map<std::string, float> gameFontScales;    // game_id → font scale multiplier
    std::map<std::string, int> gamePlayerLimits;    // game_id → max players
    std::map<std::string, nlohmann::json> gameTextOverrides; // game_id → text element overrides

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
- **Game Filter**: `enabledGames` filtert verfügbare Spiele im Vote/Random-Modus (leer = alle Spiele); serialisiert als `enabled_games` in JSON
- **Scoreboard Overlay**: `renderGlobalScoreboard()` zeigt Scoreboard-Panels (Recent/All-Time) mit Alpha-Crossfade. `sendScoreboardToChat()` postet formatierte Top-5 an Chat-Kanäle.
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
    std::string profile = "baseline";
    std::string tune = "zerolatency";
    int keyframeInterval = 2;
    int threads = 2;
    bool cbr = true;
    float maxrateFactor = 1.2f;
    float bufsizeFactor = 1.0f;

    // Audio
    int audioBitrate = 128;
    int audioSampleRate = 44100;
    std::string audioCodec = "aac";
    AudioMixer* audioMixer = nullptr;  // Optional: echtes Spiel-Audio
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

### Prozedurale Arena-Generierung
- `Arena::generate(world, width, height)` erzeugt ein zufälliges Layout mit Random-Seed
- `Arena::generate(world, width, height, seed)` erzeugt reproduzierbares Layout
- **Tier-System**: Die Arena wird vertikal in 6 Tiers unterteilt (Lower → Top)
- Jeder Tier hat min/max Plattform-Anzahl, Überlappungsprüfung (`overlapsExisting()`)
- Destructible Blocks werden ebenfalls prozedural platziert (5–10 Stück, variable HP)
- Boundary + Main-Platform bleiben statisch (Wände, Decke, Bodenfläche)

### Animierte Spieler-Sprites
- `SpriteAnimator::draw()` ersetzt die alten `sf::RectangleShape`-Spieler
- AnimState wird aus Spieler-Zustand abgeleitet (Velocity, Cooldowns, Blocking, HitFlash)
- Komponenten: Kopf mit Augen+Blinzel, Torso mit Gürtel, Arme mit Händen, Beine mit Schuhen
- Squash-and-Stretch bei Jump/Dash, Idle-Bob, Eye-Blink alle ~4s
- Accessoires: Schwert-Swing bei Attack, Energieschild bei Block, Speed-Lines bei Dash

### Chat-Befehl-Parsing
Befehle in `ChaosArena::onChatMessage()`:
- `!join` / `!play` → Spieler hinzufügen
- `!left` / `!l` / `!a` → Spieler nach links impulsen
- `!right` / `!r` / `!d` → Spieler nach rechts impulsen
- `!jump` / `!j` / `!w` / `!up` → Springen / Doppelsprung
- `!jumpleft` / `!jl` → Springen nach links
- `!jumpright` / `!jr` → Springen nach rechts
- `!attack` / `!hit` / `!atk` → Melee-Angriff
- `!special` / `!sp` / `!ult` → Projektil (5s Cooldown)
- `!dash` / `!dodge` → Dash mit I-Frames (3s Cooldown)
- `!block` / `!shield` / `!def` → Block aktivieren
- `!emote [text]` → Kosmetisches Emote

### Stream-Event-Handling
Bei `eventType`-Nachrichten in `handleStreamEvent()`:
- **yt_subscribe / twitch_sub**: Schild (10s) + 50 HP Heilung + 300 Punkte
- **yt_superchat / twitch_bits** (>100): Volle Heilung + Schadens-Boost (15s) + Unverwundbarkeit (5s) + 500 Punkte
- **twitch_channel_points**: Schild (5s) + Geschwindigkeits-Boost (10s) + 100 Punkte
- Events lösen automatisch `!join` aus, falls der Nutzer noch nicht im Spiel ist

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

### Stream-Event-Handling
Bei `eventType`-Nachrichten in `handleStreamEvent()`:
- **yt_subscribe / twitch_sub**: Doppelte Gebiets-Expansion in zufälliger Richtung
- **yt_superchat / twitch_bits** (>100): Doppelte Expansion in ALLEN 4 Richtungen
- Events lösen automatisch `!join` aus, falls der Nutzer noch nicht im Spiel ist

---

## Gravity Brawl – Interna

### Übersicht
Physik-basierter Orbital-Brawler mit dynamischen Gravitations-Shifts (Cosmic Events), Planeten-Tier-System und schwarzem Loch. Namespace: `is::games::gravity_brawl`. Dateien: `GravityBrawl.h/cpp`, `AvatarCache.h/cpp`.

### Spielphasen
`GamePhase::Lobby` → `Countdown` → `Playing` → `GameOver`

### Planeten-Tier-System
```cpp
enum class PlanetTier { Asteroid, IcePlanet, GasGiant, Star };
// Basierend auf Kill-Anzahl: 0→Asteroid, 3→IcePlanet, 10→GasGiant, 25→Star
// Tier bestimmt Radius, Masse und visuelle Effekte
```

### Physik
- ARENA_RADIUS = 18.0m (sicherer Orbit)
- BLACK_HOLE_RADIUS = 2.5m (Todeszone, konfigurierbar via `black_hole_radius`)
- PIXELS_PER_METER = 27.0f
- WORLD_CENTER = (20m, 35.5m)
- Orbitale Gravitation + Schwarzes-Loch-Gravitation + konfigurierbare Abstoßung (`black_hole_repulsion_strength`)

### Anomaly Pickups
```cpp
enum class AnomalyType { MassInjector, Shield, ScoreJackpot };
// Spawnen periodisch (anomaly_spawn_interval), aktivieren bei Kollision
```

### Cosmic Events (Gravitations-Shifts)
- Periodische Gravitationsänderungen während der Playing-Phase
- Richtung und Stärke der Schwerkraft ändern sich dynamisch
- Spieler müssen sich an neue Physik-Bedingungen anpassen
- Konfigurierbar: `cosmic_event_cooldown`, `cosmic_event_duration`, `event_gravity_mul`

### Bot Fill System
- Füllt die Lobby automatisch mit KI-Bots auf, um einen Mindest-Spielerstand zu erreichen
- Bots wählen zufällige Aktionen (Smash mit konfigurierbarer Wahrscheinlichkeit)
- Konfigurierbar per Game-Settings: `bot_fill` (int), `min_players` (int)
- **Kill Feed**: `bot_kill_feed` (bool, Default: false) – Bots erscheinen standardmäßig nicht im Kill-Feed
- **Respawn**: `bot_respawn` (bool, Default: false) – tote Bots respawnen nach `bot_respawn_delay` Sekunden (Default: 5.0)
- **Verhalten**: `bot_action_interval` (float, Default: 0.3s), `bot_smash_chance` (float, Default: 0.2), `bot_danger_smash_chance` (float, Default: 0.6), `bot_event_smash_chance` (float, Default: 0.7)

### Dynamic Camera Zoom
- Kamera zoomt dynamisch, um alle Spieler sichtbar zu halten
- Zoomt rein wenn Spieler nah beieinander (`camera_max_zoom`, Default: 2.0), raus wenn weit verstreut (`camera_min_zoom`, Default: 0.4)
- `camera_zoom_enabled` (bool, Default: true), `camera_zoom_speed` (float, Default: 2.0), `camera_buffer_meters` (float, Default: 3.0)
- `camera_zoom_amplify` (float 1–5, Default: 2.0): Verstärkt die Abweichung vom neutralen Zoom (1.0), damit der Effekt auf dem Stream deutlich sichtbar wird
- Alle Einstellungen im Web-Dashboard konfigurierbar

### Sound Effects (SFX)
- SFX-Dateien werden beim `initialize()` aus `assets/audio/sfx/gravity_brawl/` geladen
- Unterstützte Formate: `.wav`, `.ogg`, `.mp3` (erste gefundene Extension wird verwendet)
- Falls eine Datei fehlt, wird der Effekt übersprungen (graceful degradation)
- Geladen via `AudioManager::loadSfx()`, abgespielt via `AudioManager::playSfx()`
- Per-Game Settings: `sfx_enabled` (bool, Default: true), `sfx_volume` (float 0–100, Default: 80)

| SFX-Name | Event | Datei |
|----------|-------|-------|
| `gb_join` | Spieler tritt bei | `gb_join.wav` |
| `gb_smash` | Smash-Angriff | `gb_smash.wav` |
| `gb_supernova` | Supernova-Explosion | `gb_supernova.wav` |
| `gb_hit` | Planeten-Kollision | `gb_hit.wav` |
| `gb_death` | Planet eliminiert | `gb_death.wav` |
| `gb_kill` | Kill-Attribution | `gb_kill.wav` |
| `gb_bounty` | Bounty-Kill (King) | `gb_bounty.wav` |
| `gb_cosmic_event` | Cosmic Event Start | `gb_cosmic_event.wav` |
| `gb_cosmic_end` | Cosmic Event Ende | `gb_cosmic_end.wav` |
| `gb_countdown` | Countdown startet | `gb_countdown.wav` |
| `gb_battle_start` | Kampf beginnt | `gb_battle_start.wav` |
| `gb_game_over` | Spiel vorbei | `gb_game_over.wav` |

### Per-Game Settings
- Konfigurierbare Parameter über `configure()` / `getSettings()`
- Gespeichert unter `game_settings.gravity_brawl` in Config
- API: `GET/PUT /api/games/gravity_brawl/settings`
- Enthält u.a. `sfx_enabled`, `sfx_volume`, Bot-Verhalten, Kamera-Zoom

### Chat-Befehl-Parsing
Befehle in `GravityBrawl::onChatMessage()`:
- `!join [farbe]` / `!play` → Spieler hinzufügen (Farbe optional: red, blue, green, yellow, #RRGGBB)
- `!s` / `!smash` → Dash/Ram-Angriff (0.8s Cooldown)
- 5 aufeinanderfolgende Smashes innerhalb 3s → Supernova (großer AoE-Knockback)

### Stream-Event-Handling
Bei `eventType`-Nachrichten wird `triggerLivestreamReward()` aufgerufen:
- **yt_subscribe / twitch_sub**: Schild + Tier-Bonus + 300 Punkte
- **twitch_channel_points**: Supernova-Auslösung
- **yt_superchat / twitch_bits** (>100): God Mode (30s) + 3× Masse
- Events lösen automatisch `!join` aus, falls der Nutzer noch nicht im Spiel ist

### Per-Game Settings
- Konfigurierbare Parameter über `configure()` / `getSettings()`
- Gespeichert unter `game_settings.gravity_brawl` in Config
- API: `GET/PUT /api/games/gravity_brawl/settings`
- Enthalt u.a.: `epoch_duration`, `enable_post_processing`, `background_stars`, Physik-Settings (`orbital_gravity_strength`, `black_hole_gravity_strength`, `restitution`, `black_hole_radius`, `black_hole_repulsion_strength`), Gameplay (`smash_cooldown`, `supernova_radius`, `max_speed`, `respawn_cooldown`), Audio (`sfx_enabled`, `sfx_volume`), Kamera-Zoom, Bot-Verhalten, Anomalien (`anomaly_spawn_interval`) und ~40+ weitere Parameter

---

## SettingsDatabase (`src/core/SettingsDatabase.h/cpp`)

SQLite-basierte persistente Speicherung **aller** Einstellungen (ersetzt die alte rein-JSON-basierte Persistenz).

### Architektur
- **Datei**: `data/settings.db` (WAL-Modus, Thread-Safe via Mutex)
- **Tabelle**: `settings` mit `key TEXT PRIMARY KEY, data TEXT NOT NULL`
- **Keys**: `config` (globale Settings ohne Channels/Streams), `channels` (JSON-Array), `streams` (JSON-Array), `profiles` (JSON-Array), `auth_password` (JSON-Objekt: `{hash, salt}`)

### Persistenz-Flow
1. **Startup**: SQLite wird als primäre Quelle geladen. Falls leer (Erststart), wird aus `config/default.json` migriert.
2. **Auto-Save**: Jede API-Mutation (CRUD auf Channels/Streams, Settings-Änderungen) persistiert automatisch in SQLite.
3. **Shutdown**: Alle Einstellungen werden nochmals in SQLite gesichert (Safety-Net).
4. **Backup**: `POST /api/config/save` schreibt zusätzlich die JSON-Datei als menschenlesbares Backup.

### Application-Hilfsmethoden
- `app.persistChannels()` – Speichert alle Channels in SQLite
- `app.persistStreams()` – Speichert alle Streams in SQLite
- `app.persistGlobalConfig()` – Speichert globale Config (ohne Channels/Streams) in SQLite

### Migration
Beim Erststart (SQLite leer) werden die Daten einmalig aus `config/default.json` importiert.
Danach ist SQLite die alleinige Source of Truth.

---

## PlayerDatabase (`src/core/PlayerDatabase.h/cpp`)

SQLite-basierte persistente Spieler-Datenbank für Scoreboard-Funktionalität.

### Tabellen
- `players` – user_id (PK), display_name, total_points, total_wins, games_played, first_seen, last_seen
- `game_results` – id (autoincrement), user_id (FK), game_name, points, is_win, timestamp

### Scoring-Hooks
Spiele rufen `Application::instance().playerDatabase().recordResult()` auf:
- **Chaos Arena**: +100 Punkte + Win bei Rundengewinn, +25 Punkte bei Kill, +1 Punkt für Teilnahme
- **Gravity Brawl**: Gleiche Scoring-Regeln wie Chaos Arena
- **Color Conquest**: +50 Punkte + Win für Gewinner-Team, +5 Punkte für Teilnahme

### API
- `GET /api/scoreboard/recent?limit=10&hours=24` – Top-Spieler der letzten N Stunden
- `GET /api/scoreboard/alltime?limit=5` – All-Time-Leaderboard
- `GET /api/scoreboard/player/:id` – Einzelne Spieler-Statistiken

---

## PerfMonitor (`src/core/PerfMonitor.h/cpp`)

Performance-Metriken mit Ring-Buffer (max 3600 Samples).

### Erfasste Metriken
- FPS, Frame-Time (ms), CPU-Auslastung (Windows: `GetProcessMemoryInfo`), RAM (MB)
- Aktive Streams, verbundene Channels, Gesamtspielerzahl

### Sampling
- Alle 5 Frames ein Sample, Ring-Buffer mit Verwurf ältester Einträge
- `toJson()` dünnt auf ~120 Datenpunkte für Charts aus

### API
- `GET /api/perf?seconds=60` – Durchschnittswerte
- `GET /api/perf/history?seconds=300` – Zeitreihen für Graphen

---

## PostProcessing (`src/rendering/PostProcessing.h/cpp`)

GPU-beschleunigtes Post-Processing mit GLSL-Fragment-Shadern und Software-Fallbacks.

### Architektur
- **Shader-Dateien**: `assets/shaders/` (vignette.frag, bloom.frag, chromatic_aberration.frag, crt.frag)
- **Laden**: `initialize()` prüft `sf::Shader::isAvailable()` und lädt alle Shader via `loadShader()`
- **Multi-Pass**: `applyShaderPass()` zeichnet die Quelle durch einen Shader auf `m_tempTarget` (temporäre RenderTexture), dann blit zurück – nötig weil SFML nicht auf die eigene Textur durch einen Shader zeichnen kann

### Effekte
| Methode | GPU-Shader | Software-Fallback | Uniforms |
|---------|-----------|-------------------|----------|
| `applyVignette()` | ✅ | ✅ (Pixel-Manipulation) | `intensity` |
| `applyBloom()` | ✅ | ❌ (No-Op) | `threshold`, `intensity`, `resolution` |
| `applyChromaticAberration()` | ✅ | ❌ (No-Op) | `amount`, `resolution` |
| `applyCRT()` | ✅ | ❌ (No-Op) | `scanlineIntensity`, `curvature`, `resolution` |
| `applyScanlines()` | ❌ | ✅ (Software-only) | — |

### Nutzung in Spielen
```cpp
// In ChaosArena::render():
if (auto* rt = dynamic_cast<sf::RenderTexture*>(&target)) {
    m_postProcessing.applyBloom(*rt, 0.35f);
    m_postProcessing.applyChromaticAberration(*rt, 1.5f);
    m_postProcessing.applyVignette(*rt, 0.4f);
}
```

---

## WebServer-Authentifizierung (`src/web/WebServer.h/cpp`)

Zwei Auth-Mechanismen: **API-Key** (legacy) und **Password-Login** (Session-basiert).

### API-Key Auth (Legacy)
- **Config-Key**: `web.api_key` (leer = API-Key-Auth deaktiviert)
- **Reload**: `reloadApiKey()` liest den Key aus der Config
- Key wird geprüft: `Authorization: Bearer <key>`, `X-API-Key: <key>`, oder `?api_key=<key>`

### Password-Login (Session-basiert)
- **Sha256** (`src/core/Sha256.h`): Header-only SHA-256 Implementierung mit `sha256()`, `hashPassword()`, `generateSalt()`, `generateToken()`
- **Passwort-Speicherung**: Als `auth_password` Key in SettingsDatabase (JSON: `{hash, salt}`)
- **Session-Management**: `WebServer` hat `addSession()`, `removeSession()`, `hasSession()`, `isPasswordAuthEnabled()` mit `m_sessions` (`std::unordered_set<std::string>`) + Mutex
- **CLI-Flag**: `--reset-password` löscht das Passwort aus der SettingsDB (für Recovery)

### Auth-Endpunkte (`ApiRoutes.cpp`)
- `GET /api/auth/status` – Prüft ob Auth eingerichtet ist und ob Session gültig
- `POST /api/auth/setup` – Erstmalige Passwort-Einrichtung
- `POST /api/auth/login` – Login mit Passwort, gibt Session-Token zurück
- `POST /api/auth/logout` – Session-Token invalidieren
- `POST /api/auth/change-password` – Passwort ändern (erfordert aktive Session)

### Auth-Flow (`setupAuth()`)
1. Pre-Routing-Handler prüft: Ist der Request ein `/api/`-Pfad?
2. OPTIONS-Requests (CORS-Preflight) werden immer durchgelassen
3. Statische Dateien (Dashboard) werden immer ausgeliefert
4. `/api/auth/*`-Pfade werden immer durchgelassen
5. Auth wird geprüft in dieser Reihenfolge:
   - API-Key: `Authorization: Bearer <key>`, `X-API-Key`, `?api_key=`
   - Session-Token: `Authorization: Bearer <session_token>` (via `hasSession()`)
6. Wenn weder API-Key gesetzt noch Passwort eingerichtet → kein Auth, alles durchlassen
7. Bei Fehlschlag: HTTP 401 JSON-Response

### Dashboard-Integration
- `web/src/app/login/page.tsx`: Login-Seite mit Setup- und Login-Modus
- `web/src/components/app-shell.tsx`: Auth-Guard prüft `/api/auth/status` beim Mount, leitet zu Login/Setup um
- `web/src/lib/api.ts`: `authHeaders()` erzeugt `Authorization: Bearer`-Header (Token aus localStorage)
- `request()` fügt Auth-Headers automatisch zu allen API-Calls hinzu

---

## Docker & CI/CD

### Dockerfile (Multi-Stage)
1. **Stage 1** (`dashboard-builder`): `oven/bun:1` – `bun install && bun run build` → erzeugt `web/out/`
2. **Stage 2** (`cpp-builder`): `ubuntu:24.04` + CMake + Dependencies – kompiliert nur die Binary (keine Assets/Config/Dashboard)
3. **Stage 3** (`runtime`): `ubuntu:24.04` + FFmpeg + Xvfb – kopiert Binary aus cpp-builder, Assets/Config direkt aus Build-Context, Dashboard aus dashboard-builder. COPY-Layer nach Änderungshäufigkeit sortiert (Config → Assets → Dashboard → Binary) für optimales Caching.

### docker-compose.yml
- Service `app` auf Port 8080
- Named Volume `is-data` für SQLite-Persistenz (`data/`)
- Config-Bind-Mount für `config/default.json`

### GitHub Actions (`.github/workflows/ci.yml`)
4 Jobs, getriggert auf Push/PR zu `main`:
1. **dashboard**: Bun install + build, Dashboard-Artifact hochladen
2. **build-linux**: Ubuntu + apt deps + CMake + CTest
3. **build-windows**: MSVC + CMake + CTest (Release-Config)
4. **docker**: Docker-Image bauen (ohne Push)

---

## Abhängigkeiten & externe Tools

| Dependency | Zweck | Zugriff |
|-----------|-------|---------|
| SFML 2.6.1 | Rendering, Fenster, Audio | `FetchContent`, Namespace `sf::`, Module: graphics, window, system, network, audio |
| Box2D 2.4.1 | 2D-Physik | `FetchContent`, Prefix `b2` |
| nlohmann/json 3.11.3 | JSON-Konfiguration | `FetchContent`, `nlohmann::json` |
| cpp-httplib 0.15.3 | HTTP-Server (Regex-Pfadmuster) | `FetchContent`, `httplib::Server` |
| spdlog 1.13.0 | Logging | `FetchContent`, globaler Logger |
| SQLite3 3.45.3 | Spieler-Datenbank (Scoreboard) | `FetchContent` (URL-Download Amalgamation), `sqlite3_lib` |
| FFmpeg | RTMP-Encoding | **Extern** (muss im PATH sein), über `popen()` angesteuert |

### Frontend (Web-Dashboard)

| Dependency | Zweck | Zugriff |
|-----------|-------|--------|
| Next.js 16.1.6 | React-Framework, Static Export | `bun install`, `web/` |
| TypeScript | Typsicherheit | In Next.js integriert |
| shadcn/ui + Radix UI | UI-Komponenten | `web/src/components/ui/` |
| Tailwind CSS v4 | Styling | `web/postcss.config.mjs` |
| Recharts 3.7.0 | Performance-Graphen | Import in Dashboard-Seiten |
| Bun | Package-Manager & Build-Tool | `bun install`, `bun run build` |

Das Dashboard wird als statischer Export (`web/out/`) erzeugt und beim CMake-Build nach `dashboard/` neben die Executable kopiert.

**Dashboard-Build:** `cd web && bun install && bun run build`

---

## Dateien modifizieren – Checkliste

### Neues Spiel hinzufügen
1. [ ] Ordner `src/games/<spielname>/` anlegen
2. [ ] IGame-Interface implementieren mit `REGISTER_GAME` Makro (inkl. `isRoundComplete()` und `isGameOver()`)
3. [ ] Quelldateien in `CMakeLists.txt` → `GAME_SOURCES` eintragen
4. [ ] Optional: Konfigurationssektion in `config/default.json` unter `game_settings.<game_id>` hinzufügen
5. [ ] Optional: `configure()` und `getSettings()` implementieren für per-game Settings
6. [ ] Optional: Spezifische Chat-Befehle in `getCommands()` zurückgeben
7. [ ] Das Spiel erscheint automatisch im Dashboard (Streams-Tab: Spiel-Auswahl, Vote-System)
8. [ ] README.md und TESTING.md aktualisieren (Spielbeschreibung, Befehle)
9. [ ] Git-Commit erstellen

### Neue Plattform hinzufügen
1. [ ] Ordner `src/platform/<plattform>/` anlegen
2. [ ] IPlatform-Interface implementieren (Thread-Safe `pollMessages()`)
3. [ ] In `ChannelManager.cpp` → `createPlatform()` Factory erweitern
4. [ ] Quelldateien in `CMakeLists.txt` → `PLATFORM_SOURCES` eintragen
5. [ ] Plattform-spezifische Settings im Dashboard-Formular hinzufügen (`web/src/components/channel-card.tsx`)
6. [ ] README.md aktualisieren
7. [ ] Git-Commit erstellen

### Rendering ändern
- Haupt-Renderer: `src/rendering/Renderer.h/cpp` – SFML-Window und Vorschau
- Jeder Stream hat eigene `sf::RenderTexture` (in `StreamInstance`)
- `Renderer::displayPreview()` zeigt die Textur eines ausgewählten Streams im Vorschau-Fenster
- **SpriteAnimator**: `src/rendering/SpriteAnimator.h/cpp` – Prozeduraler Charakter-Renderer mit Kopf, Körper, Armen, Beinen. Animationszustände: Idle, Walk, Jump, Attack, Dash, Block, Hit, Death. Ersetzt die alten `sf::RectangleShape`-Spieler in ChaosArena. Zeichnet Accessoires (Schwert bei Attack, Schild bei Block, Speed-Lines bei Dash). Squash-and-Stretch, Eye-Blink, Idle-Bob.
- **Post-Processing**: `src/rendering/PostProcessing.h/cpp` – GLSL-Shader in `assets/shaders/` (Vignette, Bloom, Chromatic Aberration, CRT). `applyShaderPass()` nutzt eine temporäre RenderTexture für Multi-Pass-Rendering. Software-Fallback für Vignette wenn keine Shader verfügbar.
- Neue visuelle Effekte: Neuen Shader in `assets/shaders/` erstellen, in `PostProcessing` laden und in `StreamInstance::render()` oder Spiel-`render()` aufrufen

### Streaming ändern
- Encoder: `src/streaming/StreamEncoder.h/cpp` – FFmpeg wird via `popen` als Kindprozess gestartet
- Jeder Stream hat seinen eigenen `StreamEncoder` mit eigenen `EncoderSettings`
- RGBA-Frames werden über `stdin` an FFmpeg gepiped
- Neue Streaming-Ziele: Über das Web-Dashboard (Streams-Tab) konfigurierbar

### REST API ändern
- API-Routen: `src/web/ApiRoutes.cpp` – Alle Endpunkte registriert in `registerApiRoutes()`
- Regex-Pfadmuster: `server.Get(R"(/api/streams/([^/]+))"` – Captures über `req.matches[1]`
- Neue Endpunkte: Route hinzufügen, im `registerApiRoutes()` registrieren, JSON-Response zurückgeben
- Per-Game Settings: `GET/PUT /api/games/:id/settings` für spielspezifische Konfiguration
- Auth-Endpunkte: `/api/auth/*` für Login/Session-Management
- Config Export/Import: `GET /api/config/export`, `POST /api/config/import`
- Profiles: `GET/POST /api/profiles`, `GET/PUT/DELETE /api/profiles/:id`

---

## Wichtige Hinweise

1. **GLSL-Shader**: Post-Processing nutzt GPU-Shader (`assets/shaders/`) mit Software-Fallback. `PostProcessing::initialize()` lädt alle Shader; `applyShaderPass()` rendert über temporäre `sf::RenderTexture`. Verfügbare Effekte: Vignette, Bloom, Chromatic Aberration, CRT, Scanlines.
2. **Sound-System**: `AudioManager` (`src/core/AudioManager.h/cpp`) verwaltet Hintergrundmusik (Playlist mit Shuffle, auto-advance) und SFX. Musikdateien werden aus `assets/audio/` gescannt (.mp3/.ogg/.wav/.flac). Neue Tracks einfach ins Verzeichnis legen und `rescan()` aufrufen. Web-API: `GET/PUT /api/audio`, `POST /api/audio/next|pause|resume|rescan`. Config-Keys: `audio.music_volume`, `audio.sfx_volume`, `audio.muted`, `audio.fade_in_seconds`, `audio.fade_out_seconds`, `audio.crossfade_overlap`.
3. **Pixel-Format**: Jeder Stream hat eigene `sf::RenderTexture`, der Encoder erwartet RGBA-Pixeldaten.
4. **Thread-Sicherheit**: Plattform-Threads und Web-API-Threads nutzen `std::mutex`-geschützte Queues/States. Stream/Channel-CRUD vom Web-Thread durch Mutex geschützt. RenderTextures werden lazy im Main-Thread erstellt.
5. **FFmpeg-Pfad**: FFmpeg wird via `popen("ffmpeg ...")` aufgerufen – muss im System-PATH sein.
6. **Config-Pfad**: Standard ist `config/default.json`, überschreibbar per CLI-Argument.
7. **Config-Persistenz**: Alle Einstellungen werden automatisch in `data/settings.db` (SQLite) persistiert. `POST /api/config/save` schreibt zusätzlich eine JSON-Backup-Datei. SQLite ist die primäre Source of Truth; `config/default.json` dient nur als Erst-Migrations-Template.
8. **Web-Dashboard**: Statische Dateien werden aus `dashboard/` neben der Executable geladen (Post-Build-Copy in CMake). Dashboard ist ein Next.js-Static-Export (`web/`), gebaut mit `bun run build`. Tab-basiert: Streams, Channels, Statistics, Scoreboard, Performance, Settings. **Immer `bun` verwenden** (nicht npm/yarn).
9. **API-Authentifizierung**: Zwei Auth-Mechanismen: API-Key (legacy, `web.api_key`) und Password-Login (Session-basiert). `setupAuth()` Pre-Routing-Handler prüft beide. `/api/auth/*` immer erlaubt. `Sha256.h` für Passwort-Hashing. `--reset-password` CLI-Flag für Recovery.
10. **Docker**: Multi-Stage Dockerfile (Bun Dashboard-Build → Ubuntu C++-Build → Ubuntu Runtime mit FFmpeg+Xvfb). `docker-compose.yml` für einfaches Deployment. Named Volume für SQLite-Daten.
11. **CI/CD**: GitHub Actions (`.github/workflows/ci.yml`) mit 4 Jobs: Dashboard-Build, Linux-Build+Test, Windows-Build+Test, Docker-Image. Trigger auf Push/PR zu `main`.
12. **Commits**: Nach jeder Änderung einen beschreibenden Git-Commit erstellen. README.md und diese Datei bei Bedarf aktualisieren.
13. **Git Hash Version**: CMake extrahiert den Git-Short-Hash via `git rev-parse --short HEAD` → `IS_GIT_HASH` Compile-Definition. `/api/status` gibt `version: "0.2.0+<hash>"` und `gitHash: "<hash>"` zurück. Dashboard-Footer zeigt Version dynamisch an.
14. **Config Export/Import**: `GET /api/config/export` liefert vollständigen JSON-Snapshot (config, channels, streams, profiles, audio, metadata mit `_export_version: 1`). `POST /api/config/import` stellt aus Snapshot wieder her (schreibt in SQLite, fordert Restart an).
15. **Per-Game Settings**: Spiele implementieren `configure(const nlohmann::json&)` und `getSettings()`. Gespeichert unter `game_settings.<game_id>` in Config. API: `GET/PUT /api/games/:id/settings`. Angewendet beim Startup via `Application::initialize()` und bei API-Aufruf.
16. **ProfileManager** (`src/core/ProfileManager.h/cpp`): Verwaltet Stream-Konfigurationsprofile (Presets). CRUD: `addProfile()`, `updateProfile()`, `removeProfile()`, `getProfile()`. Serialisierung via `loadFromJson()` / `toJson()`. Gespeichert in SettingsDatabase unter "profiles" Key. API: `GET/POST /api/profiles`, `GET/PUT/DELETE /api/profiles/:id`.
17. **Bot Fill System**: Spiele können Bot-Spawning auslösen, um Mindest-Spielerzahlen zu erreichen. Bots haben KI-Verhalten mit konfigurierbaren Smash-Wahrscheinlichkeiten und Aktionsintervallen. Bots erscheinen standardmäßig nicht im Kill-Feed (`bot_kill_feed`). Tote Bots können automatisch respawnen (`bot_respawn`, `bot_respawn_delay`). Konfigurierbar per Game-Settings. Aktuell in Gravity Brawl implementiert.
18. **Scoreboard Overlay**: Streams zeigen ein rotierendes Scoreboard-Overlay (Recent/All-Time Panels) mit konfigurierbarer Zykluszeit und Crossfade. Optional: automatischer Chat-Post des Scoreboards in konfigurierbarem Intervall.
19. **YouTube Quota Management** (`src/platform/youtube/YouTubeQuota.h/cpp`): Singleton-Tracker für YouTube Data API v3 Quota (10.000 Units/Tag, Mitternacht Pacific Time Reset). Kosten: `COST_LIST=1`, `COST_SEARCH=100`, `COST_INSERT=50`, `COST_UPDATE=50`. `consume(cost)` blockiert bei erschöpftem Budget. Auto-Rollover via `pacificDayNumber()`. Alle `YouTubeApi`-Methoden rufen `consume()` vor jeder API-Anfrage auf. REST-Chat-Polling ebenfalls quota-geschützt. Dashboard zeigt Quota-Bar in YouTube-Channel-Karten. API: `GET /api/youtube/quota`, `PUT /api/youtube/quota` (budget setzen, reset). Broadcast-Erkennung: Keine automatischen Versuche ohne gestarteten Stream. Nach Stream-Start: 10s Wartezeit, dann max. 2 Versuche (30s Abstand). Danach nur noch manuell via `POST /api/youtube/detect/:channelId` (Dashboard-Button).
20. **Stream-Event-Reactions**: Spiele reagieren auf Stream-Events (Subscriptions, Super Chats, Bits, Channel Points) über `ChatMessage::eventType`. YouTube-Events werden sowohl via gRPC (proto event types: NEW_SPONSOR_EVENT=7, SUPER_CHAT_EVENT=15, SUPER_STICKER_EVENT=16) als auch via REST (snippet.type) gemappt. Twitch-Events via IRC-Tags. Alle drei Spiele implementieren `handleStreamEvent()` mit spielspezifischen Belohnungen (Schild, Heilung, God Mode, Gebietserweiterung). Events lösen automatisch `!join` aus.
21. **Text-Layout-System mit Farbüberschreibung**: Spiele registrieren Text-Elemente via `registerTextElement()` mit optionaler `defaultColor` (RGBA hex). Dashboard erlaubt Überschreiben von Position, Größe, Sichtbarkeit und Farbe pro Element. `parseHexColor()` konvertiert "#RRGGBBAA" oder "#RRGGBB" zu `sf::Color`. `applyTextLayout()` wendet automatisch die konfigurierte Farbe an.
22. **AudioMixer** (`src/core/AudioMixer.h/cpp`): Erzeugt PCM-Audio-Output für Streams. Mixt Musik und SFX in 44100 Hz Stereo S16LE. Wird als `audioMixer` Pointer in `EncoderSettings` übergeben. `StreamEncoder` piped den Audio-Output parallel zum Video an FFmpeg.
23. **ChannelStats** (`src/core/ChannelStats.h/cpp`): Per-Channel Engagement-Metriken (unique viewers, total interactions, active sessions, top users). API: `GET /api/channels/:id/stats`.
24. **TwitchApi** (`src/platform/twitch/TwitchApi.h/cpp`): Statische Helfer für Twitch Helix API via curl. `getBroadcasterId()`, `getGameId()`, `updateChannelInfo()`. Ermöglicht automatisches Setzen von Twitch-Kategorie und Titel beim Spielwechsel.

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
