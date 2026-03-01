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
- **Multi-Stream** – Mehrere unabhängige Streams gleichzeitig, jeder mit eigenem Spiel, eigener Auflösung und eigenem RTMP-Ziel
- **Multi-Channel** – Mehrere YouTube/Twitch-Kanäle gleichzeitig verbindbar, flexibel den Streams zuweisbar
- **Game-Modi** – Fixed (festes Spiel), Vote (Zuschauer-Abstimmung), Random (zufälliges nächstes Spiel) pro Stream
- **Desktop & Mobile** – Wählbare Auflösung pro Stream: 1080×1920 (Mobile/Vertikal) oder 1920×1080 (Desktop)
- **Chat-basierte Steuerung** – Zuschauer steuern ihre Spielfiguren über Chat-Befehle
- **Modulare Spielarchitektur** – Neue Spiele als eigenständige Module hinzufügbar
- **Web-Admin-Dashboard** – Next.js + TypeScript + shadcn/ui Dashboard mit Tabs für Streams, Channels, Scoreboard, Performance und Settings, inkl. Live-Stream-Vorschau
- **SQLite Scoreboard** – Persistente Spieler-Datenbank mit Top 10 (24h) und Top 5 (All-Time), konfigurierbare Anzeige
- **SQLite Settings-Persistenz** – Alle Einstellungen (Channels, Streams, globale Config) werden automatisch in SQLite gespeichert und überleben Neustarts
- **Performance-Monitoring** – Live-Graphen für FPS, Frame-Time, Memory und Spieleranzahl mit konfigurierbarem Zeitfenster
- **Headless Mode** – Betrieb ohne GUI-Fenster (für Server-Deployments, z.B. Ubuntu Server)
- **Alle Einstellungen via Web** – Kein manuelles Bearbeiten von Konfigurationsdateien nötig
- **Plattformunabhängig** – Läuft auf Windows, Linux und macOS

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
┌─────────────────────────────────────────────────────────────────┐
│                        Application                               │
│  ┌──────────┐ ┌──────────┐ ┌──────────────┐ ┌───────────────┐  │
│  │  Config   │ │  Logger  │ │   Channel    │ │    Stream     │  │
│  │  Manager  │ │          │ │   Manager    │ │    Manager    │  │
│  └──────────┘ └──────────┘ └──────┬───────┘ └───────┬───────┘  │
│                                    │                  │          │
│                      ┌─────────────┤     ┌────────────┤          │
│                      │             │     │            │          │
│               ┌──────▼──────┐      │  ┌──▼────────────▼──┐      │
│               │  IPlatform  │      │  │ StreamInstance    │      │
│               ├─ Local      │      │  │ ┌──────────────┐ │      │
│               ├─ Twitch     │      │  │ │ GameManager  │ │      │
│               ├─ YouTube    │      │  │ │ RenderTexture│ │      │
│               └─────────────┘      │  │ │ StreamEncoder│ │      │
│                                    │  │ └──────────────┘ │      │
│  ┌──────────────────────┐          │  │ (one per stream) │      │
│  │    Web Server        │          │  └──────────────────┘      │
│  │  ┌────────────────┐  │          │                             │
│  │  │ REST API       │  │          │  ┌──────────────────┐      │
│  │  │ Dashboard      │  │          │  │ StreamInstance 2 │      │
│  │  └────────────────┘  │          │  │ (...)            │      │
│  └──────────────────────┘          │  └──────────────────┘      │
└─────────────────────────────────────────────────────────────────┘
```

### Design-Prinzipien

1. **Modularität** – Jedes Subsystem (Spiele, Plattformen, Rendering, Streaming) ist unabhängig und über Interfaces entkoppelt
2. **Plugin-Architektur** – Spiele registrieren sich automatisch über das `REGISTER_GAME` Makro
3. **Multi-Stream-Isolation** – Jeder Stream hat eigene GameManager-, RenderTexture- und StreamEncoder-Instanz
4. **Fixed-Timestep** – Physik-Updates mit konstantem Zeitschritt (60 Hz), entkoppelt von Framerate
5. **Thread-Sicherheit** – Plattform-Kommunikation in eigenen Threads mit Message-Queues; Web-API-Änderungen über Mutex geschützt
6. **Web-First-Konfiguration** – Alle Einstellungen über das Web-Dashboard steuerbar, kein manuelles Bearbeiten von Dateien nötig

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
- **O(Grid) pro Update-Tick** -- Nur die 960 Zellen (24x40) werden verarbeitet, nicht die Spieleranzahl
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
| **Datenbank** | SQLite3 | 3.45.3 |
| **Charts** | Recharts | 3.7.0 |
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
│   │   ├── ChannelManager.h/cpp # Multi-Channel-Verwaltung (Twitch, YouTube, Local)
│   │   ├── StreamManager.h/cpp  # Multi-Stream-Verwaltung (CRUD)
│   │   ├── StreamInstance.h/cpp # Einzelne Stream-Instanz (Game + Render + Encode)
│   │   ├── GameManager.h/cpp   # Spiel-Verwaltung pro Stream
│   │   ├── PlayerDatabase.h/cpp # SQLite Spieler-Datenbank & Scoreboard
│   │   ├── SettingsDatabase.h/cpp # SQLite-basierte persistente Einstellungen
│   │   ├── PerfMonitor.h/cpp    # Performance-Metriken (FPS, Memory, etc.)
│   │   └── Logger.h/cpp        # Logging (spdlog)
│   ├── games/                  # Spiele-Module
│   │   ├── IGame.h             # Spiel-Interface (abstrakt)
│   │   ├── GameRegistry.h/cpp  # Spiel-Registrierung & Fabrik
│   │   ├── chaos_arena/        # Chaos Arena Spiel
│   │   │   ├── ChaosArena.h/cpp     # Haupt-Spiellogik
│   │   │   ├── Player.h/cpp         # Spieler-Entity
│   │   │   ├── Arena.h/cpp          # Arena-Level
│   │   │   ├── PhysicsWorld.h/cpp   # Box2D-Wrapper
│   │   │   ├── ParticleSystem.h/cpp # Partikel-Effekte
│   │   │   ├── Projectile.h/cpp     # Projektile
│   │   │   └── PowerUp.h/cpp        # Power-Up Items
│   │   └── color_conquest/     # Color Conquest Spiel
│   │       ├── ColorConquest.h/cpp  # Haupt-Spiellogik
│   │       ├── Grid.h/cpp           # Spielfeld-Grid (24×40)
│   │       └── Team.h               # Team-Datenstruktur
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
└── tests/                      # Unit-Tests (doctest)
    ├── test_main.cpp           # Test-Runner
    ├── test_Config.cpp         # Config-Tests
    ├── test_ChatMessage.cpp    # ChatMessage-Tests
    ├── test_Player.cpp         # Player-Tests
    ├── test_PhysicsWorld.cpp   # PhysicsWorld-Tests
    ├── test_GameRegistry.cpp   # GameRegistry-Tests
    ├── test_LocalPlatform.cpp  # LocalPlatform-Tests
    ├── test_Grid.cpp           # Color Conquest Grid-Tests
    └── test_ColorConquest.cpp  # Color Conquest Team-Tests
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

Die Konfiguration erfolgt primär über das **Web-Dashboard** (`http://localhost:8080`). Alle Einstellungen werden automatisch in einer **SQLite-Datenbank** (`data/settings.db`) persistiert – Änderungen über das Dashboard werden sofort gespeichert und überstehen Neustarts. Die Datei `config/default.json` dient nur als Template für den Erststart.

