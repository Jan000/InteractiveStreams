#include "streaming/StreamOutput.h"
#include <spdlog/spdlog.h>

namespace is::streaming {

StreamOutput::StreamOutput(core::Config& config) : m_config(config) {
    loadTargets();
}

StreamOutput::~StreamOutput() = default;

void StreamOutput::loadTargets() {
    m_targets.clear();

    auto& raw = m_config.raw();
    if (!raw.contains("streaming") || !raw["streaming"].contains("targets")) {
        spdlog::info("[StreamOutput] No stream targets configured.");
        return;
    }

    for (const auto& t : raw["streaming"]["targets"]) {
        StreamTarget target;
        target.name      = t.value("name", "Unnamed");
        target.platform  = t.value("platform", "custom");
        target.url       = t.value("url", "");
        target.streamKey = t.value("stream_key", "");
        target.enabled   = t.value("enabled", true);
        m_targets.push_back(target);

        spdlog::info("[StreamOutput] Target loaded: '{}' ({}) [{}]",
            target.name, target.platform, target.enabled ? "enabled" : "disabled");
    }
}

std::string StreamOutput::getPrimaryUrl() const {
    for (const auto& t : m_targets) {
        if (t.enabled) return t.getFullUrl();
    }
    return "";
}

void StreamOutput::addTarget(const StreamTarget& target) {
    m_targets.push_back(target);
    spdlog::info("[StreamOutput] Target added: '{}'", target.name);
}

void StreamOutput::removeTarget(const std::string& name) {
    m_targets.erase(
        std::remove_if(m_targets.begin(), m_targets.end(),
            [&name](const auto& t) { return t.name == name; }),
        m_targets.end());
    spdlog::info("[StreamOutput] Target removed: '{}'", name);
}

void StreamOutput::setTargetEnabled(const std::string& name, bool enabled) {
    for (auto& t : m_targets) {
        if (t.name == name) {
            t.enabled = enabled;
            spdlog::info("[StreamOutput] Target '{}' {}", name, enabled ? "enabled" : "disabled");
            return;
        }
    }
}

nlohmann::json StreamOutput::getStatus() const {
    nlohmann::json status = nlohmann::json::array();
    for (const auto& t : m_targets) {
        status.push_back({
            {"name", t.name},
            {"platform", t.platform},
            {"enabled", t.enabled},
            {"url", t.url}
        });
    }
    return status;
}

} // namespace is::streaming
