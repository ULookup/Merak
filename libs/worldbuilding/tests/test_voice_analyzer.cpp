#include <gtest/gtest.h>
#include <merak/worldbuilding/voice_analyzer.hpp>

using namespace merak::worldbuilding;

namespace {

std::vector<std::string> make_loquacious_turns(const std::string& prefix, int count) {
    std::vector<std::string> turns;
    for (int i = 0; i < count; ++i) {
        turns.push_back(prefix + " 这是第" + std::to_string(i) +
                         "句话包含了足够多的内容以构建完整指纹。");
    }
    return turns;
}

} // namespace

TEST(VoiceAnalyzer, NoFingerprintWithFewerThan10Turns) {
    VoiceAnalyzer analyzer;
    std::vector<std::string> few = {"你好", "再见", "是的", "不", "可能", "也许", "好", "嗯", "对", "行"};

    auto fp = analyzer.update("agent_x", few);
    EXPECT_EQ(fp.sample_count, 0);
    EXPECT_FALSE(analyzer.fingerprint_for("agent_x").has_value());
}

TEST(VoiceAnalyzer, GeneratesFingerprintAfterMinTurns) {
    VoiceAnalyzer analyzer;
    auto turns = make_loquacious_turns("你好", 12);

    auto fp = analyzer.update("agent_linshi", turns);
    EXPECT_EQ(fp.agent_id, "agent_linshi");
    EXPECT_EQ(fp.sample_count, 12);
    EXPECT_GT(fp.avg_sentence_length, 0.0);
    EXPECT_FALSE(fp.updated_at.empty());
    EXPECT_FALSE(fp.signature_words.empty());
    EXPECT_TRUE(fp.tone_profile.is_object());

    auto stored = analyzer.fingerprint_for("agent_linshi");
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->agent_id, "agent_linshi");
    EXPECT_EQ(stored->sample_count, 12);
}

TEST(VoiceAnalyzer, QuestionFrequencyDetected) {
    VoiceAnalyzer analyzer;
    std::vector<std::string> turns;
    for (int i = 0; i < 15; ++i) {
        if (i % 3 == 0) {
            turns.push_back("真的吗？我不确定这一点是否成立。");
        } else {
            turns.push_back("这是陈述句不需要疑问。我们继续讨论话题。");
        }
    }

    auto fp = analyzer.update("agent_q", turns);
    EXPECT_GT(fp.question_frequency, 0.0);
    EXPECT_LT(fp.question_frequency, 1.0);
}

TEST(VoiceAnalyzer, SimilarVoicesYieldHighSimilarity) {
    VoiceAnalyzer analyzer;

    std::vector<std::string> turns_a;
    std::vector<std::string> turns_b;
    for (int i = 0; i < 15; ++i) {
        turns_a.push_back("短句为多。简洁明了。无需修饰。直来直往。");
        turns_b.push_back("也是短句。一样简洁。不要修饰。也很直接。");
    }

    auto fp_a = analyzer.update("agent_a", turns_a);
    auto fp_b = analyzer.update("agent_b", turns_b);

    auto comp = analyzer.compare(fp_a, fp_b);
    EXPECT_GT(comp.similarity, 0.70);
}

TEST(VoiceAnalyzer, DistinctVoicesYieldLowerScore) {
    VoiceAnalyzer analyzer;

    std::vector<std::string> short_turns;
    for (int i = 0; i < 15; ++i) {
        short_turns.push_back("短。");
    }

    std::vector<std::string> long_turns;
    for (int i = 0; i < 15; ++i) {
        long_turns.push_back("这是一句非常非常长的包含了大量修饰词和详细描述的句子用来测试截然不同的说话风格。");
    }

    auto fp_short = analyzer.update("agent_short", short_turns);
    auto fp_long = analyzer.update("agent_long", long_turns);

    auto comp = analyzer.compare(fp_short, fp_long);
    EXPECT_LT(comp.similarity, 0.70);
}

TEST(VoiceAnalyzer, GroupVoicesClustersSimilarAgents) {
    VoiceAnalyzer analyzer;

    std::vector<std::string> gruff;
    for (int i = 0; i < 15; ++i) gruff.push_back("短。有力。不多话。");

    std::vector<std::string> gruff2;
    for (int i = 0; i < 15; ++i) gruff2.push_back("也短。也硬。不废话。");

    std::vector<std::string> flowery;
    for (int i = 0; i < 15; ++i)
        flowery.push_back("今夜月色真美呢，让我想起了许多年前那个遥远的春日午后时光。");

    auto fp1 = analyzer.update("agent_gruff", gruff);
    auto fp2 = analyzer.update("agent_gruff2", gruff2);
    auto fp3 = analyzer.update("agent_flowery", flowery);

    auto groups = analyzer.group_voices({fp1, fp2, fp3});
    EXPECT_FALSE(groups.empty());

    // gruff and gruff2 should be in the same group
    bool found_same = false;
    for (const auto& [name, members] : groups) {
        bool has_gruff = std::find(members.begin(), members.end(), "agent_gruff") != members.end();
        bool has_gruff2 = std::find(members.begin(), members.end(), "agent_gruff2") != members.end();
        if (has_gruff && has_gruff2) {
            found_same = true;
            break;
        }
    }
    EXPECT_TRUE(found_same);
}

TEST(VoiceAnalyzer, CheckAllReturnsComparisonsForAllPairs) {
    VoiceAnalyzer analyzer;

    auto turns = make_loquacious_turns("test", 15);
    auto fp1 = analyzer.update("a", turns);
    auto fp2 = analyzer.update("b", turns);
    auto fp3 = analyzer.update("c", turns);

    auto results = analyzer.check_all({fp1, fp2, fp3});
    EXPECT_EQ(results.size(), 3); // 3 choose 2
}

TEST(VoiceAnalyzer, ComparisonIncludesSuggestionsButDoesNotMutate) {
    VoiceAnalyzer analyzer;

    auto turns_a = make_loquacious_turns("你好", 15);
    auto turns_b = make_loquacious_turns("你好", 15);

    auto fp_a = analyzer.update("agent_x", turns_a);
    auto fp_b = analyzer.update("agent_y", turns_b);

    // Original fingerprints should be unchanged after compare
    auto orig_words_a = fp_a.signature_words;
    auto orig_words_b = fp_b.signature_words;

    auto comp = analyzer.compare(fp_a, fp_b);
    EXPECT_GT(comp.similarity, 0.70);
    EXPECT_FALSE(comp.suggestions.empty());

    // fp_a and fp_b should not be modified by compare
    EXPECT_EQ(fp_a.signature_words, orig_words_a);
    EXPECT_EQ(fp_b.signature_words, orig_words_b);
}