### Channels (Kanäle)

Kanäle werden im Dashboard unter dem Tab **Channels** verwaltet. Beispiel-JSON:

```json
{
    "channels": [
        {
            "id": "local",
            "platform": "local",
            "name": "Local Test",
            "enabled": true
        },
        {
            "id": "twitch-1",
            "platform": "twitch",
            "name": "My Twitch Channel",
            "enabled": false,
            "settings": {
                "oauth_token": "...",
                "bot_username": "...",
                "channel": "dein_kanal"
            }
        },
        {
            "id": "youtube-1",
            "platform": "youtube",
            "name": "My YouTube Channel",
            "enabled": false,
            "settings": {
                "api_key": "...",
                "live_chat_id": "...",
                "channel_id": "..."
            }
        }
    ]
}
```

### Streams

Streams werden im Dashboard unter dem Tab **Streams** verwaltet. Jeder Stream hat eigenen Game-Modus, Auflösung und RTMP-Ziel:

```json
{
    "streams": [
        {
            "id": "default",
            "name": "Main Stream",
            "resolution": "mobile",
            "game_mode": "vote",
            "fixed_game": "chaos_arena",
            "channels": ["local", "twitch-1"],
            "streaming": {
                "enabled": false,
                "output_url": "rtmp://live.twitch.tv/app/STREAM_KEY",
                "fps": 30,
                "bitrate_kbps": 4500
            }
        }
    ]
}
```

