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
| `is::games::country_elimination` | `src/games/country_elimination/` | Country Elimination-Spiel (Marble-Race in rotierender Arena) |
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
Abstrakte Basisklasse für alle Spiele. Vollständige Definition in `src/games/IGame.h`.

**Pure virtuals** (Pflicht): `id()`, `displayName()`, `description()`, `initialize()`, `shutdown()`, `onChatMessage()`, `update()`, `render()`, `isRoundComplete()`, `isGameOver()`, `getState()`, `getCommands()`

**Optional overrides**: `maxPlayers()` (0=unbegrenzt), `getLeaderboard()`, `configure(json)`, `getSettings()`

**Text-Layout-System**: Im Konstruktor via `registerTextElement(id, label, x%, y%, fontSize, align, visible, defaultColor)` registrieren. Im `render()` via `resolve(id, w, h)` → `ResolvedText`, dann `applyTextLayout(sf::Text&, resolved)`. Farben als RGBA-Hex `#RRGGBBAA`.

**Chat-Feedback**: `sendChatFeedback(msg)` sendet Nachricht via Callback an Chat-Kanäle.

**Font-Skalierung**: `setFontScale()` / `fontScale()` – per-Stream konfigurierbar.

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
5. Bei Vote-Modus fängt StreamInstance `vote`-Commands ab, ansonsten Weiterleitung an `GameManager::handleChatMessage()`
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
Felder: `ffmpegPath`, `outputUrl`, `width`/`height`/`fps`, `bitrate`, `preset`, `codec`, `profile`, `tune`, `keyframeInterval`, `threads`, `cbr`, `maxrateFactor`, `bufsizeFactor` sowie Audio (`audioBitrate`, `audioSampleRate`, `audioCodec`, `AudioMixer* audioMixer`). Jeder Stream hat eigene `EncoderSettings`; `StreamEncoder` akzeptiert diesen Struct im Konstruktor.

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
Befehle in `ChaosArena::onChatMessage()` (alle Befehle funktionieren mit oder ohne `!`-Prefix):
- `join`/`play` | `left`/`l`/`a`, `right`/`r`/`d` → Bewegung
- `jump`/`j`/`w`/`up`, `jumpleft`/`jl`, `jumpright`/`jr` → Springen
- `attack`/`hit`/`atk` → Melee | `special`/`sp`/`ult` → Projektil (5s CD)
- `dash`/`dodge` → I-Frame Dash (3s CD) | `block`/`shield`/`def` → Block
- `emote [text]` → Kosmetisches Emote

### Stream-Event-Handling
Bei `eventType`-Nachrichten in `handleStreamEvent()`:
- **yt_subscribe / twitch_sub**: Schild (10s) + 50 HP Heilung + 300 Punkte
- **yt_superchat / twitch_bits** (>100): Volle Heilung + Schadens-Boost (15s) + Unverwundbarkeit (5s) + 500 Punkte
- **twitch_channel_points**: Schild (5s) + Geschwindigkeits-Boost (10s) + 100 Punkte
- Events lösen automatisch `join` aus, falls der Nutzer noch nicht im Spiel ist

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
Befehle in `ColorConquest::onChatMessage()` (alle Befehle funktionieren mit oder ohne `!`-Prefix):
- `join [team]`/`play` → Team beitreten (red/blue/green/yellow oder auto)
- `up`/`u`/`w`/`north`, `down`/`d`/`s`/`south`, `left`/`l`/`a`/`west`, `right`/`r`/`e`/`east` → Expansion-Vote
- `emote [text]` → Team-Emote

### Stream-Event-Handling
Bei `eventType`-Nachrichten in `handleStreamEvent()`:
- **yt_subscribe / twitch_sub**: Doppelte Gebiets-Expansion in zufälliger Richtung
- **yt_superchat / twitch_bits** (>100): Doppelte Expansion in ALLEN 4 Richtungen
- Events lösen automatisch `join` aus, falls der Nutzer noch nicht im Spiel ist

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
- Füllt die Lobby automatisch mit KI-Bots auf, um `min_players` zu erreichen
- Settings: `bot_fill`, `min_players`, `bot_kill_feed` (default: false), `bot_respawn`, `bot_respawn_delay`, `bot_action_interval`, `bot_smash_chance`, `bot_danger_smash_chance`, `bot_event_smash_chance`

