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
# Only compiles the binary.  Assets, config, and dashboard are copied directly
# into the runtime stage from the build context / dashboard-builder – this is
# more reliable than depending on CMake POST_BUILD copy commands in Docker and
# also improves caching (asset/config changes no longer invalidate the C++ build).
FROM ubuntu:24.04 AS cpp-builder

ARG GIT_COMMIT_HASH=
ARG SOURCE_COMMIT=
ARG COOLIFY_GIT_COMMIT_SHA=
ARG COMMIT_SHA=
ARG GITHUB_SHA=

ENV DEBIAN_FRONTEND=noninteractive

# Cache APT package lists and downloaded .deb files between builds
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates mold ccache \
    libx11-dev libxrandr-dev libxcursor-dev libxi-dev \
    libudev-dev libgl1-mesa-dev libfreetype-dev libopenal-dev libvorbis-dev libflac-dev

# ── ccache: dramatically speeds up recompilation ──────────────────────────
# ccache intercepts compiler calls and returns cached results when the
# source + flags haven't changed.  Even when Docker invalidates the build
# layer (e.g. because src/ changed), ccache still has the compiled objects
# for all unchanged translation units.  This turns a 15-min full rebuild
# into seconds for typical one-file changes.
ENV CCACHE_DIR=/ccache
ENV CCACHE_MAXSIZE=2G
ENV CCACHE_COMPRESS=1
ENV CMAKE_C_COMPILER_LAUNCHER=ccache
ENV CMAKE_CXX_COMPILER_LAUNCHER=ccache

WORKDIR /app

# ── Step 1: CMake configure (cached when CMakeLists.txt unchanged) ────────
# Only build-system files and source code are needed for compilation.
# assets/, config/, and web/out/ are NOT copied here – they go directly
# to the runtime stage, which decouples asset changes from C++ rebuilds.
COPY CMakeLists.txt CMakePresets.json ./
COPY src/ src/
COPY cmake/ cmake/

# Configure with FetchContent cache mount.
# FETCHCONTENT_BASE_DIR points to a persistent BuildKit cache volume so that
# all external dependencies (SFML, Box2D, spdlog, gRPC, etc.) are downloaded
# and compiled ONCE and reused across every subsequent build.
RUN --mount=type=cache,id=is-fetchcontent,target=/fetchcontent-cache \
    export EFFECTIVE_GIT_HASH="${GIT_COMMIT_HASH:-${SOURCE_COMMIT:-${COOLIFY_GIT_COMMIT_SHA:-${COMMIT_SHA:-${GITHUB_SHA}}}}}" && \
    cmake -B build \
          -DCMAKE_BUILD_TYPE=Release \
          -DIS_BUILD_TESTS=OFF \
          -DGIT_COMMIT_HASH_OVERRIDE="${EFFECTIVE_GIT_HASH}" \
          -DFETCHCONTENT_BASE_DIR=/fetchcontent-cache \
          -DSFML_BUILD_SHARED_LIBS=OFF \
          -DBUILD_SHARED_LIBS=OFF \
          -DCMAKE_C_COMPILER_LAUNCHER=ccache \
          -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
          -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold"

# ── Step 2: Build (ccache reuses unchanged object files across rebuilds) ──
# The ccache mount persists compiled objects between Docker builds.
# Combined with FetchContent cache, only actually-changed TUs are recompiled.
# POST_BUILD copy commands (CopyIfStale) will silently skip because assets/
# config/ are not present – that's expected; we copy them in the runtime stage.
RUN --mount=type=cache,id=is-fetchcontent,target=/fetchcontent-cache \
    --mount=type=cache,id=is-ccache,target=/ccache \
    cmake --build build --config Release -j$(nproc)

# ── Stage 3: Runtime image ───────────────────────────────────────────────────
FROM ubuntu:24.04 AS runtime

ARG GIT_COMMIT_HASH=
ARG SOURCE_COMMIT=
ARG COOLIFY_GIT_COMMIT_SHA=
ARG COMMIT_SHA=
ARG GITHUB_SHA=

ENV DEBIAN_FRONTEND=noninteractive

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
    ffmpeg xvfb curl ca-certificates \
    libx11-6 libxrandr2 libxcursor1 libxi6 \
    libudev1 libgl1 libgl1-mesa-dri libfreetype6 libopenal1 libvorbis0a libvorbisenc2 libvorbisfile3 libflac12

