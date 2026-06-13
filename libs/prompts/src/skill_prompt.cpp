#include <merak/prompts/skill_prompt.hpp>

namespace merak::prompts {

const char* SKILLS_BLOCK = R"(
## 输出格式：Markdown

- 使用标题（##、###）组织章节
- 多条项目用列表，顺序步骤用编号
- 代码块（```）带语言标签
- 使用 **加粗** 强调关键术语，不用全大写
- 段落简短（2-3 句）
- 对比性数据使用表格
- 不输出无格式文本墙

## 输出约束：简洁

- 默认字数 ≤ 100，除非任务需要更多
- 先给答案，再解释
- 不用填充语
- 不复述问题
- 代码：只展示相关 diff 或片段，不贴整个文件
- 禁止使用 emoji，任何情况下都不输出 emoji 字符
)";

PromptSection build_skill_section() {
    return {SKILLS_BLOCK, PromptCachePolicy::Global};
}

} // namespace merak::prompts
