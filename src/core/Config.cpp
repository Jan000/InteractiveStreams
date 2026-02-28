#include "core/Config.h"
#include <spdlog/spdlog.h>
#include <sstream>

namespace is::core {

Config::Config(const std::string& path) : m_path(path) {
    reload();
}

void Config::reload() {
    std::ifstream file(m_path);
    if (file.is_open()) {
        try {
            file >> m_data;
            spdlog::info("Configuration loaded from: {}", m_path);
        } catch (const nlohmann::json::parse_error& e) {
            spdlog::error("Failed to parse config {}: {}", m_path, e.what());
            m_data = nlohmann::json::object();
        }
    } else {
        spdlog::warn("Config file not found: {} – using defaults", m_path);
        m_data = nlohmann::json::object();
    }
}

void Config::save() const {
    std::ofstream file(m_path);
    if (file.is_open()) {
        file << m_data.dump(4);
        spdlog::info("Configuration saved to: {}", m_path);
    } else {
        spdlog::error("Failed to save config to: {}", m_path);
    }
}

const nlohmann::json* Config::navigate(const std::string& key) const {
    const nlohmann::json* current = &m_data;
    std::istringstream stream(key);
    std::string token;

    while (std::getline(stream, token, '.')) {
        if (current->is_object() && current->contains(token)) {
            current = &(*current)[token];
        } else {
            return nullptr;
        }
    }
    return current;
}

nlohmann::json* Config::navigateMut(const std::string& key, bool create) {
    nlohmann::json* current = &m_data;
    std::istringstream stream(key);
    std::string token;
    std::string remaining;

    std::vector<std::string> tokens;
    while (std::getline(stream, token, '.')) {
        tokens.push_back(token);
    }

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (!current->is_object()) {
            if (create) {
                *current = nlohmann::json::object();
            } else {
                return nullptr;
            }
        }
        if (!current->contains(tokens[i])) {
            if (create) {
                (*current)[tokens[i]] = (i == tokens.size() - 1)
                    ? nlohmann::json()
                    : nlohmann::json::object();
            } else {
                return nullptr;
            }
        }
        current = &(*current)[tokens[i]];
    }
    return current;
}

} // namespace is::core
