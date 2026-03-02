#include "core/StreamProfile.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>

namespace is::core {

ProfileManager::ProfileManager()  = default;
ProfileManager::~ProfileManager() = default;

// ── CRUD ─────────────────────────────────────────────────────────────────────

std::string ProfileManager::addProfile(const StreamProfile& profile) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto p = profile;
    if (p.id.empty()) {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        p.id = "profile_" + std::to_string(now % 1000000);
    }

    std::string id = p.id;
    m_profiles.push_back(std::move(p));
    spdlog::info("[ProfileManager] Profile added: '{}' ('{}')", id, profile.name);
    return id;
}

void ProfileManager::updateProfile(const std::string& id,
                                    const StreamProfile& profile) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& p : m_profiles) {
        if (p.id == id) {
            p.name     = profile.name;
            p.parentId = profile.parentId;
            p.config   = profile.config;
            spdlog::info("[ProfileManager] Profile updated: '{}'", id);
            return;
        }
    }
    spdlog::warn("[ProfileManager] Profile not found: '{}'", id);
}

void ProfileManager::removeProfile(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Clear parentId references to this profile in other profiles
    for (auto& p : m_profiles) {
        if (p.parentId == id) {
            spdlog::info("[ProfileManager] Clearing parent ref in profile '{}'", p.id);
            p.parentId.clear();
        }
    }

    auto it = std::remove_if(m_profiles.begin(), m_profiles.end(),
        [&id](const StreamProfile& p) { return p.id == id; });
    if (it != m_profiles.end()) {
        m_profiles.erase(it, m_profiles.end());
        spdlog::info("[ProfileManager] Profile removed: '{}'", id);
    }
}

const StreamProfile* ProfileManager::getProfile(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& p : m_profiles) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

std::vector<const StreamProfile*> ProfileManager::allProfiles() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<const StreamProfile*> out;
    out.reserve(m_profiles.size());
    for (const auto& p : m_profiles) out.push_back(&p);
    return out;
}

size_t ProfileManager::count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_profiles.size();
}

// ── Inheritance resolution ───────────────────────────────────────────────────

/// Deep-merge src into dst.  Values in src override dst.
/// For objects, merge recursively.  For arrays and scalars, src wins.
static void deepMerge(nlohmann::json& dst, const nlohmann::json& src) {
    if (!src.is_object()) return;
    for (auto& [key, val] : src.items()) {
        if (val.is_object() && dst.contains(key) && dst[key].is_object()) {
            deepMerge(dst[key], val);
        } else {
            dst[key] = val;
        }
    }
}

nlohmann::json ProfileManager::resolveProfileChain(const std::string& profileId,
                                                     int maxDepth) const {
    // Caller must NOT hold m_mutex (we acquire it internally)
    std::lock_guard<std::mutex> lock(m_mutex);

    // Build chain from root ancestor → target profile
    std::vector<const StreamProfile*> chain;
    std::string currentId = profileId;

    for (int depth = 0; depth < maxDepth && !currentId.empty(); ++depth) {
        const StreamProfile* found = nullptr;
        for (const auto& p : m_profiles) {
            if (p.id == currentId) { found = &p; break; }
        }
        if (!found) break;

        chain.push_back(found);
        currentId = found->parentId;
    }

    // Merge from root (last) to leaf (first)
    nlohmann::json result = nlohmann::json::object();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        deepMerge(result, (*it)->config);
    }

    return result;
}

nlohmann::json ProfileManager::resolveStreamConfig(
    const nlohmann::json& streamJson) const {

    // Check if stream references a profile
    std::string profileId;
    if (streamJson.contains("profile_id") && streamJson["profile_id"].is_string()) {
        profileId = streamJson["profile_id"].get<std::string>();
    }

    if (profileId.empty()) {
        return streamJson; // No profile, return as-is
    }

    // Resolve profile chain
    nlohmann::json resolved = resolveProfileChain(profileId);

    // Overlay stream-level overrides on top of profile defaults
    deepMerge(resolved, streamJson);

    return resolved;
}

// ── Serialisation ────────────────────────────────────────────────────────────

void ProfileManager::loadFromJson(const nlohmann::json& arr) {
    if (!arr.is_array()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_profiles.clear();

    for (const auto& item : arr) {
        StreamProfile p;
        p.id       = item.value("id", "");
        p.name     = item.value("name", "Profile");
        p.parentId = item.value("parent_id", "");
        p.config   = item.value("config", nlohmann::json::object());
        if (!p.id.empty()) {
            m_profiles.push_back(std::move(p));
        }
    }

    spdlog::info("[ProfileManager] Loaded {} profile(s).", m_profiles.size());
}

nlohmann::json ProfileManager::toJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : m_profiles) {
        nlohmann::json j;
        j["id"]        = p.id;
        j["name"]      = p.name;
        j["parent_id"] = p.parentId;
        j["config"]    = p.config;
        arr.push_back(j);
    }
    return arr;
}

} // namespace is::core