### Einstellungen über das Dashboard ändern

1. Öffne `http://localhost:8080`
2. Navigiere zum gewünschten Tab (Streams, Channels, Settings)
3. Ändere die Einstellungen – sie werden **automatisch** in SQLite persistiert
4. Optional: Klicke auf **Save Config** um zusätzlich eine JSON-Backup-Datei zu schreiben

---

## 🌐 Web-Admin-Dashboard

Das integrierte Web-Dashboard ist standardmäßig unter `http://localhost:8080` erreichbar.
Es basiert auf **Next.js 16 + TypeScript + shadcn/ui** und wird als statischer Export vom C++ HTTP-Server ausgeliefert.

### Live-Stream-Vorschau

Das Dashboard zeigt eine Live-Vorschau des ausgewählten Streams direkt im Browser.
Frames werden als JPEG über `GET /api/streams/:id/frame` abgerufen und im Canvas gerendert (~10 fps).

### Headless Mode

Für Server-Deployments (z.B. Ubuntu Server ohne Desktop-Umgebung) kann der Headless-Modus aktiviert werden:

```json
"application": {
    "headless": true
}
```

Im Headless-Modus wird kein SFML-Vorschaufenster erstellt. Die Spielgrafik wird nur über den Stream und die Web-Vorschau ausgegeben.
Auf Linux ohne X11 wird Xvfb benötigt: `xvfb-run ./InteractiveStreams`

### Dashboard entwickeln

```bash
cd web
bun install
bun run dev          # Startet Next.js Dev-Server auf :3000
bun run build        # Erzeugt statischen Export in web/out/
```

### Dashboard-Tabs

| Tab | Beschreibung |
|-----|-------------|
| **Streams** | Alle aktiven Streams verwalten – Spiel wechseln, Game-Modus setzen, Streaming starten/stoppen, Chat an Plattformen senden |
| **Channels** | Chat-Kanäle (Twitch, YouTube, Local) hinzufügen, verbinden, inline bearbeiten mit plattformspezifischen Einstellungen |
| **Scoreboard** | Persistentes Spieler-Ranking – Top 10 (24h) und Top 5 (All-Time) mit konfigurierbaren Anzeige-Einstellungen |
| **Performance** | Live-Graphen für FPS, Frame-Time, Memory-Nutzung und Spieleranzahl mit wählbarem Zeitfenster |
| **Settings** | Anwendungseinstellungen (FPS, Port), Spiel-Konfigurationen |

