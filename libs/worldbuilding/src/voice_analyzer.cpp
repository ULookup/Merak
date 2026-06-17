#include <merak/worldbuilding/voice_analyzer.hpp>
#include <merak/worldbuilding/ids.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace merak::worldbuilding {
namespace {

constexpr int kMinTurns = 10;

int count_words(const std::string& text) {
    std::istringstream in(text);
    int count = 0;
    std::string word;
    while (in >> word) ++count;
    return count;
}

bool ends_with_question(const std::string& text) {
    auto trimmed = text;
    while (!trimmed.empty() && std::isspace(trimmed.back())) trimmed.pop_back();
    return trimmed.ends_with("？");
}

// Simple Chinese modifier words
bool is_modifier_char(char32_t c) {
    return c == U'的' || c == U'地' || c == U'得' ||
           c == U'很' || c == U'更' || c == U'最' ||
           c == U'非' || c == U'极' || c == U'太' ||
           c == U'有' || c == U'没' || c == U'不' ||
           c == U'会' || c == U'能' || c == U'可' ||
           c == U'也' || c == U'都' || c == U'还' ||
           c == U'就' || c == U'只';
}

int count_modifiers(const std::string& text) {
    int count = 0;
    for (size_t i = 0; i < text.size(); ) {
        unsigned char byte = static_cast<unsigned char>(text[i]);
        if (byte < 0x80) { i++; continue; }
        // Rough UTF-8 multibyte decode for Chinese chars
        int len = 0;
        char32_t cp = 0;
        if ((byte & 0xE0) == 0xC0) { len = 2; cp = byte & 0x1F; }
        else if ((byte & 0xF0) == 0xE0) { len = 3; cp = byte & 0x0F; }
        else if ((byte & 0xF8) == 0xF0) { len = 4; cp = byte & 0x07; }
        else { i++; continue; }

        if (i + len > text.size()) { i++; continue; }
        for (int j = 1; j < len; ++j) {
            cp = (cp << 6) | (static_cast<unsigned char>(text[i + j]) & 0x3F);
        }
        if (is_modifier_char(cp)) ++count;
        i += len;
    }
    return count;
}

std::string extract_word(const std::string& text, size_t& i) {
    unsigned char byte = static_cast<unsigned char>(text[i]);
    int len = 1;
    if ((byte & 0xE0) == 0xC0) len = 2;
    else if ((byte & 0xF0) == 0xE0) len = 3;
    else if ((byte & 0xF8) == 0xF0) len = 4;

    if (i + len > text.size()) len = 1;
    auto word = text.substr(i, len);
    i += len;
    return word;
}

std::vector<std::string> extract_signature_words(const std::vector<std::string>& turns) {
    std::unordered_map<std::string, int> freq;
    for (const auto& turn : turns) {
        for (size_t i = 0; i < turn.size(); ) {
            unsigned char byte = static_cast<unsigned char>(turn[i]);
            if (byte < 0x80) { i++; continue; }
            auto word = extract_word(turn, i);
            freq[word]++;
        }
    }

    std::vector<std::pair<std::string, int>> ranked(freq.begin(), freq.end());
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<std::string> result;
    for (size_t i = 0; i < ranked.size() && i < 20; ++i) {
        if (ranked[i].second >= 2) {
            result.push_back(ranked[i].first);
        }
    }
    return result;
}

nlohmann::json compute_tone_profile(const std::vector<std::string>& turns) {
    int exclamation = 0;
    int question = 0;
    int ellipsis = 0;
    int total = static_cast<int>(turns.size());

    for (const auto& turn : turns) {
        auto t = turn;
        while (!t.empty() && std::isspace(t.back())) t.pop_back();
        if (t.empty()) continue;
        if (t.ends_with("！")) exclamation++;
        else if (t.ends_with("？")) question++;
        else if (t.ends_with("…")) ellipsis++;
    }

    return nlohmann::json::object({
        {"exclamation_ratio", total > 0 ? static_cast<double>(exclamation) / total : 0.0},
        {"question_ratio", total > 0 ? static_cast<double>(question) / total : 0.0},
        {"ellipsis_ratio", total > 0 ? static_cast<double>(ellipsis) / total : 0.0},
    });
}

} // namespace

VoiceFingerprint VoiceAnalyzer::update(const std::string& agent_id,
                                       const std::vector<std::string>& dialogue_turns) {
    auto total_turns = dialogue_turns.size();
    if (total_turns < kMinTurns) {
        VoiceFingerprint empty;
        empty.agent_id = agent_id;
        return empty;
    }

    VoiceFingerprint fp;
    fp.agent_id = agent_id;
    fp.updated_at = now_iso_utc();
    fp.sample_count = static_cast<int>(total_turns);

    // Average sentence length and variance
    std::vector<int> word_counts;
    word_counts.reserve(total_turns);
    for (const auto& turn : dialogue_turns) {
        word_counts.push_back(count_words(turn));
    }
    auto sum = std::accumulate(word_counts.begin(), word_counts.end(), 0);
    fp.avg_sentence_length = static_cast<double>(sum) / total_turns;

    double variance = 0.0;
    for (const auto& wc : word_counts) {
        auto diff = wc - fp.avg_sentence_length;
        variance += diff * diff;
    }
    fp.sentence_variance = variance / total_turns;

    // Question frequency
    int q_count = 0;
    for (const auto& turn : dialogue_turns) {
        if (ends_with_question(turn)) q_count++;
    }
    fp.question_frequency = static_cast<double>(q_count) / total_turns;

    // Modifier ratio
    int total_words = sum;
    int total_modifiers = 0;
    for (const auto& turn : dialogue_turns) {
        total_modifiers += count_modifiers(turn);
    }
    fp.modifier_ratio = total_words > 0 ? static_cast<double>(total_modifiers) / total_words : 0.0;

    // Signature words
    fp.signature_words = extract_signature_words(dialogue_turns);

    // Tone profile
    fp.tone_profile = compute_tone_profile(dialogue_turns);

    fingerprints_[agent_id] = fp;
    return fp;
}

