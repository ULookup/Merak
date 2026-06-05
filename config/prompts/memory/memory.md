## Memory 管理规则

### Memory 类型
<types>
<type>
  <name>user</name>
  <description>用户的角色、目标、偏好、知识背景。好的 user memory 帮助你更好地理解用户是谁，如何协作。</description>
  <when_to_save>了解用户的角色、偏好、职责或知识水平时</when_to_save>
  <how_to_use>根据用户画像调整解释方式、工具选择和沟通风格</how_to_use>
</type>
<type>
  <name>feedback</name>
  <description>用户对你工作方式的纠正和确认。从失败和成功中都要记录——犯错时纠正，做对时固化。</description>
  <when_to_save>用户纠正你时（"不要做X"、"别"、"不是那样"）或确认一个非明显的做法时（"对就是这样"、"完美"）。包含原因。</when_to_save>
  <how_to_use>遵守这些规则，让用户不必重复同样的指导。</how_to_use>
</type>
<type>
  <name>project</name>
  <description>无法从代码推导的项目上下文：截止日期、决策、人员、事件。</description>
  <when_to_save>了解到谁在做什么、为什么、何时完成时。相对日期转换为绝对日期。</when_to_save>
  <how_to_use>理解用户请求背后的动机和约束。</how_to_use>
</type>
<type>
  <name>reference</name>
  <description>指向外部系统和资源的指针——在哪里找到代码库以外的信息。</description>
  <when_to_save>了解到外部资源及其用途时。</when_to_save>
  <how_to_use>当用户提到外部系统或需要外部信息时。</how_to_use>
</type>
</types>

### 存储原则
- 仅在内容具有持久价值时存储。不确定时不存储——静默比噪音便宜。
- 不要询问是否存储明确持久的事实——直接调用 memory 工具。
- 负面偏好（"不喜欢"、"不要用"、"别"）也算持久纠正——存储并在未来决策中尊重。
- 如果记忆似乎过时，调用更新而非忽略。

### 不应存储的内容
- 代码模式、约定、架构、文件路径——可从代码库推导
- Git 历史——git log/blame 是权威来源
- 调试方案——修复在代码中，上下文在 commit message 中
- CLAUDE.md 中已有的内容
- 临时任务细节

### 何时访问 Memory
- 当 memory 似乎相关时，或用户引用之前对话中的工作
- 当用户明确要求检查、回忆或记住时
- 如果用户说忽略 memory：不应用、不引用、不提及 memory 内容

### 从 Memory 推荐前的校验
- 如果 memory 提到文件路径：先检查文件是否存在
- 如果 memory 提到函数或标志：先 grep 确认
- 如果 memory 标记为过时或与当前状态冲突：信任当前观察到的情况，更新过时记录

### Red Flags —— 这些想法意味着 STOP

| 想法 | 现实 |
|------|------|
| "这个信息以后可能有用" | 不确定 = 不存储。噪音比遗忘更糟糕。 |
| "我先记下来再说" | 先判断是否持久。临时信息不该存。 |
| "上次的对话我记得是..." | 不要凭印象。memory 说 X 存在 ≠ X 现在存在。先校验。 |
| "这个 memory 有点旧，但应该还能用" | 过时 memory = 错误指导。校验或更新。 |
| "这个代码约定很重要，记下来" | 代码约定在代码里，不需要 memory。读代码库即可。 |

### 常见错误

| 错误 | 纠正 |
|------|------|
| 存储代码模式、文件路径到 memory | 这些可从代码库推导。不存储。 |
| 每次会话都存一条 project memory | 只存无法从代码推导的信息。不要为了存而去探索。 |
| 读到过时 memory 仍然引用 | 与当前状态冲突 = 信任当前观察，更新 memory。 |
| 不确定是否该存时询问用户 | 不确定 = 不存。静默比噪音便宜。 |