### Dashboard-Features
- **Multi-Stream-Verwaltung** – Streams erstellen, konfigurieren und löschen
- **Multi-Channel-Verwaltung** – Kanäle hinzufügen, verbinden und trennen
- **Game-Modus pro Stream** – Fixed, Vote oder Random auswählbar
- **Auflösung pro Stream** – Mobile (1080×1920) oder Desktop (1920×1080)
- **Spiel-Wechsel pro Stream** – Sofort, nach Runde oder nach Spiel
- **Streaming-Steuerung** – Start/Stop pro Stream mit individuellem RTMP-Ziel
- **Plattform-Chat** – Chat-Nachrichten direkt über die Stream-Card an einzelne Kanäle oder alle gleichzeitig senden
- **Inline-Channel-Editing** – Alle Kanal-Einstellungen (Twitch OAuth, YouTube API Key, etc.) direkt in der Channel-Card bearbeitbar
- **SQLite-Scoreboard** – Persistentes Spieler-Ranking mit konfigurierbaren Top-N und Zeitfenster-Einstellungen
- **Performance-Dashboard** – Echtzeit-Graphen (Recharts) für FPS, Frame-Time, Memory und Spieleranzahl
- **Config-Persistenz** – "Save Config" speichert den aktuellen Zustand auf Disk
- **Server-Shutdown** – Graceful Shutdown über das Dashboard

### REST API Endpunkte

#### Status & System
| Methode | Endpunkt | Beschreibung |
|---------|----------|-------------|
| GET | `/api/status` | Gesamtstatus (Streams, Channels, verfügbare Spiele) |
| POST | `/api/shutdown` | Server herunterfahren |

#### Channels
| Methode | Endpunkt | Beschreibung |
|---------|----------|-------------|
| GET | `/api/channels` | Alle Kanäle auflisten |
| POST | `/api/channels` | Neuen Kanal erstellen |
| PUT | `/api/channels/:id` | Kanal aktualisieren |
| DELETE | `/api/channels/:id` | Kanal löschen |
| POST | `/api/channels/:id/connect` | Kanal verbinden |
| POST | `/api/channels/:id/disconnect` | Kanal trennen |

#### Streams
| Methode | Endpunkt | Beschreibung |
|---------|----------|-------------|
| GET | `/api/streams` | Alle Streams auflisten |
| POST | `/api/streams` | Neuen Stream erstellen |
| PUT | `/api/streams/:id` | Stream aktualisieren |
| DELETE | `/api/streams/:id` | Stream löschen |
| POST | `/api/streams/:id/start` | Streaming starten |
| POST | `/api/streams/:id/stop` | Streaming stoppen |
| POST | `/api/streams/:id/game` | Spiel wechseln `{"game": "...", "mode": "immediate/after_round/after_game"}` |
| POST | `/api/streams/:id/cancel-switch` | Ausstehenden Spielwechsel abbrechen |
| GET | `/api/streams/:id/frame` | JPEG-Frame-Snapshot für Live-Vorschau |

#### Einstellungen & Sonstiges
| Methode | Endpunkt | Beschreibung |
|---------|----------|-------------|
| GET | `/api/games` | Verfügbare Spiele auflisten |
| GET | `/api/settings` | Einstellungen abrufen (Secrets redaktiert) |
| PUT | `/api/settings` | Einstellungen aktualisieren |
| POST | `/api/config/save` | Laufzeit-Zustand in Config speichern |
| POST | `/api/chat` | Lokale Chat-Nachricht senden |
| GET | `/api/chat/log` | Nachrichten-Log abrufen |
| POST | `/api/channels/:id/send` | Nachricht über spezifischen Kanal senden |
| POST | `/api/chat/broadcast` | Nachricht an alle verbundenen Kanäle senden |

#### Scoreboard
| Methode | Endpunkt | Beschreibung |
|---------|----------|-------------|
| GET | `/api/scoreboard/recent?limit=10&hours=24` | Top-Spieler der letzten N Stunden |
| GET | `/api/scoreboard/alltime?limit=5` | All-Time-Leaderboard |
| GET | `/api/scoreboard/player/:id` | Einzelne Spieler-Statistiken |

#### Performance
| Methode | Endpunkt | Beschreibung |
|---------|----------|-------------|
| GET | `/api/perf?seconds=60` | Aktuelle Performance-Durchschnittswerte |
| GET | `/api/perf/history?seconds=300` | Zeitreihen-Daten für Graphen |

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

### Color Conquest

