# InteractiveStreams – Coolify Setup Guide

Diese Anleitung beschreibt das Deployment von InteractiveStreams auf einem Ubuntu-Server mit [Coolify](https://coolify.io).

---

## Voraussetzungen

- **Coolify** installiert und erreichbar (v4+)
- **Server** mit mindestens **4 GB RAM** (der Build braucht ~3 GB für C++ und Next.js)
- **Git-Repository** (GitHub/GitLab) mit dem InteractiveStreams-Code
- **FFmpeg** wird im Container automatisch installiert (kein manuelles Setup nötig)

---

## Schritt 1: Projekt in Coolify anlegen

1. In Coolify auf **„Add Resource"** → **„Docker Compose"** klicken
2. **Git Repository** auswählen (z.B. `github.com/Jan000/InteractiveStreams`)
3. **Branch**: `main`
4. **Docker Compose Location**: `docker-compose.yml` (Standard)
5. Auf **„Save"** klicken

---

## Schritt 2: Build-Einstellungen konfigurieren

### Build-Ressourcen erhöhen (wichtig!)

Der Build kompiliert sowohl ein Next.js-Dashboard als auch eine C++-Anwendung mit mehreren FetchContent-Dependencies. Das braucht Ressourcen.

In den **Service-Einstellungen** von Coolify:

- **Build-Timeout**: Auf mindestens **600 Sekunden** (10 min) erhöhen
  - Der C++-Build mit CMake + FetchContent dauert beim ersten Mal 3–5 Minuten
- Falls der Build mit Out-of-Memory abbricht:
  - Unter **„Advanced" → „Limits"** sicherstellen, dass der Build-Container genug RAM hat (mindestens 3 GB)
  - Alternativ kann ein Swap-File auf dem Server helfen:
    ```bash
    sudo fallocate -l 4G /swapfile
    sudo chmod 600 /swapfile
    sudo mkswap /swapfile
    sudo swapon /swapfile
    echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
    ```

### Kein Build-Argument nötig

Coolify injiziert automatisch `COOLIFY_URL`, `COOLIFY_FQDN` etc. als Build-ARGs. Diese werden von InteractiveStreams nicht verwendet, stören aber auch nicht.

---

## Schritt 3: Netzwerk & Ports

### Port-Konfiguration

InteractiveStreams lauscht auf **Port 8080** (HTTP).

In Coolify unter **„Network"**:
- **Ports Exposes**: `8080`
- **Ports Mappings**: Coolify übernimmt das automatisch über seinen Reverse Proxy

### Domain zuweisen

1. Unter **„Settings" → „Domain"**: Gewünschte Domain eintragen (z.B. `streams.example.com`)
2. Coolify erstellt automatisch ein **Let's Encrypt SSL-Zertifikat**
3. Der Reverse Proxy (Traefik/Caddy) leitet HTTPS → Container:8080 weiter

> **Hinweis**: Das Dashboard ist dann unter `https://streams.example.com` erreichbar.

---

## Schritt 4: Persistent Storage

### SQLite-Datenbanken

InteractiveStreams speichert alle Einstellungen und Spieler-Scores in SQLite-Datenbanken unter `/home/streams/app/data/`:

| Datei | Inhalt |
|-------|--------|
| `settings.db` | Alle Konfigurationen (Channels, Streams, Profiles) |
| `players.db` | Spieler-Scores und Statistiken |

Die `docker-compose.yml` definiert bereits ein **Named Volume** `streams-data`, das Coolify automatisch anlegt:

```yaml
volumes:
  streams-data:
    driver: local
```

> **Wichtig**: Dieses Volume **nicht löschen**, sonst gehen alle Einstellungen und Scores verloren!

### Backup

Für manuelle Backups der SQLite-Datenbanken:
```bash
# Volume-Pfad finden
docker volume inspect e8s84scc8gc0wk84gg000sgc_streams-data

# Dateien kopieren
sudo cp /var/lib/docker/volumes/<volume-name>/_data/*.db ~/backup/
```

Alternativ über das Dashboard: **Settings → Export** erstellt ein JSON-Backup aller Einstellungen.

---

## Schritt 5: Konfiguration nach dem Start

Nach dem ersten erfolgreichen Deploy:

1. **Dashboard öffnen**: `https://<deine-domain>/` oder `http://<server-ip>:8080/`
2. **Channels** konfigurieren:
   - **Local**: Wird automatisch erstellt (zum Testen)
   - **YouTube**: API-Key + Channel-ID eintragen, Stream-URL + Stream-Key setzen
   - **Twitch**: OAuth-Token oder Channel-Name konfigurieren
3. **Streams** konfigurieren:
   - Channels dem Stream zuweisen
   - Game Mode wählen (Fixed/Vote/Random)
   - RTMP-URL wird über die Channel-Settings konfiguriert
4. **Stream starten**: Im Stream-Card auf „Start Streaming" klicken

### API-Key (optional)

Für produktive Deployments einen API-Key setzen:
1. Dashboard → **Settings** → **API Key** Feld ausfüllen
2. Den gleichen Key im Browser unter **Settings** eingeben
3. Alle API-Aufrufe werden dann authentifiziert

---

## Troubleshooting

### Build schlägt bei `bun run build` fehl

**Symptom**: `process "/bin/sh -c bun run build" did not complete successfully: exit code: 1`

**Mögliche Ursachen**:
1. **Zu wenig RAM** beim Build → Swap-File anlegen (siehe oben)
2. **Build-Timeout** zu niedrig → Auf 600s erhöhen
3. **Lockfile-Inkompatibilität** → Das Dockerfile nutzt bereits einen Fallback: `bun install --frozen-lockfile || bun install`

**Debug**: In Coolify unter „Deployments" → „Logs" nachschauen. Der genaue Fehler von `next build` steht dort.

### Container startet, aber Dashboard ist nicht erreichbar

1. Unter **„Network" → Port Exposes** prüfen, ob `8080` eingetragen ist
2. Health Check abwarten (ca. 15 Sekunden nach Start)
3. Container Logs prüfen: `Entering main loop at 60 FPS target` sollte erscheinen

### „Cannot start streaming: no assigned channels have a stream URL"

Die RTMP-URL muss in den **Channel-Settings** konfiguriert werden, nicht im Stream selbst:
1. Dashboard → **Channels** Tab
2. YouTube/Twitch Channel bearbeiten
3. **Stream URL** + **Stream Key** eintragen
4. Speichern
5. Dann im **Streams** Tab den Channel dem Stream zuweisen

### YouTube zeigt „Keine Daten" obwohl der Stream gestartet ist

Ab Version nach Commit `14fcbf5` enthält der RTMP-Stream automatisch eine stille Audio-Spur. YouTube erfordert Audio im RTMP-Feed. Sicherstellen, dass die aktuelle Version deployed ist.

### YouTube Chat-Verbindung: „search.list failed (HTTP 403)"

Die YouTube Data API v3 hat ein tägliches Quota-Limit (10.000 Units). Wenn das erschöpft ist:
- Der Chat-Poll-Loop wartet automatisch und versucht es alle 30 Sekunden erneut
- Alternativ: `live_chat_id` manuell konfigurieren (umgeht die search.list API)
- YouTube API-Key in der Google Cloud Console prüfen und ggf. Quota erhöhen

---

## Architektur im Container

```
/home/streams/app/
├── InteractiveStreams      # Kompilierte Binary
├── assets/
│   ├── audio/              # Hintergrundmusik
│   ├── fonts/              # UI-Fonts
│   └── shaders/            # GLSL Post-Processing Shader
├── config/
│   └── default.json        # Standard-Konfiguration (nur Erststart)
├── dashboard/              # Next.js Static Export (Web UI)
└── data/                   # ← Persistentes Volume
    ├── settings.db         # Konfigurationen
    └── players.db          # Spieler-Scores
```

- **Xvfb** läuft als virtuelles Display (`:99`) weil SFML einen X-Server braucht, auch im headless-Modus
- **FFmpeg** wird für jeden aktiven RTMP-Stream als Kindprozess gestartet
- **Port 8080** bedient sowohl das Dashboard (statische Dateien) als auch die REST API

---

## Aktualisieren

1. Neuen Code nach `main` pushen
2. In Coolify: **„Redeploy"** klicken (oder Auto-Deploy aktivieren via Webhook)
3. Coolify baut das Image neu und startet den Container
4. Das `streams-data` Volume bleibt erhalten → Einstellungen und Scores bleiben bestehen
