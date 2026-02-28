#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <fstream>

namespace is::core {

/// Manages application configuration loaded from JSON files.
class Config {
public:
    explicit Config(const std::string& path);

    /// Get a value by dotted key path (e.g., "rendering.width").
    template<typename T>
    T get(const std::string& key, const T& defaultValue = T{}) const;

    /// Set a value by dotted key path.
    template<typename T>
    void set(const std::string& key, const T& value);

    /// Reload configuration from file.
    void reload();

    /// Save current configuration to file.
    void save() const;

    /// Get the raw JSON object.
    const nlohmann::json& raw() const { return m_data; }

private:
    /// Navigate into the JSON using a dotted key and return a pointer (or nullptr).
    const nlohmann::json* navigate(const std::string& key) const;
    nlohmann::json*       navigateMut(const std::string& key, bool create = false);

    std::string    m_path;
    nlohmann::json m_data;
};

// ─── Template implementations ────────────────────────────────────────────────

template<typename T>
T Config::get(const std::string& key, const T& defaultValue) const {
    const auto* node = navigate(key);
    if (node && !node->is_null()) {
        try {
            return node->get<T>();
        } catch (...) {
            return defaultValue;
        }
    }
    return defaultValue;
}

template<typename T>
void Config::set(const std::string& key, const T& value) {
    auto* node = navigateMut(key, true);
    if (node) {
        *node = value;
    }
}

} // namespace is::core