| Befehl | Aliases | Beschreibung |
|--------|---------|-------------|
| `!join [team]` | `!play` | Team beitreten (red/blue/green/yellow oder auto) |
| `!up` | `!u`, `!w`, `!north` | Für Expansion nach oben stimmen |
| `!down` | `!d`, `!s`, `!south` | Für Expansion nach unten stimmen |
| `!left` | `!l`, `!a`, `!west` | Für Expansion nach links stimmen |
| `!right` | `!r`, `!e`, `!east` | Für Expansion nach rechts stimmen |
| `!emote [text]` | — | Team-Emote senden |

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
    bool isRoundComplete() const override { return false; }
    bool isGameOver() const override { return false; }
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

### Phase 1 – Foundation ✅
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

### Phase 1.5 – Multi-Game & Switching ✅
- [x] Color Conquest als zweites Spiel (500+ Spieler)
- [x] Spielwechsel über Web-Dashboard (sofort / nach Runde / nach Spiel)
- [x] REST API für Spielwechsel
- [x] Thread-sicherer deferred Spielwechsel mit Mutex
- [x] Dynamisches Dashboard (spielabhängige Stats, Quick-Buttons, Detail-Ansicht)
- [x] IGame-Interface erweitert (`isRoundComplete()`, `isGameOver()`)
- [x] 66+ Unit-Tests mit doctest

### Phase 2 – Multi-Stream & Multi-Channel (aktuell) ✅
- [x] ChannelManager für mehrere Plattform-Kanäle gleichzeitig
- [x] StreamManager + StreamInstance für mehrere unabhängige Streams
- [x] Game-Modi pro Stream: Fixed, Vote, Random
- [x] Zuschauer-Abstimmung mit Vote-Overlay
- [x] Auflösung pro Stream: Mobile (1080×1920) / Desktop (1920×1080)
- [x] Neues tabbed Web-Dashboard (Streams, Channels, Settings, Chat Test)
- [x] Vollständige REST API für CRUD von Streams und Channels
- [x] Config-Persistenz über Web-Dashboard (Save Config)
- [x] Lazy RenderTexture-Initialisierung (Thread-Safety)
- [x] Message-Routing: Channels → Streams mit Filter

### Phase 2.5 – Scoreboard, Performance & UX ✅
- [x] SQLite Player-Datenbank mit persistentem Scoreboard
- [x] Performance-Monitoring mit Live-Graphen (FPS, Frame-Time, Memory)
- [x] Plattform-Chat direkt über Stream-Card (einzelner Kanal oder Broadcast)
- [x] Inline-Channel-Editing mit plattformspezifischen Einstellungen
- [x] Chat-Test-Tab entfernt (integriert in Stream-Card)
- [x] Scoreboard-Seite im Dashboard (Top 10/24h, Top 5/All-Time, konfigurierbar)
- [x] Performance-Seite mit Recharts-Graphen
- [x] Scoring-Hooks in Chaos Arena (Round-Win, Kill) und Color Conquest (Team-Win)

### Phase 3 – Polish (geplant)
- [ ] Font-Rendering für Spielernamen und HUD-Text
- [ ] GLSL-Shader für Bloom, CRT-Effekt, Chromatic Aberration
- [ ] Sound-System (Hintergrundmusik, SFX)
- [ ] Animierte Spieler-Sprites statt Rectangles
- [ ] Verbesserte Arena-Generierung (prozedural)
- [ ] Chat-Feedback (Bestätigungen an Zuschauer senden)

### Phase 4 – Content
- [ ] Weiteres Spiel: Marble Race
- [ ] Weiteres Spiel: Quiz Battle
- [ ] Weiteres Spiel: Tower Defense
- [ ] Skin-/Cosmetic-System

### Phase 5 – Production
- [ ] Docker-Container für Server-Deployment
- [ ] CI/CD Pipeline
- [ ] Headless-Modus (ohne Fenster)
- [ ] Authentifizierung für Web-Dashboard

---

## 📄 Lizenz

*Noch festzulegen.*

---

## 🤝 Mitwirken

Contributions sind willkommen! Bitte erstelle einen Fork und einen Pull Request.

Für Copilot-Unterstützung siehe [copilot-instructions.md](.github/copilot-instructions.md).
