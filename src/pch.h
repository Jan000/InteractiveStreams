#pragma once

// ─── Precompiled Header ───────────────────────────────────────────────────────
// Alle schweren externen Dependencies werden hier einmal vorkompiliert.
// Das spart bei jedem inkrementellen Build mehrere Sekunden pro Translation Unit.

// ── SFML ─────────────────────────────────────────────────────────────────────
#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <SFML/System.hpp>
#include <SFML/Network.hpp>

// ── Box2D ─────────────────────────────────────────────────────────────────────
#include <box2d/box2d.h>

// ── JSON ──────────────────────────────────────────────────────────────────────
#include <nlohmann/json.hpp>

// ── Logging ───────────────────────────────────────────────────────────────────
#include <spdlog/spdlog.h>

// ── HTTP ──────────────────────────────────────────────────────────────────────
#include <httplib.h>

// ── STL ───────────────────────────────────────────────────────────────────────
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