# Create non-root user and writable app directory
# The app dir must be owned by streams so the process can create runtime files
# (ffmpeg_stderr.log, etc.) alongside the binary.
RUN useradd -m -s /bin/bash streams \
    && mkdir -p /home/streams/app \
    && chown streams:streams /home/streams/app
WORKDIR /home/streams/app

# ── Copy runtime files, ordered by change frequency (least → most) ────────
# This maximizes Docker layer cache hits: a C++ code change only invalidates
# the binary layer and below, not the config/assets/dashboard layers above.

# 1. Config (rarely changes) – owned by streams, patched for headless mode
COPY --chown=streams:streams config/ config/
RUN sed -i 's/"headless": false/"headless": true/' config/default.json \
    && sed -i 's/"muted": false/"muted": true/' config/default.json \
    && grep -E '"headless"|"muted"' config/default.json

# 2. Assets (rarely changes)
COPY --chown=streams:streams assets/ assets/

# 3. Dashboard from dashboard-builder (changes when web/src changes)
COPY --from=dashboard-builder --chown=streams:streams /app/web/out/ dashboard/

# 4. Compiled binary from cpp-builder (changes most often – C++ code changes)
COPY --from=cpp-builder --chown=streams:streams /app/build/InteractiveStreams ./

# Runtime directories (data = SQLite via volume, logs = spdlog output)
# Pre-create /tmp/.X11-unix with sticky bit so Xvfb can use it as non-root user
# mkdir runs as root, so explicitly chown the app-level dirs to streams.
RUN mkdir -p data logs /tmp/.X11-unix \
    && chown streams:streams data logs \
    && chmod 1777 /tmp/.X11-unix

USER streams

# Force Mesa software renderer – no GPU in the container.
# Without this, SFML fails to create an OpenGL context under Xvfb even with
# libgl1-mesa-dri installed, because Mesa tries hardware acceleration first
# and falls back to nothing instead of llvmpipe.
ENV LIBGL_ALWAYS_SOFTWARE=1
ENV GALLIUM_DRIVER=llvmpipe
ENV GIT_COMMIT_HASH=${GIT_COMMIT_HASH}
ENV SOURCE_COMMIT=${SOURCE_COMMIT}
ENV COOLIFY_GIT_COMMIT_SHA=${COOLIFY_GIT_COMMIT_SHA}
ENV COMMIT_SHA=${COMMIT_SHA}
ENV GITHUB_SHA=${GITHUB_SHA}
# Force OpenAL-Soft to use the null (silent) audio backend.
# Without this, OpenAL-Soft calls abort() when no audio device is found
# (ALSA/PulseAudio are not present in the container).
ENV ALSOFT_DRIVERS=null
# Force headless mode at the application level, regardless of what the
# config/SQLite says.  A config-import with "headless": false must not
# create an sf::RenderWindow inside a container.
ENV IS_HEADLESS=1

EXPOSE 8080

# Health check for monitoring / Coolify
HEALTHCHECK --interval=15s --timeout=5s --start-period=30s --retries=3 \
    CMD curl -sf http://localhost:8080/health || exit 1

# Default: headless mode with Xvfb (SFML needs a display even when headless)
# Override entrypoint for non-headless use with a real X server.
ENV DISPLAY=:99
# Remove stale Xvfb lock file (left over from a previous crashed container run)
# before starting a fresh Xvfb instance, otherwise it reports "Server is already
# active for display 99" and the app never gets a display.
ENTRYPOINT ["sh", "-c", "\
  rm -f /tmp/.X99-lock /tmp/.X11-unix/X99; \
  Xvfb :99 -screen 0 1920x1080x24 >/tmp/xvfb.log 2>&1 & \
  sleep 2; \
  echo '=== [startup] binary check ==='; \
  ls -lh ./InteractiveStreams; \
  echo '=== [startup] ALSOFT_DRIVERS ==='; \
  echo \"ALSOFT_DRIVERS=$ALSOFT_DRIVERS\"; \
  echo '=== [startup] launching app (stderr+stdout merged) ==='; \
  ./InteractiveStreams 2>&1; \
  RC=$?; echo \"=== APP EXITED: rc=$RC (134=SIGABRT 139=SIGSEGV 132=SIGILL) ===\"; exit $RC \
"]
