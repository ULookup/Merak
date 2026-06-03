#pragma once

#include <merak/worldbuilding/world_models.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace merak::worldbuilding {

class VoiceAnalyzer {
public:
    VoiceFingerprint update(const std::string& agent_id,
                            const std::vector<std::string>& dialogue_turns);
    std::optional<VoiceFingerprint>
    fingerprint_for(const std::string& agent_id) const;
    std::vector<VoiceFingerprint> list_fingerprints() const;
    VoiceComparison compare(const VoiceFingerprint& left,
                            const VoiceFingerprint& right) const;
    std::vector<VoiceComparison>
    check_all(const std::vector<VoiceFingerprint>& fingerprints) const;
    std::map<std::string, std::vector<std::string>>
    group_voices(const std::vector<VoiceFingerprint>& fingerprints) const;

private:
    std::map<std::string, VoiceFingerprint> fingerprints_;
};

} // namespace merak::worldbuilding
