#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>

namespace is::core {

/// A named configuration profile that provides default values for streams.
/// Profiles form an inheritance chain: each profile can have a parent.
/// When resolving effective config for a stream, we merge:
///   system defaults ← grandparent profile ← parent profile ← stream overrides
///
/// Only fields present in the profile's config JSON are applied; absent
/// fields fall through to the parent or system default.
struct StreamProfile {
    std::string    id;
    std::string    name;
    std::string    parentId;       ///< Optional parent profile ID (empty = no parent)
    nlohmann::json config;         ///< Partial stream config (only overridden fields)
};

/// Manages a collection of StreamProfile objects with CRUD and inheritance
/// resolution.  Thread-safe via internal mutex.
class ProfileManager {
public:
    ProfileManager();
    ~ProfileManager();

    // ── CRUD ─────────────────────────────────────────────────────────────

    /// Add a profile. Returns the (possibly generated) ID.
    std::string addProfile(const StreamProfile& profile);

    /// Update an existing profile.
    void updateProfile(const std::string& id, const StreamProfile& profile);

    /// Remove a profile. Streams/profiles referencing it will lose their parent.
    void removeProfile(const std::string& id);

    /// Get profile by ID (nullptr if not found).
    const StreamProfile* getProfile(const std::string& id) const;

    /// Get all profiles.
    std::vector<const StreamProfile*> allProfiles() const;

    size_t count() const;

    // ── Inheritance resolution ───────────────────────────────────────────

    /// Resolve the full inheritance chain for a profile, returning a merged
    /// JSON config.  Fields from child profiles override parent fields.
    /// max_depth prevents infinite loops.
    nlohmann::json resolveProfileChain(const std::string& profileId,
                                        int maxDepth = 10) const;

    /// Merge a profile chain's resolved config with stream-level overrides.
    /// Returns the fully resolved JSON ready for configFromJson().
    nlohmann::json resolveStreamConfig(const nlohmann::json& streamJson) const;

    // ── Serialisation ────────────────────────────────────────────────────

    void loadFromJson(const nlohmann::json& arr);
    nlohmann::json toJson() const;

private:
    mutable std::mutex m_mutex;
    std::vector<StreamProfile> m_profiles;
};

} // namespace is::core
