#include <merak/worldbuilding/pipeline.hpp>
#include <nlohmann/json.hpp>

namespace merak::worldbuilding {

std::vector<CreativePhase> allowed_next_phases(CreativePhase current) {
    switch (current) {
        case CreativePhase::Worldbuilding:
            return {CreativePhase::CharacterCreation};
        case CreativePhase::CharacterCreation:
            return {CreativePhase::Worldbuilding, CreativePhase::PlotArchitecture};
        case CreativePhase::PlotArchitecture:
            return {CreativePhase::CharacterCreation, CreativePhase::SceneWriting};
        case CreativePhase::SceneWriting:
            return {CreativePhase::PlotArchitecture, CreativePhase::Reflection};
        case CreativePhase::Reflection:
            return {CreativePhase::SceneWriting};
    }
    return {};
}

std::string generate_phase_context(const PipelineState& state) {
    switch (state.current_phase) {
        case CreativePhase::Worldbuilding: {
            return R"(## 创作阶段：世界观构建
当前状态：正在构建世界基础设定

### 已完成
- 世界基本参数已设定

### 待完成
- 创建核心角色（至少2-3个主要角色）
- 设定主要地点
- 确立世界规则体系

### 推荐下一步
使用 create_character 工具创建角色，从主角开始。
)";
        }
        case CreativePhase::CharacterCreation: {
            return R"(## 创作阶段：角色创建
当前状态：正在创建和完善角色

### 待完成
- 为每个角色设定完整的角色卡（性格、背景、欲望、恐惧）
- 建立角色之间的关系网络
- 确保角色知识范围合理（避免全知）

### 推荐工具
- update_character_card：编辑角色卡
- add_relation：建立角色关系
- add_character_diary：添加角色日记

### 推荐下一步
完成角色创建后，进入情节架构阶段。
)";
        }
        case CreativePhase::PlotArchitecture: {
            return R"(## 创作阶段：情节架构
当前状态：正在规划故事结构

### 待完成
- 创建故事弧线（Arc）
- 规划章节结构
- 布置伏笔

### 推荐工具
- create_chapter：创建章节
- plant_foreshadowing：布置伏笔
- create_secret：创建秘密

### 推荐下一步
进入场景写作阶段，开始具体场景的创作。
)";
        }
        case CreativePhase::SceneWriting: {
            return R"(## 创作阶段：场景写作
当前状态：正在写作具体场景

### 注意事项
- 每个场景需明确参与者
- 注意角色知识边界——角色只应知道自己知道的事
- 场景结束时更新角色日记和关系

### 推荐工具
- create_scene：创建新场景
- end_scene：结束当前场景（自动生成日记、更新关系）
- update_foreshadow：更新伏笔状态

### 自检清单
□ 场景中每个角色的行为是否符合其性格特征？
□ 对话风格是否与角色设定一致？
□ 是否有角色知道了不该知道的信息？
□ 伏笔是否在适当的时候被回收？

### 推荐下一步
当前章节场景数达标后，进入反思阶段。
)";
        }
        case CreativePhase::Reflection: {
            return R"(## 创作阶段：反思与回顾
当前状态：正在回顾已创作内容

### 检查项目
- 角色一致性：角色行为是否前后一致
- 情节连贯性：情节发展是否合理
- 伏笔管理：是否有未回收的伏笔
- 节奏控制：章节长度和节奏是否合适
- 秘密管理：角色间的信息不对等是否合理

### 推荐工具
- voice_check：检查角色声音一致性
- update_foreshadow：更新伏笔状态

### 自检清单
□ 所有角色的行为是否与角色卡一致？
□ 伏笔是否按计划推进？
□ 秘密的知晓范围是否合理？
□ 章节节奏是否合适？
□ 如有问题，返回场景写作阶段进行修改。

### 推荐下一步
如一切就绪，返回场景写作阶段继续下一章节。
)";
        }
    }
    return "";
}

void to_json(nlohmann::json& j, const PipelineState& s) {
    j = {
        {"world_id", s.world_id},
        {"current_phase", to_string(s.current_phase)},
        {"last_updated", s.last_updated},
        {"active_workflow", s.active_workflow},
        {"chapter_count", s.chapter_count},
        {"total_chapters_target", s.total_chapters_target},
        {"is_cycle_complete", s.is_cycle_complete},
        {"cycle_count", s.cycle_count},
        {"extra", s.extra}
    };
    if (s.active_arc_id) j["active_arc_id"] = *s.active_arc_id;
    if (s.active_chapter_id) j["active_chapter_id"] = *s.active_chapter_id;
    if (s.active_scene_id) j["active_scene_id"] = *s.active_scene_id;
    j["scene_count_in_chapter"] = s.scene_count_in_chapter;
    j["total_scenes_target"] = s.total_scenes_target;
    j["needs_diary_update"] = s.needs_diary_update;
    j["needs_character_update"] = s.needs_character_update;
}

void from_json(const nlohmann::json& j, PipelineState& s) {
    j.at("world_id").get_to(s.world_id);
    s.current_phase = creative_phase_from_string(j.at("current_phase").get<std::string>())
                          .value_or(CreativePhase::Worldbuilding);
    if (j.contains("active_arc_id") && !j.at("active_arc_id").is_null())
        s.active_arc_id = j.at("active_arc_id").get<std::string>();
    if (j.contains("active_chapter_id") && !j.at("active_chapter_id").is_null())
        s.active_chapter_id = j.at("active_chapter_id").get<std::string>();
    if (j.contains("active_scene_id") && !j.at("active_scene_id").is_null())
        s.active_scene_id = j.at("active_scene_id").get<std::string>();
    s.scene_count_in_chapter = j.value("scene_count_in_chapter", 0);
    s.total_scenes_target = j.value("total_scenes_target", 0);
    s.needs_diary_update = j.value("needs_diary_update", false);
    s.needs_character_update = j.value("needs_character_update", false);
    j.at("last_updated").get_to(s.last_updated);
    s.active_workflow = j.value("active_workflow", "");
    s.chapter_count = j.value("chapter_count", 0);
    s.total_chapters_target = j.value("total_chapters_target", 0);
    s.is_cycle_complete = j.value("is_cycle_complete", false);
    s.cycle_count = j.value("cycle_count", 0);
    if (j.contains("extra")) s.extra = j.at("extra");
}

} // namespace merak::worldbuilding
