# syntax=docker/dockerfile:1
# ══════════════════════════════════════════════════════════════════════════════
# InteractiveStreams – Multi-stage Docker Build
# ══════════════════════════════════════════════════════════════════════════════
# Produces a minimal runtime image with the compiled binary, assets,
# dashboard static files, and FFmpeg for RTMP streaming.
#
# Build:   docker build -t interactive-streams .
# Run:     docker run -p 8080:8080 interactive-streams
# Compose: docker compose up
# ══════════════════════════════════════════════════════════════════════════════

# ── Stage 1: Build the web dashboard ─────────────────────────────────────────
FROM oven/bun:1 AS dashboard-builder
WORKDIR /app/web
COPY web/package.json web/bun.lock* ./
# Cache bun's package store between builds (never re-downloads unchanged packages)
RUN --mount=type=cache,id=is-bun-cache,target=/root/.bun/install/cache \
    bun install --frozen-lockfile || bun install
COPY web/ .
ENV NODE_OPTIONS="--max-old-space-size=4096"
# Cache Next.js incremental build artifacts between builds
RUN --mount=type=cache,id=is-nextjs-cache,target=/app/web/.next/cache \
    bun run build

# ── Stage 2: Build the C++ application ───────────────────────────────────────
FROM ubuntu:24.04 AS cpp-builder

ENV DEBIAN_FRONTEND=noninteractive

# Cache APT package lists and downloaded .deb files between builds
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates \
    libx11-dev libxrandr-dev libxcursor-dev libxi-dev \
    libudev-dev libgl1-mesa-dev libfreetype-dev libopenal-dev libvorbis-dev libflac-dev

WORKDIR /app

# Copy all sources (src/ must exist at configure time for add_executable validation)
COPY CMakeLists.txt CMakePresets.json ./
COPY src/ src/
COPY config/ config/
COPY assets/ assets/
COPY --from=dashboard-builder /app/web/out/ web/out/

# Configure + Build with FetchContent cache mount.
# FETCHCONTENT_BASE_DIR points to a persistent BuildKit cache volume so that
# all external dependencies (SFML, Box2D, spdlog, etc.) are downloaded and
# compiled ONCE and reused across every subsequent build – even when src/ changes.
# Only the application code itself is re-compiled on source changes.
RUN --mount=type=cache,id=is-fetchcontent,target=/fetchcontent-cache \
    cmake -B build \
          -DCMAKE_BUILD_TYPE=Release \
          -DIS_BUILD_TESTS=OFF \
          -DFETCHCONTENT_BASE_DIR=/fetchcontent-cache \
          -DSFML_BUILD_SHARED_LIBS=OFF \
          -DBUILD_SHARED_LIBS=OFF \
    && cmake --build build --config Release -j$(nproc)

# ── Stage 3: Runtime image ───────────────────────────────────────────────────
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
    ffmpeg xvfb curl \
    libx11-6 libxrandr2 libxcursor1 libxi6 \
    libudev1 libgl1 libfreetype6 libopenal1 libvorbis0a libvorbisenc2 libvorbisfile3 libflac12

# Create non-root user
RUN useradd -m -s /bin/bash streams
WORKDIR /home/streams/app

# Copy compiled binary and runtime assets
COPY --from=cpp-builder /app/build/InteractiveStreams ./
COPY --from=cpp-builder /app/build/assets/ assets/
COPY --from=cpp-builder /app/build/config/ config/
COPY --from=cpp-builder /app/build/dashboard/ dashboard/

# Data directory for SQLite databases (persist via volume)
# Pre-create /tmp/.X11-unix with sticky bit so Xvfb can use it as non-root user
RUN mkdir -p data /tmp/.X11-unix \
    && chmod 1777 /tmp/.X11-unix \
    && chown -R streams:streams /home/streams/app

USER streams

EXPOSE 8080

# Health check for monitoring / Coolify
HEALTHCHECK --interval=15s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -sf http://localhost:8080/api/perf?seconds=5 || exit 1

# Default: headless mode with Xvfb (SFML needs a display even when headless)
# Override entrypoint for non-headless use with a real X server.
ENV DISPLAY=:99
ENTRYPOINT ["sh", "-c", "Xvfb :99 -screen 0 1920x1080x24 &>/dev/null & sleep 0.5 && exec ./InteractiveStreams"]