### Dynamic Camera Zoom
- Kamera zoomt dynamisch, um alle Spieler sichtbar zu halten
- Settings: `camera_zoom_enabled`, `camera_zoom_speed`, `camera_min_zoom`, `camera_max_zoom`, `camera_buffer_meters`, `camera_zoom_amplify` (1–5, verstärkt Zoom-Effekt für Sichtbarkeit auf Stream)

### Sound Effects (SFX)
- Dateien in `assets/audio/sfx/gravity_brawl/` (.wav/.ogg/.mp3, graceful degradation wenn fehlend)
- `AudioManager::loadSfx()` / `playSfx()` | Settings: `sfx_enabled`, `sfx_volume`
- Events: `gb_join`, `gb_smash`, `gb_supernova`, `gb_hit`, `gb_death`, `gb_kill`, `gb_bounty`, `gb_cosmic_event`, `gb_cosmic_end`, `gb_countdown`, `gb_battle_start`, `gb_game_over`

### Chat-Befehl-Parsing
Befehle in `GravityBrawl::onChatMessage()` (alle Befehle funktionieren mit oder ohne `!`-Prefix):
- `join [farbe]` / `play` → Spieler hinzufügen (Farbe optional: red, blue, green, yellow, #RRGGBB)
- `s` / `smash` → Dash/Ram-Angriff (0.8s Cooldown)
- 5 aufeinanderfolgende Smashes innerhalb 3s → Supernova (großer AoE-Knockback)

### Stream-Event-Handling
Bei `eventType`-Nachrichten wird `triggerLivestreamReward()` aufgerufen:
- **yt_subscribe / twitch_sub**: Schild + Tier-Bonus + 300 Punkte
- **twitch_channel_points**: Supernova-Auslösung
- **yt_superchat / twitch_bits** (>100): God Mode (30s) + 3× Masse
- Events lösen automatisch `join` aus, falls der Nutzer noch nicht im Spiel ist

### Per-Game Settings
- Configure via `configure(json)` / `getSettings()`, gespeichert unter `game_settings.gravity_brawl`
- API: `GET/PUT /api/games/gravity_brawl/settings`
- 40+ Parameter: Physik (`orbital_gravity_strength`, `black_hole_*`, `restitution`), Gameplay (`smash_cooldown`, `supernova_radius`, `max_speed`, `respawn_cooldown`), Audio, Kamera-Zoom, Bots, `epoch_duration`, `enable_post_processing`, Anomalien

---

## Country Elimination – Interna

### Übersicht
Marble-Race/Elimination in einer rotierenden Arena. Zuschauer joinen mit `join <country>`, Bälle prallen in einer kreisförmigen Begrenzung ab. Die Arena dreht sich und hat eine Lücke – wer herausfällt, ist eliminiert. Letzter Ball gewinnt. Bot-System füllt automatisch die Arena, sodass das Spiel nie stillsteht. Periodische Geographie-Quiz-Fragen werden als Overlay angezeigt und im Chat gepostet; Zuschauer antworten mit `1`–`4` oder `a`–`d`. Ein animierter Audio-Visualizer (Spektrum-Ring, Lila/Cyan-Farbverlauf) umgibt die Arena. Namespace: `is::games::country_elimination`. Dateien: `CountryElimination.h/cpp`, `CountryAliases.h`.

### Spielphasen
`GamePhase::Lobby` → `Countdown` → `Battle` → `RoundEnd` → zurück zu `Lobby`

### Physik (Box2D)
- `b2Vec2 gravity(0.0f, 15.0f)` – Schwerkraft für eliminierte Bälle
- Spieler im Arena: `GravityScale = 0.0f`, `linearDamping = 0.0f` (schweben/prallen ab, konstante Geschwindigkeit)
- Spieler nach Eliminierung: `GravityScale = 1.0f`, `linearDamping = 0.5f` (fallen zum Boden, prallen an Seitenwänden)
- Arena-Boundary: `b2_kinematicBody` mit rotierender Winkelbewegung
- Boden: `b2_staticBody` bei `FLOOR_Y = WORLD_CY + 17.0f` (sichtbar am unteren Bildschirmrand)
- Seitenwände: `b2_staticBody` bei `WALL_LEFT_X / WALL_RIGHT_X` (knapp außerhalb der Arena, sichtbar)
- Decke: `b2_staticBody` bei `CEILING_Y = WORLD_CY - ARENA_RADIUS - 3.0f`
- Hohe Restitution (0.95): Bälle prallen energisch ab
- **Konstante Geschwindigkeit**: `enforceConstantVelocity()` normalisiert Ballgeschwindigkeit jedes Frame auf `m_currentBallSpeed`
- Eliminierte Bälle sind **sichtbar auf dem Bildschirm** – Boden, Wände und Decke halten sie im sichtbaren Bereich

### Flaggen-Rendering
- `generateFlagTextures()` lädt `assets/img/flagSprite60.png` (246 Flaggen, vertikal gestapelt)
- `SPRITE_ORDER[]` definiert die Reihenfolge der 246 ISO-2-Letter-Codes (Afrika→Amerika→Asien→Europa→Naher Osten→Ozeanien→Spezial)
- Jede Reihe wird per `sf::Texture::loadFromImage(spriteSheet, IntRect)` extrahiert und in `m_flagTextures` (Key: 2-Letter-Code) gespeichert
- Lookup via Label (case-insensitive uppercase): Bot-Labels nutzen 2-Letter-Codes ("US", "GB", etc.)
- Fallback: Wenn kein Flaggen-Textur gefunden, wird ein farbiger Kreis gezeichnet
- Center-Square-Crop via `setTextureRect()` für rechteckige Flaggen auf kreisförmigen Bällen
- `renderPlayers()`: `sf::CircleShape::setTexture()` rendert Flagge kreisförmig auf dem Ball
- `renderWinnerOverlay()`: Großer Sieger-Ball zeigt ebenfalls die Flagge

### Arena-Rotation & Gap
- Kreisförmige Wand aus `WALL_SEGMENTS` (64) Box-Segmenten
- **Lobby**: Geschlossener Ring (`m_currentGapAngle = 0`), Bälle prallen innen ab
- **Battle**: Gap öffnet sich (`GAP_INITIAL = 0.26 rad`), expandiert über Zeit (`m_gapExpansionRate`)
- **Arena dreht sich IMMER** – auch in Lobby/Countdown/RoundEnd
- `recreateArena()` baut Arena-Body bei Gap-Änderung neu auf (Winkel/Geschwindigkeit bleiben erhalten)
- Rotation beschleunigt sich während Battle (`m_arenaSpeedIncrease`)

### Bot Fill System
- Füllt die Arena automatisch mit Bot-Bällen (Ländernamen als Labels)
- 40 vordefinierte Länder: USA, GBR, FRA, DEU, JPN, BRA, etc.
- `isBot()` via `"__bot_"` userId-Prefix Konvention
- Settings: `bot_fill` (default 8), `bot_respawn` (default true), `bot_respawn_delay` (default 3.0s)
- Bots werden gefiltert in: Leaderboard, PlayerDatabase, Chat-Feedback

### Geography Quiz
- Periodische Geographie-Quiz-Fragen während der Playing-Phase
- Fragen erscheinen als Overlay + werden gleichzeitig im Chat gepostet
- Zuschauer antworten mit `1`/`2`/`3`/`4` oder `a`/`b`/`c`/`d`
- Richtige Antworten geben `quiz_points` Punkte (Standard: 25)
- Settings: `quiz_enabled` (default true), `quiz_interval` (Standard 60s), `quiz_duration` (Standard 20s), `quiz_points` (Standard 25)

### Audio Visualizer
- Animierter Spektrum-Ring in Lila/Cyan-Farbverlauf umgibt die Arena
- Wird via `renderVisualizer()` in der render()-Methode gezeichnet
- Läuft unabhängig vom Spielzustand (auch in Lobby/Countdown/RoundEnd sichtbar)
- Setting: `visualizer_enabled` (default true)

### Chat-Befehl-Parsing
Befehle in `CountryElimination::onChatMessage()` (alle Befehle funktionieren mit oder ohne `!`-Prefix):
- `join [land/emoji]` / `play [land/emoji]` → Ball spawnen mit Label (max 16 Zeichen)

### Stream-Event-Handling
Bei `eventType`-Nachrichten in `handleStreamEvent()`:
- **yt_subscribe / twitch_sub**: Schild (15s) + 300 Punkte – Schild rettet einmal vor Eliminierung
- **yt_superchat / twitch_bits** (>100): Größerer Ball (1.5×) + Schild (20s) + 500 Punkte
- **twitch_channel_points**: Schild (10s) + 100 Punkte
- Events lösen automatisch `join` aus, falls der Nutzer noch nicht im Spiel ist

### Per-Game Settings
- `arena_radius`: Radius des Arena-Kreises in Metern (default 7.5)
- `arena_speed`: Anfangs-Rotationsgeschwindigkeit (rad/s, default 0.3)
- `arena_speed_increase`: Beschleunigung pro Sekunde (default 0.03)
- `gap_initial`: Anfängliche Gap-Halbwinkel in rad (default 0.26)
- `gap_expansion_rate`: Gap-Expansion pro Sekunde in rad (default 0.02)
- `gap_max`: Maximale Gap-Größe in rad (default 1.2)
- `wall_thickness`: Dicke der Arena-Wand in Metern (default 0.35)
- `ball_radius`: Radius der Spieler-Bälle in Metern (default 0.45)
- `initial_speed`: Anfangsgeschwindigkeit der Bälle (default 5.0)
- `ball_speed_increase`: Geschwindigkeitszunahme pro Sekunde (default 0.5)
- `max_ball_speed`: Maximale Ballgeschwindigkeit (default 15.0)
- `restitution`: Abprall-Elastizität (default 0.95)
- `gravity`: Schwerkraft für eliminierte Bälle (default 15.0)
- `countdown_duration`: Countdown-Dauer vor Battle-Start in Sekunden (default 3.0)
- `round_duration`: Maximale Rundendauer in Sekunden (default 120)
- `lobby_duration` (default 5.0), `round_end_duration` (default 4.0): Timing-Einstellungen
- `min_players`: Mindestanzahl für Spielstart (default 2)
- `champion_threshold`: Siege für Champion-Titel (default 4)
- `max_entries_per_player`: Wie viele Bälle ein Spieler gleichzeitig haben kann (default 1)
- `score_win`: Punkte für Rundengewinn (default 100)
- `score_participation`: Punkte für Teilnahme/Eliminierung (default 1)
- `score_sub`: Punkte für Sub-Event (default 300)
- `score_superchat`: Punkte für Superchat/Bits-Event (default 500)
- `score_points`: Punkte für Channel-Points-Event (default 100)
- `shield_duration_sub`: Schild-Dauer bei Sub-Events in Sekunden (default 15.0)
- `shield_duration_superchat`: Schild-Dauer bei Superchat/Bits in Sekunden (default 20.0)
- `shield_duration_points`: Schild-Dauer bei Channel-Points in Sekunden (default 10.0)
- `bot_fill`: Ziel-Spieleranzahl mit Bots (default 8, 0=disabled)
- `bot_respawn`: Bots respawnen nach Eliminierung (default true)
- `bot_respawn_delay`: Respawn-Verzögerung in Sekunden (default 3.0)
- `elim_feed_max`: Maximale Einträge im Elimination-Feed (default 8)
- `max_eliminated_visible`: Maximale sichtbare eliminierte Bälle (default 20)
- `elim_linger_duration`: Verweildauer eliminierter Bälle am Boden (default 8.0)
- `elim_fade_duration`: Fade-Out-Dauer eliminierter Bälle (default 2.0)
- `elim_infinite_linger`: Eliminierte Bälle bleiben unendlich, bis max_eliminated_visible erreicht (default false)
- `elim_persist_rounds`: Eliminierte Bälle bleiben zwischen Runden liegen (default false)
- `flag_shape_rect`: Flaggen als Rechteck statt Kreis anzeigen, Hitbox passt sich an (default false)
- `flag_outline`: Flaggen-Rahmen anzeigen (default true)
- `flag_outline_thickness`: Dicke des Flaggen-Rahmens (default 1.5)
- `rainbow_ring`: Arena-Ring in animierten Regenbogenfarben (default true)
- `allow_reentry`: Spieler, die zurück in die Arena prallen, werden wiederbelebt (default true)
- `show_bot_names`: Namen unter Bot-Spielern anzeigen (default true)
- `name_text_scale`: Skalierung des Namenstexts unter dem Ball (default 1.0)
- `label_text_scale`: Skalierung des Label-Texts auf dem Ball (default 1.0)
- `avatar_scale`: Skalierung des Profilbilds unter dem Ball (default 1.0)
- `avatar_outline_thickness`: Dicke des Rahmens um das Profilbild unter dem Ball (default 1.0)
- `quiz_enabled`: Geographie-Quiz während Battle-Phase aktivieren (default true)
- `quiz_interval`: Pause zwischen Quiz-Fragen in Sekunden (default 60.0)
- `quiz_duration`: Zeit zum Beantworten einer Quiz-Frage in Sekunden (default 20.0)
- `quiz_points`: Punkte für richtige Quiz-Antwort (default 25)
- `visualizer_enabled`: Audio-Visualizer (Spektrum-Ring) um die Arena anzeigen (default true)
- `leaderboard_enabled`: Länder-Leaderboard-Panel anzeigen (default true)
- `leaderboard_mode`: Leaderboard-Modus: 0 = Session (In-Memory), 1 = 24h (SQLite), 2 = All-Time (SQLite) (default 0)
- `leaderboard_max_entries`: Maximale Einträge im Länder-Leaderboard (default 5, max 30)
- `leaderboard_font_size`: Schriftgröße im Leaderboard in Punkten (default 18, Bereich 10–48)
- `leaderboard_flag_size`: Flaggen-Skalierung im Leaderboard (default 1.0, Bereich 0.3–3.0)
- `leaderboard_show_codes`: ISO-2-Ländercodes im Leaderboard anzeigen (default true)
- `leaderboard_show_names`: Vollständige Ländernamen im Leaderboard anzeigen (default true)
- `leaderboard_text_scale`: Text-Skalierung im Leaderboard (default 1.0, Bereich 0.3–3.0)

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
- `country_wins` – id (autoincrement), country_code (TEXT), country_name (TEXT), wins (INT), timestamp

### Scoring-Hooks
Spiele rufen `Application::instance().playerDatabase().recordResult()` auf:
- **Chaos Arena**: +100 Punkte + Win bei Rundengewinn, +25 Punkte bei Kill, +1 Punkt für Teilnahme
- **Gravity Brawl**: Gleiche Scoring-Regeln wie Chaos Arena
- **Color Conquest**: +50 Punkte + Win für Gewinner-Team, +5 Punkte für Teilnahme
- **Country Elimination**: `recordCountryWin(code, name)` – Ländersieg in `country_wins` Tabelle + reguläre Player-Punkte (+100 + Win für Sieger, +1 für Teilnahme)

### API
- `GET /api/scoreboard/recent?limit=10&hours=24` – Top-Spieler der letzten N Stunden
- `GET /api/scoreboard/alltime?limit=5` – All-Time-Leaderboard
- `GET /api/scoreboard/player/:id` – Einzelne Spieler-Statistiken
- `GET /api/games/country_elimination/scoreboard/alltime?limit=5` – Top-Länder aller Zeiten
- `GET /api/games/country_elimination/scoreboard/recent?hours=24&limit=5` – Top-Länder der letzten 24h

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
GPU-Shader (Software-Fallback für Vignette): `applyVignette()`, `applyBloom()`, `applyChromaticAberration()`, `applyCRT()`. Software-only: `applyScanlines()`.

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
OPTIONS, statische Dateien und `/api/auth/*` werden immer durchgelassen. Auth-Prüfung: API-Key (`Authorization: Bearer`, `X-API-Key`, `?api_key=`), dann Session-Token. Falls weder API-Key noch Passwort konfiguriert: kein Auth. Sonst HTTP 401.

### Dashboard-Integration
- `web/src/app/login/page.tsx`: Login-Seite mit Setup- und Login-Modus
- `web/src/components/app-shell.tsx`: Auth-Guard prüft `/api/auth/status` beim Mount, leitet zu Login/Setup um
- `web/src/lib/api.ts`: `authHeaders()` erzeugt `Authorization: Bearer`-Header (Token aus localStorage)
- `request()` fügt Auth-Headers automatisch zu allen API-Calls hinzu

---

## Docker & CI/CD

- **Dockerfile**: Multi-Stage (Bun Dashboard-Build → Ubuntu C++-Build → Ubuntu Runtime mit FFmpeg+Xvfb). COPY-Layer nach Änderungshäufigkeit sortiert für optimales Caching.
- **docker-compose.yml**: Service `app` auf Port 8080, Named Volume `is-data` für SQLite, Config-Bind-Mount.
- **GitHub Actions** (`.github/workflows/ci.yml`): 4 Jobs auf Push/PR zu `main`: Dashboard-Build, Linux-Build+Test, Windows-Build+Test, Docker-Image.

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

1. **Sound-System**: `AudioManager` (`src/core/AudioManager.h/cpp`) – Musik (Playlist, Shuffle, auto-advance aus `assets/audio/`) und SFX. API: `GET/PUT /api/audio`, `POST /api/audio/next|pause|resume|rescan`. Config: `audio.music_volume`, `audio.sfx_volume`, `audio.muted`, `audio.fade_in_seconds`, `audio.fade_out_seconds`, `audio.crossfade_overlap`.
2. **Thread-Sicherheit**: `std::mutex`-geschützte Queues/States in Plattform-Threads und Web-API-Threads. RenderTextures werden lazy im Main-Thread erstellt.
3. **FFmpeg**: `popen("ffmpeg ...")` – muss im System-PATH sein. RGBA-Frames via `stdin`, Audio parallel via `AudioMixer`.
4. **Config**: Standard `config/default.json` (CLI-überschreibbar). Persistenz in `data/settings.db` (SQLite, WAL). `POST /api/config/save` schreibt JSON-Backup.
5. **Web-Dashboard**: Statische Dateien aus `dashboard/` neben Executable. Next.js Static Export, gebaut mit `bun run build`. **Immer `bun` verwenden** (nicht npm/yarn).
6. **API-Auth**: API-Key (`web.api_key`) oder Password-Login (Session-basiert, `Sha256.h`). `/api/auth/*` immer erlaubt. `--reset-password` für Recovery.
7. **Docker/CI**: Multi-Stage Dockerfile (Bun → Ubuntu C++ → Ubuntu Runtime + FFmpeg/Xvfb). Named Volume für SQLite. GitHub Actions: 4 Jobs (Dashboard, Linux, Windows, Docker) auf Push/PR zu `main`.
8. **Commits & Push**: Beschreibende Commits nach jeder Änderung erstellen **und auf `main` pushen**. README.md und diese Datei bei Bedarf aktualisieren.
9. **Git Hash Version**: `IS_GIT_HASH` Compile-Definition. `/api/status` → `version: "0.2.0+<hash>"`, `gitHash`.
10. **Config Export/Import**: `GET /api/config/export` → JSON-Snapshot (`_export_version: 1`). `POST /api/config/import` → restauriert und fordert Restart.
11. **Per-Game Settings**: `configure(json)` / `getSettings()` → `game_settings.<game_id>`. API: `GET/PUT /api/games/:id/settings`.
12. **ProfileManager** (`src/core/ProfileManager.h/cpp`): Stream-Konfigurationsprofile. API: `GET/POST /api/profiles`, `GET/PUT/DELETE /api/profiles/:id`.
13. **Scoreboard Overlay**: Rotierendes Recent/All-Time-Overlay mit Crossfade. Optionaler Chat-Post via `scoreboardChatInterval`.
14. **YouTube Quota** (`YouTubeQuota.h/cpp`): 10.000 Units/Tag, Pacific-Time Reset. `consume(cost)` blockiert bei Erschöpfung. API: `GET /api/youtube/quota`, `PUT /api/youtube/quota`. Broadcast-Erkennung: 10s nach Stream-Start, max. 2 Versuche; manuell via `POST /api/youtube/detect/:channelId`.
15. **Stream-Event-Reactions**: `handleStreamEvent()` in allen 4 Spielen. YouTube gRPC (event types 7/15/16) + REST + Twitch IRC. Events lösen automatisch `join` aus.
16. **Text-Layout mit Farbe**: `registerTextElement(..., defaultColor)` + Dashboard-Overrides via `applyTextOverrides(json)`. `parseHexColor()` unterstützt `#RRGGBBAA` und `#RRGGBB`.
17. **AudioMixer** (`src/core/AudioMixer.h/cpp`): PCM 44100 Hz Stereo S16LE. Pointer in `EncoderSettings::audioMixer`.
18. **ChannelStats** (`src/core/ChannelStats.h/cpp`): Engagement-Metriken pro Channel. API: `GET /api/channels/:id/stats`.
19. **TwitchApi** (`src/platform/twitch/TwitchApi.h/cpp`): `getBroadcasterId()`, `getGameId()`, `updateChannelInfo()` – automatisches Setzen von Kategorie/Titel beim Spielwechsel.

---

## Lokales Testen

- **LocalPlatform**: `POST /api/chat` (`{"username": "...", "text": "..."}`) oder Terminal `[username] befehl` (Config: `platforms.local.console_input: true`)
- **Dashboard**: http://localhost:8080 – Chat-Test-UI, Quick-Buttons, 4 vorkonfigurierte Spieler
- **Vorschau**: SFML-Fenster öffnet automatisch (erster Stream), Letterboxing via `Renderer::displayPreview()`
- Ausführliche Test-Anleitungen in `TESTING.md`
