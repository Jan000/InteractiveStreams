# 🎮 InteractiveStreams

**Modulare, automatische Streaming-Plattform für interaktive Zuschauer-Spiele**

InteractiveStreams ist ein C++-Programm, das vollautomatisch interaktive Spiele für Stream-Zuschauer auf YouTube, Twitch und weiteren Plattformen bereitstellt. Die gesamte Grafikoberfläche wird vom Programm generiert und automatisch an die konfigurierten Plattformen gestreamt. Zuschauer spielen gegeneinander – gesteuert ausschließlich über Chat-Nachrichten.

---

## 📋 Inhaltsverzeichnis

- [Features](#-features)
- [Architektur](#-architektur)
- [Erstes Spiel: Chaos Arena](#-erstes-spiel-chaos-arena)
- [Zweites Spiel: Color Conquest](#-zweites-spiel-color-conquest)
- [Technologie-Stack](#-technologie-stack)
- [Projektstruktur](#-projektstruktur)
- [Build-Anleitung](#-build-anleitung)
- [Konfiguration](#-konfiguration)
- [Web-Admin-Dashboard](#-web-admin-dashboard)
- [Chat-Befehle](#-chat-befehle)
- [Erweiterbarkeit](#-erweiterbarkeit)
- [Roadmap](#-roadmap)

---

## ✨ Features

### Kernfunktionen
- **Vollautomatisches Streaming** – Rendert Grafik und streamt via FFmpeg (RTMP) an Twitch, YouTube und weitere Plattformen
- **Chat-basierte Steuerung** – Zuschauer steuern ihre Spielfiguren über Chat-Befehle
- **Modulare Spielarchitektur** – Neue Spiele als eigenständige Module hinzufügbar
- **Modulare Plattformintegration** – Einfach neue Streaming-/Chat-Plattformen integrierbar
- **Web-Admin-Dashboard** – Vollständiges Verwaltungs-Dashboard im Browser
- **Plattformunabhängig** – Läuft auf Windows, Linux und macOS
- **Server-fähig** – Konzipiert für headless Server-Betrieb

### Grafik & Physik
- **Box2D-Physik** – Realistische Physik-Simulation mit Kollisionen und Knockback
- **Partikel-System** – Tausende Partikel für Explosionen, Treffer-Funken, Trails und Effekte
- **Post-Processing** – Vignette, Bloom-Approximation, Scanline-Effekte
- **Animierter Hintergrund** – Parallax-Sternenhimmel mit Nebula-Effekten
- **60 FPS Rendering** – Flüssige Darstellung mit Fixed-Timestep-Physik

### Interaktivität
- **Bis zu 20 gleichzeitige Spieler** pro Runde
- **Leaderboard & Score-System** – Persistente Rangliste über mehrere Runden
- **Power-Ups** – Heilung, Geschwindigkeit, Schaden, Schild, Doppelsprung
- **Kill-Feed** – Echtzeit-Anzeige von Eliminierungen

---

## 🏗 Architektur

```
┌─────────────────────────────────────────────────────────────┐
│                      Application                             │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌───────────────┐  │
│  │  Config   │ │  Logger  │ │  Game    │ │   Platform    │  │
│  │  Manager  │ │          │ │  Manager │ │   Manager     │  │
│  └──────────┘ └──────────┘ └────┬─────┘ └───────┬───────┘  │
│                                  │               │           │
│  ┌──────────────────────────────┐│  ┌────────────┴────────┐ │
│  │        Game Registry         ││  │  IPlatform          │ │
│  │  ┌─────────────────────┐    ││  │  ├── Twitch (IRC)    │ │
│  │  │   IGame Interface   │    ││  │  ├── YouTube (API)   │ │
│  │  │  ┌───────────────┐  │    ││  │  └── ... (erweiterb.)│ │
│  │  │  │ Chaos Arena   │  │◄───┘│  └─────────────────────┘ │
│  │  │  │ ┌───────────┐ │  │     │                           │
│  │  │  │ │ Physics   │ │  │     │  ┌─────────────────────┐  │
│  │  │  │ │ Particles │ │  │     │  │     Renderer        │  │
│  │  │  │ │ Arena     │ │  │     │  │  ┌───────────────┐  │  │
│  │  │  │ │ Players   │ │  │     │  │  │ SFML Window   │  │  │
│  │  │  │ └───────────┘ │  │     │  │  │ RenderTexture │  │  │
│  │  │  └───────────────┘  │     │  │  └───────┬───────┘  │  │
│  │  └─────────────────────┘     │  └──────────┼──────────┘  │
│  └──────────────────────────────┘             │              │
│                                    ┌──────────▼──────────┐   │
│  ┌──────────────────────┐          │  Stream Encoder     │   │
│  │    Web Server        │          │  (FFmpeg Pipeline)   │   │
│  │  ┌────────────────┐  │          │  ┌───────────────┐  │   │
│  │  │ REST API       │  │          │  │ RTMP Output   │  │   │
│  │  │ Dashboard HTML │  │          │  │ ├── Twitch    │  │   │
│  │  └────────────────┘  │          │  │ └── YouTube   │  │   │
│  └──────────────────────┘          │  └───────────────┘  │   │
│                                    └─────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### Design-Prinzipien

1. **Modularität** – Jedes Subsystem (Spiele, Plattformen, Rendering, Streaming) ist unabhängig und über Interfaces entkoppelt
2. **Plugin-Architektur** – Spiele registrieren sich automatisch über das `REGISTER_GAME` Makro
3. **Fixed-Timestep** – Physik-Updates mit konstantem Zeitschritt (60 Hz), entkoppelt von Framerate
4. **Thread-Sicherheit** – Plattform-Kommunikation in eigenen Threads mit Message-Queues
5. **Konfigurierbar** – Alle Parameter über JSON-Konfigurationsdateien steuerbar

---

## 🎯 Erstes Spiel: Chaos Arena

**Chaos Arena** ist ein physik-basiertes Battle-Arena-Spiel:

### Spielablauf
1. **Lobby** – Zuschauer treten mit `!join` bei (30 Sekunden Wartezeit)
2. **Countdown** – 3-Sekunden-Countdown vor jeder Runde
3. **Battle** – Spieler kämpfen gegeneinander (120 Sekunden pro Runde)
4. **Rundenende** – Ergebnisse werden angezeigt (8 Sekunden)
5. **Nächste Runde** – Bis zu 5 Runden, dann Game Over mit finaler Rangliste

### Spielmechaniken
- **Physik-basierte Bewegung** – Box2D für realistische Kollisionen
- **Melee-Angriff** – Nahkampf mit Knockback
- **Spezialangriff** – Physik-Projektile mit Explosionseffekten
- **Dash** – Schneller Ausweichsprint mit kurzer Unverwundbarkeit
- **Block** – Reduziert eingehenden Schaden um 75%
- **Doppelsprung** – Zwei Sprünge in der Luft möglich
- **Power-Ups** – Spawnen regelmäßig in der Arena:
  - 💚 **Heilung** – Stellt 40 HP wieder her
  - 💙 **Geschwindigkeit** – Erhöhte Bewegungsgeschwindigkeit (8s)
  - ❤️ **Schaden** – Erhöhter Schaden (8s)
  - 💛 **Schild** – Temporäre Unverwundbarkeit (5s)
  - 💜 **Doppelsprung** – Setzt Sprung-Counter zurück

### Arena-Design
- Große Hauptplattform am Boden
- Mehrere schwebende Plattformen auf verschiedenen Höhen
- Zerstörbare Blöcke (50 HP)
- Todeszone unter der Arena (Fall-Tod)
- Wände und Decke als Begrenzung

---

## 🗺 Zweites Spiel: Color Conquest

**Color Conquest** ist ein territoriales Eroberungsspiel, konzipiert für **500+ gleichzeitige Spieler**.

### Warum ein zweites Spiel?

Chaos Arena nutzt Box2D-Physik mit O(n²)-Kollisionsprüfungen und bis zu 13 Draw-Calls pro Spieler. Dadurch ist es auf ~40-60 Spieler limitiert. Color Conquest wurde von Grund auf für massive Spielerzahlen entworfen:

- **O(1) pro Chat-Nachricht** – Hash-Map-basierte Spieler-Zuordnung
- **O(Grid) pro Update-Tick** – Nur die 960 Zellen (40×24) werden verarbeitet, nicht die Spieleranzahl
- **Keine Physik-Engine** – Kein Box2D, keine dynamischen Bodies
- **Minimale Draw-Calls** – Grid-basiertes Rendering

### Spielablauf

1. **Lobby** – Zuschauer wählen ein Team mit `!join red/blue/green/yellow` (oder `!join` für Auto-Zuweisung)
2. **Runden** – 30 Runden à 8 Sekunden Abstimmungszeit
3. **Abstimmung** – Teammitglieder stimmen über die Expansionsrichtung ab (`!up`, `!down`, `!left`, `!right`)
4. **Expansion** – Die Mehrheitsentscheidung jedes Teams wird ausgeführt, Grenzzellen werden erobert
5. **Spielende** – Das Team mit den meisten Zellen nach 30 Runden gewinnt

### Teams

| Team | Farbe | Startposition |
|------|-------|---------------|
| Red | 🔴 Rot | Oben links |
| Blue | 🔵 Blau | Oben rechts |
| Green | 🟢 Grün | Unten links |
| Yellow | 🟡 Gelb | Unten rechts |

### Chat-Befehle

| Befehl | Aliase | Beschreibung |
|--------|--------|-------------|
| `!join [team]` | `!play` | Team beitreten (red/blue/green/yellow oder auto) |
| `!up` | `!u`, `!w`, `!north` | Für Expansion nach oben stimmen |
| `!down` | `!d`, `!s`, `!south` | Für Expansion nach unten stimmen |
| `!left` | `!l`, `!a`, `!west` | Für Expansion nach links stimmen |
| `!right` | `!r`, `!e`, `!east` | Für Expansion nach rechts stimmen |
| `!emote [text]` | — | Team-Emote senden |

### Skalierbarkeit

| Spieleranzahl | Chaos Arena | Color Conquest |
|--------------|-------------|----------------|
| 20 | ✅ Optimal | ✅ Funktioniert |
| 50 | ⚠️ Grenzwertig | ✅ Kein Problem |
| 200 | ❌ Nicht möglich | ✅ Kein Problem |
| 1000+ | ❌ Nicht möglich | ✅ Kein Problem |

---

## 🛠 Technologie-Stack

| Komponente | Technologie | Version |
|-----------|------------|---------|
| **Sprache** | C++20 | — |
| **Build-System** | CMake | ≥ 3.20 |
| **Rendering** | SFML | 2.6.1 |
| **Physik** | Box2D | 2.4.1 |
| **JSON** | nlohmann/json | 3.11.3 |
| **HTTP-Server** | cpp-httplib | 0.15.3 |
| **Logging** | spdlog | 1.13.0 |
| **Streaming** | FFmpeg (extern) | — |

Alle Abhängigkeiten werden automatisch über **CMake FetchContent** heruntergeladen und gebaut.

---

## 📁 Projektstruktur

```
InteractiveStreams/
├── CMakeLists.txt              # Haupt-Build-Konfiguration
├── README.md                   # Diese Datei
├── .github/
│   └── copilot-instructions.md # GitHub Copilot Anweisungen
├── config/
│   └── default.json            # Standard-Konfiguration
├── assets/
│   └── fonts/                  # TrueType-Schriftarten
├── src/
│   ├── main.cpp                # Einstiegspunkt
│   ├── core/                   # Kern-Framework
│   │   ├── Application.h/cpp   # Hauptanwendung, besitzt alle Subsysteme
│   │   ├── Config.h/cpp        # JSON-Konfigurationsmanager
│   │   ├── GameManager.h/cpp   # Spiel-Verwaltung
│   │   └── Logger.h/cpp        # Logging (spdlog)
│   ├── games/                  # Spiele-Module
│   │   ├── IGame.h             # Spiel-Interface (abstrakt)
│   │   ├── GameRegistry.h/cpp  # Spiel-Registrierung & Fabrik
│   │   └── chaos_arena/        # Chaos Arena Spiel
│   │       ├── ChaosArena.h/cpp     # Haupt-Spiellogik
│   │       ├── Player.h/cpp         # Spieler-Entity
│   │       ├── Arena.h/cpp          # Arena-Level
│   │       ├── PhysicsWorld.h/cpp   # Box2D-Wrapper
│   │       ├── ParticleSystem.h/cpp # Partikel-Effekte
│   │       ├── Projectile.h/cpp     # Projektile
│   │       └── PowerUp.h/cpp        # Power-Up Items
│   ├── platform/               # Plattform-Integrationen
│   │   ├── IPlatform.h         # Plattform-Interface (abstrakt)
│   │   ├── ChatMessage.h       # Chat-Nachricht Struktur
│   │   ├── PlatformManager.h/cpp # Plattform-Verwaltung
│   │   ├── local/
│   │   │   └── LocalPlatform.h/cpp   # Lokale Test-Plattform
│   │   ├── twitch/
│   │   │   └── TwitchPlatform.h/cpp  # Twitch IRC Integration
│   │   └── youtube/
│   │       └── YoutubePlatform.h/cpp # YouTube API Integration
│   ├── rendering/              # Render-System
│   │   ├── Renderer.h/cpp      # SFML-Renderer (Window + Offscreen)
│   │   ├── Camera.h/cpp        # Kamera mit Shake & Zoom
│   │   ├── ParticleRenderer.h/cpp # Partikel-Rendering
│   │   ├── UIOverlay.h/cpp     # HUD & Benachrichtigungen
│   │   ├── Background.h/cpp    # Parallax-Hintergrund
│   │   └── PostProcessing.h/cpp # Nachbearbeitungseffekte
│   ├── streaming/              # Stream-Pipeline
│   │   ├── StreamEncoder.h/cpp # FFmpeg-Encoding
│   │   └── StreamOutput.h/cpp  # RTMP-Ziel-Verwaltung
│   └── web/                    # Web-Dashboard
│       ├── WebServer.h/cpp     # HTTP-Server (cpp-httplib)
│       ├── ApiRoutes.h/cpp     # REST API Endpunkte
│       └── dashboard/          # Frontend-Dateien
│           ├── index.html      # Dashboard HTML
│           ├── style.css       # Dashboard Styles
│           └── app.js          # Dashboard JavaScript
└── tests/                      # Tests (geplant)
```

---

## 🔨 Build-Anleitung

### Voraussetzungen

- **C++20-fähiger Compiler** (GCC 11+, Clang 14+, MSVC 2022+)
- **CMake** ≥ 3.20
- **Git** (für FetchContent)
- **FFmpeg** (optional, für Streaming – muss im PATH sein)

### Build (Windows)

```powershell
# Repository klonen
git clone <repo-url> InteractiveStreams
cd InteractiveStreams

# Build-Verzeichnis erstellen und konfigurieren
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Kompilieren
cmake --build build --config Release

# Ausführen
./build/Release/InteractiveStreams.exe
```

### Build (Linux / macOS)

```bash
# Zusätzliche Abhängigkeiten (Ubuntu/Debian)
sudo apt install libx11-dev libxrandr-dev libxcursor-dev \
    libxi-dev libudev-dev libgl1-mesa-dev libfreetype-dev

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Ausführen
./build/InteractiveStreams
```

---

## ⚙️ Konfiguration

Die Konfiguration erfolgt über `config/default.json`. Wichtige Einstellungen:

### Streaming aktivieren

```json
{
    "streaming": {
        "output_url": "rtmp://live.twitch.tv/app/DEIN_STREAM_KEY",
        "fps": 30,
        "bitrate_kbps": 4500
    }
}
```

### Twitch-Chat verbinden

```json
{
    "platforms": {
        "twitch": {
            "enabled": true,
            "oauth_token": "DEIN_OAUTH_TOKEN",
            "bot_username": "DeinBotName",
            "channel": "dein_kanal"
        }
    }
}
```

### YouTube-Chat verbinden

```json
{
    "platforms": {
        "youtube": {
            "enabled": true,
            "api_key": "DEIN_API_KEY",
            "live_chat_id": "DEINE_CHAT_ID",
            "channel_id": "DEINE_CHANNEL_ID"
        }
    }
}
```

---

## 🌐 Web-Admin-Dashboard

Das integrierte Web-Dashboard ist standardmäßig unter `http://localhost:8080` erreichbar.

### Dashboard-Features
- **Echtzeit-Status** – Spielphase, Rundenzähler, Spieleranzahl, Partikelanzahl
- **Spieler-Übersicht** – Gesundheit, Kills, Deaths, Score für jeden Spieler
- **Leaderboard** – Gesamtrangliste über alle Runden
- **Plattform-Status** – Verbindungsstatus aller Chat-Plattformen
- **Streaming-Monitoring** – FPS, Frames, Ziel-Endpunkte
- **Befehls-Referenz** – Alle verfügbaren Chat-Befehle
- **Remote-Steuerung** – Spiel wechseln, Server herunterfahren

### REST API Endpunkte

| Methode | Endpunkt | Beschreibung |
|---------|----------|-------------|
| GET | `/api/status` | Gesamtstatus (Spiel, Plattformen, Streaming) |
| GET | `/api/games` | Liste verfügbarer Spiele |
| POST | `/api/games/load` | Spiel laden `{"game": "chaos_arena"}` |
| GET | `/api/games/state` | Aktueller Spielzustand |
| GET | `/api/platforms` | Plattform-Status |
| POST | `/api/platforms/connect` | Plattform verbinden |
| POST | `/api/platforms/disconnect` | Plattform trennen |
| GET | `/api/config` | Konfiguration (Secrets redaktiert) |
| GET | `/api/streaming` | Streaming-Status |
| POST | `/api/chat` | Lokale Chat-Nachricht senden `{"username":"...","text":"..."}` |
| GET | `/api/chat/log` | Ausgehende Nachrichten-Log |
| POST | `/api/shutdown` | Server herunterfahren |

---

## ⌨️ Chat-Befehle

### Chaos Arena

| Befehl | Aliases | Beschreibung |
|--------|---------|-------------|
| `!join` | `!play` | Dem Spiel beitreten |
| `!left` | `!l`, `!a` | Nach links bewegen |
| `!right` | `!r`, `!d` | Nach rechts bewegen |
| `!jump` | `!j`, `!w`, `!up` | Springen (Doppelsprung möglich) |
| `!attack` | `!hit`, `!atk` | Nahkampf-Angriff |
| `!special` | `!sp`, `!ult` | Projektil abfeuern (5s Cooldown) |
| `!dash` | `!dodge` | Schneller Ausweichsprint (3s Cooldown) |
| `!block` | `!shield`, `!def` | Blocken (75% Schadensreduktion) |

---

## 🖥️ Lokales Testen

InteractiveStreams enthält eine **Local Platform** für Tests ohne Twitch/YouTube-Verbindung:

### Lokale Vorschau
Beim Start öffnet sich automatisch ein **SFML-Vorschaufenster** mit der gerenderten Spielgrafik. Das Fenster skaliert sich automatisch und zeigt exakt den gleichen Output, der später an die Streaming-Plattformen gesendet wird.

### Chat über Web-Dashboard
1. Starte die Anwendung
2. Öffne `http://localhost:8080` im Browser
3. Nutze den Bereich **“Local Chat (Test)”** unten im Dashboard:
   - Wähle einen Benutzernamen (Standard: `TestUser`)
   - Tippe Chat-Befehle ein (z.B. `!join`)
   - Nutze Quick-Buttons für häufige Befehle
4. Die Nachrichten werden direkt ins Spiel injiziert

### Chat über Konsole
Wenn `console_input` aktiviert ist (Standard: `true`), kannst du auch direkt in der Konsole/Terminal Befehle eingeben:

```
!join              → Als Benutzer "console" beitreten
[Alice] !join      → Als Benutzer "Alice" beitreten
[Bob] !attack      → Als "Bob" angreifen
```

### Chat über REST API
```bash
curl -X POST http://localhost:8080/api/chat \
  -H "Content-Type: application/json" \
  -d '{"username": "TestUser", "text": "!join"}'
```

**Weitere API-Endpunkte für lokales Testen:**
| Methode | Endpunkt | Beschreibung |
|---------|----------|-------------|
| POST | `/api/chat` | Nachricht injizieren `{"username": "...", "text": "..."}` |
| GET | `/api/chat/log` | Ausgehende Nachrichten-Log abrufen |

---

## 🔌 Erweiterbarkeit

### Neues Spiel hinzufügen

1. Erstelle einen neuen Ordner unter `src/games/mein_spiel/`
2. Implementiere das `IGame`-Interface:

```cpp
#include "games/IGame.h"
#include "games/GameRegistry.h"

class MeinSpiel : public is::games::IGame {
public:
    std::string id() const override { return "mein_spiel"; }
    std::string displayName() const override { return "Mein Spiel"; }
    std::string description() const override { return "Beschreibung"; }

    void initialize() override { /* ... */ }
    void shutdown() override { /* ... */ }
    void onChatMessage(const is::platform::ChatMessage& msg) override { /* ... */ }
    void update(double dt) override { /* ... */ }
    void render(sf::RenderTarget& target, double alpha) override { /* ... */ }
    nlohmann::json getState() const override { return {}; }
    nlohmann::json getCommands() const override { return {}; }
};

// Automatische Registrierung:
REGISTER_GAME(MeinSpiel);
```

3. Füge die Quelldateien in `CMakeLists.txt` unter `GAME_SOURCES` hinzu
4. Das Spiel ist automatisch über Dashboard und Config ladbar

### Neue Plattform hinzufügen

1. Erstelle einen Ordner unter `src/platform/meine_plattform/`
2. Implementiere das `IPlatform`-Interface:

```cpp
#include "platform/IPlatform.h"

class MeinePlattform : public is::platform::IPlatform {
public:
    std::string id() const override { return "meine_plattform"; }
    std::string displayName() const override { return "Meine Plattform"; }

    bool connect() override { /* ... */ }
    void disconnect() override { /* ... */ }
    bool isConnected() const override { /* ... */ }
    std::vector<ChatMessage> pollMessages() override { /* ... */ }
    bool sendMessage(const std::string& text) override { /* ... */ }
    nlohmann::json getStatus() const override { return {}; }
    void configure(const nlohmann::json& settings) override { /* ... */ }
};
```

3. Registriere die Plattform in `PlatformManager.cpp`

---

## 🗺 Roadmap

### Phase 1 – Foundation (aktuell) ✅
- [x] Projekt-Setup mit CMake und FetchContent
- [x] Kern-Architektur (Application, Config, Logger, GameManager)
- [x] Spiel-Interface und Registry mit Auto-Registrierung
- [x] Chaos Arena Spiel mit Box2D-Physik
- [x] Partikel-System mit vielfältigen Effekten
- [x] Plattform-Interface mit Twitch- und YouTube-Integration
- [x] Lokale Test-Plattform (Dashboard-Chat, Konsolen-Input, REST API)
- [x] SFML-Lokale-Vorschau (Fenster mit Auto-Skalierung)
- [x] SFML-Renderer mit Offscreen-Rendering
- [x] FFmpeg-Streaming-Pipeline
- [x] Web-Admin-Dashboard mit REST API und Chat-UI
- [x] JSON-basierte Konfiguration

### Phase 2 – Polish (geplant)
- [ ] Font-Rendering für Spielernamen und HUD-Text
- [ ] GLSL-Shader für Bloom, CRT-Effekt, Chromatic Aberration
- [ ] Sound-System (Hintergrundmusik, SFX)
- [ ] Animierte Spieler-Sprites statt Rectangles
- [ ] Verbesserte Arena-Generierung (prozedural)
- [ ] Chat-Feedback (Bestätigungen an Zuschauer senden)

### Phase 3 – Content
- [ ] Weiteres Spiel: Marble Race
- [ ] Weiteres Spiel: Quiz Battle
- [ ] Weiteres Spiel: Tower Defense
- [ ] Skin-/Cosmetic-System
- [ ] Persistente Datenbank für Leaderboards

### Phase 4 – Production
- [ ] Docker-Container für Server-Deployment
- [ ] CI/CD Pipeline
- [ ] Headless-Modus (ohne Fenster)
- [ ] Multi-Stream-Output (gleichzeitig an mehrere Plattformen)
- [ ] Authentifizierung für Web-Dashboard
- [ ] Metrics & Monitoring (Prometheus/Grafana)

---

## 📄 Lizenz

*Noch festzulegen.*

---

## 🤝 Mitwirken

Contributions sind willkommen! Bitte erstelle einen Fork und einen Pull Request.

Für Copilot-Unterstützung siehe [copilot-instructions.md](.github/copilot-instructions.md).