std::optional<VoiceFingerprint>
VoiceAnalyzer::fingerprint_for(const std::string& agent_id) const {
    auto it = fingerprints_.find(agent_id);
    if (it != fingerprints_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<VoiceFingerprint> VoiceAnalyzer::list_fingerprints() const {
    std::vector<VoiceFingerprint> result;
    for (const auto& [id, fp] : fingerprints_) {
        result.push_back(fp);
    }
    return result;
}

VoiceComparison VoiceAnalyzer::compare(const VoiceFingerprint& left,
                                        const VoiceFingerprint& right) const {
    VoiceComparison comp;
    comp.left_agent_id = left.agent_id;
    comp.right_agent_id = right.agent_id;

    // Feature-based similarity score
    double score = 0.0;
    int features = 0;

    // Compare sentence length
    auto len_max = std::max(left.avg_sentence_length, right.avg_sentence_length);
    if (len_max > 0.0) {
        auto len_ratio = std::min(left.avg_sentence_length, right.avg_sentence_length) / len_max;
        score += len_ratio;
        features++;
    }

    // Compare question frequency (skip if both zero to avoid inflating similarity)
    auto q_max = std::max(left.question_frequency, right.question_frequency);
    if (q_max > 0.0) {
        auto q_ratio = std::min(left.question_frequency, right.question_frequency) / q_max;
        score += q_ratio;
        features++;
    }

    // Compare modifier ratio (skip if both zero)
    auto mod_max = std::max(left.modifier_ratio, right.modifier_ratio);
    if (mod_max > 0.0) {
        auto mod_ratio = std::min(left.modifier_ratio, right.modifier_ratio) / mod_max;
        score += mod_ratio;
        features++;
    }

    // Compare signature word overlap
    if (!left.signature_words.empty() || !right.signature_words.empty()) {
        std::set<std::string> left_set(left.signature_words.begin(), left.signature_words.end());
        std::set<std::string> right_set(right.signature_words.begin(), right.signature_words.end());
        std::vector<std::string> shared;
        for (const auto& w : left_set) {
            if (right_set.contains(w)) shared.push_back(w);
        }
        comp.shared_features = shared;
        auto total_words = left_set.size() + right_set.size();
        if (total_words > 0) {
            auto overlap = 2.0 * shared.size() / total_words;
            score += overlap;
            features++;
        }

        // Differences
        for (const auto& w : left_set) {
            if (!right_set.contains(w)) comp.differences.push_back(w);
        }
        for (const auto& w : right_set) {
            if (!left_set.contains(w)) comp.differences.push_back(w);
        }
    }

    comp.similarity = features > 0 ? score / features : 0.0;

    // Suggestions
    if (comp.similarity > 0.70) {
        comp.suggestions.push_back(left.agent_id + " 与 " + right.agent_id +
                                    " 声音指纹高度相似(" + std::to_string(comp.similarity) +
                                    ")，考虑增加差异化特征");
    }
    if (comp.similarity < 0.30 && comp.similarity > 0.0) {
        comp.suggestions.push_back(left.agent_id + " 与 " + right.agent_id +
                                    " 声音差异较大(" + std::to_string(comp.similarity) + ")");
    }

    return comp;
}

std::vector<VoiceComparison>
VoiceAnalyzer::check_all(const std::vector<VoiceFingerprint>& fingerprints) const {
    std::vector<VoiceComparison> results;
    for (size_t i = 0; i < fingerprints.size(); ++i) {
        for (size_t j = i + 1; j < fingerprints.size(); ++j) {
            results.push_back(compare(fingerprints[i], fingerprints[j]));
        }
    }
    return results;
}

std::map<std::string, std::vector<std::string>>
VoiceAnalyzer::group_voices(const std::vector<VoiceFingerprint>& fingerprints) const {
    std::map<std::string, std::vector<std::string>> groups;
    if (fingerprints.empty()) return groups;

    // Simple clustering: group agents whose similarity > 0.70
    std::set<size_t> assigned;

    int group_idx = 0;
    for (size_t i = 0; i < fingerprints.size(); ++i) {
        if (assigned.contains(i)) continue;

        std::string group_name = "group_" + std::to_string(group_idx++);
        groups[group_name].push_back(fingerprints[i].agent_id);
        assigned.insert(i);

        for (size_t j = i + 1; j < fingerprints.size(); ++j) {
            if (assigned.contains(j)) continue;
            auto comp = compare(fingerprints[i], fingerprints[j]);
            if (comp.similarity > 0.70) {
                groups[group_name].push_back(fingerprints[j].agent_id);
                assigned.insert(j);
            }
        }
    }

    return groups;
}

} // namespace merak::worldbuilding
