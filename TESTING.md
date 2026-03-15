# Testanleitung – InteractiveStreams

Diese Anleitung beschreibt, wie du das Spiel lokal testen, mehrere Spieler simulieren und automatisierte Tests ausführen kannst.

---

## Inhaltsverzeichnis

1. [Voraussetzungen](#voraussetzungen)
2. [Bauen & Starten](#bauen--starten)
3. [Lokales Testen (Einzelspieler)](#lokales-testen-einzelspieler)
4. [Multi-Player-Test (Dashboard)](#multi-player-test-dashboard)
5. [Spiel wechseln (Dashboard & API)](#spiel-wechseln-dashboard--api)
6. [Chat bedienen – alle Wege](#chat-bedienen--alle-wege)
7. [REST-API direkt nutzen (curl)](#rest-api-direkt-nutzen-curl)
8. [Spielablauf testen](#spielablauf-testen)
9. [Auto-Play & Bot-Simulation](#auto-play--bot-simulation)
10. [Automatisierte Tests](#automatisierte-tests)
11. [Fehlerbehebung](#fehlerbehebung)

---

## Voraussetzungen

| Tool | Version | Zweck |
|------|---------|-------|
| Visual Studio 2022 | 17.x | C++20-Compiler (MSVC) |
| CMake | ≥ 3.20 | Build-System |
| Git | beliebig | Quellcode & FetchContent |
| FFmpeg | beliebig | *(Optional)* Nur für RTMP-Streaming |
| Browser | beliebig | Web-Dashboard auf `http://localhost:8080` |

> **Hinweis:** FFmpeg wird nur benötigt, wenn du tatsächlich zum Streaming aktivierst. Für rein lokales Testen reicht das SFML-Vorschaufenster.

---

## Bauen & Starten

### Erstmalig konfigurieren
```powershell
cd C:\Users\janki\Repos\InteractiveStreams
cmake --preset msvc-debug
```

### Kompilieren (Debug)
```powershell
cmake --build build --config Debug
```

### Starten
```powershell
.\build\Debug\InteractiveStreams.exe
```

> Beim Start passieren automatisch:
> - SFML-Fenster öffnet sich (Live-Vorschau des ersten Streams)
> - Web-Dashboard wird auf **http://localhost:8080** gestartet
> - „Local“-Kanal verbindet sich automatisch
> - Der Default-Stream wird mit dem konfigurierten Game-Modus geladen

---

## Lokales Testen (Einzelspieler)

### Konsole (Terminal)
Im selben Terminal, in dem die Anwendung läuft, kannst du direkt Befehle tippen:

```
join              → Tritt als "console" bei
left              → Bewege dich nach links
jump              → Springe
attack            → Nahkampfangriff
```

Um als anderer Benutzer zu schreiben, verwende das Klammerformat:
```
[Alice] join       → Alice tritt bei
[Bob] join         → Bob tritt bei
[Alice] attack     → Alice greift an
[Bob] jump         → Bob springt
```

### Web-Dashboard (empfohlen)
1. Öffne **http://localhost:8080** im Browser
2. Scrolle zum Abschnitt **„Local Chat (Test)“**
3. Gib im Eingabefeld einen Befehl ein (z.B. `join`)
4. Klicke **Send** oder drücke **Enter**

---

## Multi-Player-Test (Dashboard)

Das Dashboard enthält ein spezielles Multi-Player-Testsystem mit Player-Tabs:

### Player-Tabs
Oben im Chat-Bereich siehst du vier vorkonfigurierte Spieler:
- 🔵 **Player1** (blau)
- 🟢 **Player2** (grün)
- 🟡 **Player3** (gelb)
- 🟣 **Player4** (lila)

**So verwendest du sie:**
1. Klicke auf einen Player-Tab (z.B. **Player2**)
2. Der Username-Feld wird automatisch auf diesen Spieler gesetzt
3. Nutze die Quick-Buttons oder tippe einen Befehl
4. Der Befehl wird im Namen dieses Spielers gesendet

### Eigene Spieler hinzufügen
Klicke auf den **+** Button neben den Tabs:
- Gib einen benutzerdefinierten Namen ein
- Ein neuer Tab wird erstellt mit zufälliger Farbe

### Bulk-Aktionen
Unterhalb der Quick-Buttons findest du drei Bulk-Aktionen:

| Button | Funktion |
|--------|----------|
| **👥 Join All** | Alle Spieler (Player1–Player4 + eigene) treten dem Spiel bei |
| **🎲 Random Actions** | Jeder Spieler führt eine zufällige Aktion aus |
| **🤖 Auto-Play** | Toggelt automatisches Spielen: Bots senden alle 500ms zufällige Befehle |

### Typischer Multi-Player-Testablauf
1. Öffne http://localhost:8080
2. Klicke **👥 Join All** → alle 4 Spieler treten bei
3. Warte auf Countdown (3 Sekunden nach genug Spielern)
4. Klicke **🤖 Auto-Play: OFF** → Auto-Play startet
5. Beobachte das Spiel im SFML-Fenster
6. Klicke erneut auf **🤖 Auto-Play: ON** zum Stoppen
7. Wechsle manuell zwischen Spielern per Tab-Klick

---

## Spiel wechseln (Dashboard & API)

Das System unterstützt **Laufzeit-Spielwechsel** über drei Modi:

### Über das Dashboard

1. Öffne http://localhost:8080
2. Im **Streams**-Tab:
   - Wähle einen Stream aus (z.B. „default“)
   - Wähle das Zielspiel aus dem Dropdown
   - Wähle den Wechsel-Modus:
     - ⚡ **Sofort** – Wechsel erfolgt unmittelbar
     - ⏳ **Nach Runde** – Wechsel nach Abschluss der aktuellen Runde
     - 🏁 **Nach Spiel** – Wechsel nach Game Over
   - Klicke **„Switch“**
3. Der Game-Modus (Fixed/Vote/Random) kann pro Stream gewählt werden
4. Die Auflösung (Mobile/Desktop) kann pro Stream geändert werden

### Über REST-API

```powershell
# Sofort wechseln (Stream-ID "default")
curl -X POST http://localhost:8080/api/streams/default/game `
     -H "Content-Type: application/json" `
     -d '{"game":"color_conquest","mode":"immediate"}'

# Nach aktueller Runde wechseln
curl -X POST http://localhost:8080/api/streams/default/game `
     -H "Content-Type: application/json" `
     -d '{"game":"chaos_arena","mode":"after_round"}'

# Nach Spielende wechseln
curl -X POST http://localhost:8080/api/streams/default/game `
     -H "Content-Type: application/json" `
     -d '{"game":"color_conquest","mode":"after_game"}'

# Ausstehenden Wechsel abbrechen
curl -X POST http://localhost:8080/api/streams/default/cancel-switch
```

### Was du testen solltest
1. **Sofort-Wechsel:** Während Battle-Phase wechseln → Spiel wird sofort ersetzt
2. **Nach-Runde-Wechsel:** Verzögerten Wechsel setzen, warten bis Runde endet → automatischer Wechsel
3. **Nach-Spiel-Wechsel:** Verzögerten Wechsel setzen, ganzes Spiel durchspielen → Wechsel bei Game Over
4. **Abbrechen:** Verzögerten Wechsel setzen, dann abbrechen → Banner verschwindet
5. **Dashboard-Aktualisierung:** Nach Wechsel müssen Quick-Buttons, Stats und Detail-Ansicht zum neuen Spiel passen

---

## Chat bedienen – alle Wege

Es gibt **drei Wege**, Chat-Nachrichten an das Spiel zu senden:

### 1. Web-Dashboard (GUI)
- **URL:** http://localhost:8080
- **Bereich:** „Local Chat (Test)"
- **Features:** Player-Tabs, Quick-Buttons, Auto-Play
- **Empfohlen für:** Komfortables Multi-Player-Testing

### 2. Konsoleneingabe (Terminal)
- **Wo:** Im Terminal, in dem `InteractiveStreams.exe` läuft
- **Format:** `!befehl` oder `[username] !befehl`
- **Aktivierung:** `config/default.json` → `platforms.local.console_input: true`
- **Empfohlen für:** Schnelle Einzeltests

### 3. REST-API (programmatisch)
- **Endpoint:** `POST http://localhost:8080/api/chat`
- **Body:** `{"username": "Name", "text": "join"}`
- **Empfohlen für:** Automatisierung, Scripting, externe Tools

---

## REST-API direkt nutzen (curl)

### Nachricht senden
```powershell
# Spieler "Alice" tritt bei
curl -X POST http://localhost:8080/api/chat `
     -H "Content-Type: application/json" `
     -d '{"username":"Alice","text":"join"}'

# Spieler "Bob" greift an
curl -X POST http://localhost:8080/api/chat `
     -H "Content-Type: application/json" `
     -d '{"username":"Bob","text":"attack"}'
```

### Spielstatus abfragen
```powershell
# Vollständiger System-Status (Streams, Channels, Games)
curl http://localhost:8080/api/status

# Verfügbare Spiele auflisten
curl http://localhost:8080/api/games

# Alle Streams auflisten
curl http://localhost:8080/api/streams

# Alle Kanäle auflisten
curl http://localhost:8080/api/channels

# Chat-Log abrufen
curl http://localhost:8080/api/chat/log

# Einstellungen abrufen
curl http://localhost:8080/api/settings

# Einstellungen ändern (wird automatisch in SQLite persistiert)
curl -X PUT http://localhost:8080/api/settings `
     -H "Content-Type: application/json" `
     -d '{"application":{"target_fps":30}}'

# Config-Backup als JSON-Datei schreiben
curl -X POST http://localhost:8080/api/config/save
```

### Spiel wechseln
```powershell
# Sofort zu Color Conquest wechseln (default-Stream)
curl -X POST http://localhost:8080/api/streams/default/game `
     -H "Content-Type: application/json" `
     -d '{"game":"color_conquest","mode":"immediate"}'

# Nach Runde zurück zu Chaos Arena
curl -X POST http://localhost:8080/api/streams/default/game `
     -H "Content-Type: application/json" `
     -d '{"game":"chaos_arena","mode":"after_round"}'

# Ausstehenden Wechsel abbrechen
curl -X POST http://localhost:8080/api/streams/default/cancel-switch
```

### PowerShell-Skript: Mehrere Spieler automatisch joinen
```powershell
$players = @("Alice", "Bob", "Charlie", "Diana")
foreach ($p in $players) {
    Invoke-RestMethod -Uri "http://localhost:8080/api/chat" `
        -Method Post -ContentType "application/json" `
        -Body "{`"username`":`"$p`",`"text`":`"join`"}"
    Start-Sleep -Milliseconds 200
}
```

---

## Spielablauf testen

### Chat-Befehle (Chaos Arena)

| Befehl | Aliase | Beschreibung |
|--------|--------|-------------|
| `join` | `play` | Spiel beitreten |
| `left` | `l`, `a` | Nach links bewegen |
| `right` | `r`, `d` | Nach rechts bewegen |
| `jump` | `j`, `w`, `up` | Springen (Doppelsprung möglich) |
| `jumpleft` | `jl` | Springen nach links |
| `jumpright` | `jr` | Springen nach rechts |
| `attack` | `hit`, `atk` | Nahkampfangriff (0.5s Cooldown) |
| `special` | `sp`, `ult` | Projektil abfeuern (5s Cooldown) |
| `dash` | `dodge` | Dash mit I-Frames (3s Cooldown) |
| `block` | `shield`, `def` | Block aktivieren (1.5s Dauer) |
| `emote` | — | Emote auslösen (z.B. `emote wave`) |

### Chat-Befehle (Color Conquest)

| Befehl | Aliase | Beschreibung |
|--------|--------|-------------|
| `join [team]` | `play` | Team beitreten (red/blue/green/yellow oder auto) |
| `up` | `u`, `w`, `north` | Für Expansion nach oben stimmen |
| `down` | `d`, `s`, `south` | Für Expansion nach unten stimmen |
| `left` | `l`, `a`, `west` | Für Expansion nach links stimmen |
| `right` | `r`, `e`, `east` | Für Expansion nach rechts stimmen |
| `emote [text]` | — | Team-Emote senden |

### Chat-Befehle (Gravity Brawl)

| Befehl | Aliase | Beschreibung |
|--------|--------|-------------|
| `join [farbe]` | `play` | Beitreten (optional: red, blue, green, yellow, #RRGGBB) |
| `smash` | `s` | Dash/Ram-Angriff (0.8s Cooldown) |

> **Supernova:** 5 aufeinanderfolgende Smashes innerhalb von 3 Sekunden lösen eine Supernova (großer AoE-Knockback) aus.

### Spielphasen (Chaos Arena)

| Phase | Beschreibung | Trigger |
|-------|-------------|---------|
| **Lobby** | Warten auf Spieler | Start / nach GameOver |
| **Countdown** | 3s Countdown vor Kampf | ≥ 2 Spieler beigetreten |
| **Battle** | Aktiver Kampf | Countdown abgelaufen |
| **RoundEnd** | Rundenwechsel | ≤ 1 Spieler übrig oder Timer |
| **GameOver** | Endergebnis | Alle Runden gespielt |

### Spielphasen (Color Conquest)

| Phase | Beschreibung | Trigger |
|-------|-------------|--------|
| **Lobby** | Warten auf Spieler, Teams wählen | Start / nach GameOver |
| **Countdown** | 3s Countdown vor der Runde | ≥ 2 Spieler beigetreten |
| **Voting** | Teams stimmen über Richtung ab (8s) | Countdown abgelaufen |
| **RoundEnd** | Ergebnis der Expansion anzeigen | Abstimmungszeit vorbei |
| **GameOver** | Endergebnis – Team mit meisten Zellen gewinnt | 30 Runden gespielt |

### Spielphasen (Gravity Brawl)

| Phase | Beschreibung | Trigger |
|-------|-------------|--------|
| **Lobby** | Warten auf Spieler, Bots füllen auf | Start / nach GameOver |
| **Countdown** | 3s Countdown vor dem Kampf | ≥ 2 Spieler (inkl. Bots) |
| **Playing** | Orbitaler Kampf, Cosmic Events, Black-Hole-Gravitation | Countdown abgelaufen |
| **GameOver** | Letzter Planet überlebt – Gewinner | ≤ 1 Planet übrig oder Epoch-Timer abgelaufen |

### Was du testen solltest
1. **Join-Mechanik:** Tritt mit mehreren Spielern bei, prüfe Leaderboard
2. **Bewegung:** Links/Rechts/Jump, Doppelsprung, Plattform-Interaktion
3. **Kampf:** Attack/Special/Dash/Block, Damage-Werte, Kill-Feed
4. **Physik:** Schwerkraft, Kollisionen, Arena-Grenzen (Spieler fallen raus → Tod)
5. **PowerUps:** Erscheinen zufällig, Speed/Damage-Boost, Schild
6. **Phasen:** Lobby → Countdown → Battle → RoundEnd → GameOver
7. **UI:** HUD-Anzeigen, Timer, Player-Liste, Leaderboard

### Color Conquest – Was du testen solltest
1. **Team-Join:** `join red`, `join blue`, `join` (Auto-Zuweisung)
2. **Abstimmung:** `up`, `down`, `left`, `right` während Voting-Phase
3. **Expansion:** Grenzzellen werden erobert, Gebiete wachsen
4. **Rundenwechsel:** 30 Runden durchspielen, Ergebnis prüfen
5. **Team-Balance:** Auto-Zuweisung weist kleinsten Teams zu
6. **Skalierung:** Viele Spieler über Auto-Play testen (500+)
7. **Spielwechsel:** Während Color Conquest zu Chaos Arena wechseln und umgekehrt

### Gravity Brawl – Was du testen solltest
1. **Join-Mechanik:** `join`, `join red`, `join #FF0000` (Farb-Auswahl)
2. **Smash:** `smash` / `s` – Impulse, Cooldown (0.8s), Richtung
3. **Supernova:** 5 schnelle Smashes → AoE-Knockback
4. **Orbitale Physik:** Planeten kreisen um das schwarze Loch, bleiben im stabilen Band
5. **Black-Hole-Gravitation:** Wächst über Zeit, zieht Planeten langsam rein
6. **Planet-Tier-System:** Kills → Asteroid → IcePlanet → GasGiant → Star
7. **King/Bounty:** Planet mit meisten Kills bekommt Krone und Bounty
8. **Cosmic Events:** Gravitationsänderungen während des Spiels
9. **Bot Fill:** Bots füllen Lobby auf Minimum-Spielerzahl auf
10. **Stream-Events:** Subscribe/Bits/Super Chat → Belohnungen (Schild, God Mode)
11. **Per-Game Settings:** `GET/PUT /api/games/gravity_brawl/settings` – 40+ Parameter

---

## Auto-Play & Bot-Simulation

### Dashboard Auto-Play
Klicke **🤖 Auto-Play** im Dashboard, um automatisches Spielen zu aktivieren:
- Alle 500ms senden 1–3 zufällige Spieler einen zufälligen Befehl
- Nützlich für Stresstests und visuelle Überprüfung

### PowerShell Bot-Skript
Für intensiveres Testing kannst du ein PowerShell-Skript verwenden:

```powershell
# bot-test.ps1 – Simuliert 4 Bots für 60 Sekunden
$players = @("Bot1", "Bot2", "Bot3", "Bot4")
$commands = @("left", "right", "jump", "attack", "special", "dash", "block")

# Alle Bots joinen lassen
foreach ($p in $players) {
    Invoke-RestMethod -Uri "http://localhost:8080/api/chat" `
        -Method Post -ContentType "application/json" `
        -Body "{`"username`":`"$p`",`"text`":`"join`"}"
    Start-Sleep -Milliseconds 100
}

# 60 Sekunden lang random commands senden
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
while ($stopwatch.Elapsed.TotalSeconds -lt 60) {
    $p = $players | Get-Random
    $cmd = $commands | Get-Random
    Invoke-RestMethod -Uri "http://localhost:8080/api/chat" `
        -Method Post -ContentType "application/json" `
        -Body "{`"username`":`"$p`",`"text`":`"$cmd`"}"
    Start-Sleep -Milliseconds 200
}
Write-Host "Bot-Simulation beendet."
```

---

## Automatisierte Tests

Das Projekt enthält Unit-Tests mit dem **doctest** Test-Framework.

### Tests bauen & ausführen
```powershell
# Konfigurieren mit Tests
cmake --preset msvc-debug -DIS_BUILD_TESTS=ON

# Bauen
cmake --build build --config Debug

# Tests ausführen
cd build
ctest --output-on-failure -C Debug
```

### Alternativ: Test-Executable direkt starten
```powershell
.\build\Debug\InteractiveStreams_Tests.exe
```

### Was wird getestet?
| Testbereich | Beschreibung |
|-------------|-------------|
| **Config** | Dotted-Key-Navigation, Get/Set, Defaults, Reload |
| **ChatMessage** | UserId-Generierung, Struct-Initialisierung |
| **Player** | Timer-Updates, Cooldowns, Invulnerability, Damage-Berechnung |
| **PhysicsWorld** | Welt-Erstellung, Body-Erstellung, Gravitation, Bodenerkennung |
| **GameRegistry** | Registrierung, Listing, Duplikat-Handling |
| **LocalPlatform** | Message-Injection, Polling, Konsolen-Format-Parsing |
| **Grid** | Grid-Erstellung, Zell-Zuordnung, Expansion, Grenzerkennung |
| **ColorConquest** | TeamData-Voting, Vote-Tallying, Tie-Break, clearVotes |
| **GravityBrawl – Config** | Settings Round-Trip, Clamping, Bot Fill, getState/getCommands |
| **GravityBrawl – Physics** | Orbit-Stabilität, Black-Hole-Gravitation, Velocity-Clamping, Kollisionen, Cosmic Events |
| **GravityBrawl – Gameplay** | Lifecycle, Join/Smash-Commands, Supernova, King/Bounty, Kill-Attribution, Tier-Evolution, Leaderboard |
| **GravityBrawl – Rendering** | Pixel-Output, Partikel-Erzeugung, Partikel-Limits, Multi-Frame-Stabilität |
| **GravityBrawl – Stress** | 12/20/30-Spieler-Simulationen über 45s bis 2 Minuten |

---

## Docker-Deployment testen

### Container starten
```bash
docker compose up -d
docker compose logs -f app
```

### Funktionstest
1. Öffne `http://localhost:8080` im Browser
2. Prüfe, ob das Dashboard lädt
3. Teste Chat über API: `curl -X POST http://localhost:8080/api/chat -H "Content-Type: application/json" -d '{"username":"DockerTest","text":"join"}'`

### Container stoppen
```bash
docker compose down
```

---

## API-Authentifizierung testen

### Password-Login (empfohlen)
1. Öffne Dashboard → beim ersten Start wird automatisch Setup-Seite gezeigt
2. Erstelle ein Passwort → Session-Token wird automatisch gespeichert
3. Alle API-Requests enthalten automatisch `Authorization: Bearer <token>`

### API-Key setzen (Legacy)
1. Öffne Dashboard → **Settings → Web Server**
2. Gib einen API-Key ein und klicke **Save**
3. Oder direkt in der Config: `"web": { "api_key": "test-key" }`

### Ohne Key (→ 401 erwartet)
```bash
curl -v http://localhost:8080/api/status
# → HTTP 401 Unauthorized
```

### Mit Key (→ 200 erwartet)
```bash
# Via Authorization Header
curl -H "Authorization: Bearer test-key" http://localhost:8080/api/status

# Via X-API-Key Header
curl -H "X-API-Key: test-key" http://localhost:8080/api/status

# Via Query-Parameter
curl "http://localhost:8080/api/status?api_key=test-key"
```

### Key leeren (Auth deaktivieren)
Setze `web.api_key` auf `""` → alle API-Requests sind wieder offen.

---

## Fehlerbehebung

### SFML-Fenster öffnet sich nicht
- Prüfe, ob die SFML-DLLs im Build-Ordner liegen (`sfml-graphics-d-2.dll` etc.)
- Führe `cmake --build build --config Debug` erneut aus (kopiert DLLs automatisch)

### Dashboard nicht erreichbar
- Prüfe, ob Port 8080 frei ist: `netstat -an | findstr 8080`
- Prüfe die Konsolenausgabe auf `[Web] Server started on :8080`

### Spieler treten nicht bei
- Befehle funktionieren mit und ohne `!`-Prefix (z.B. `join` oder `!join`)
- Prüfe im Dashboard, ob Status „Online" (grüner Punkt) angezeigt wird
- API-Test: `curl http://localhost:8080/api/status`

### Build-Fehler
- `C1041`: `/FS` Flag ist gesetzt – lösche ggf. `build/` und konfiguriere neu
- FetchContent: Lösche `build/_deps/` und konfiguriere neu
- PCH-Fehler: Lösche `build/CMakeFiles/` und baue neu

### Konsoleneingabe reagiert nicht
- Prüfe `config/default.json` → `platforms.local.console_input` muss `true` sein
- Die Konsole blockiert möglicherweise bei `std::getline` – nutze stattdessen das Dashboard
