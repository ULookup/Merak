---
name: end_scene_cleanup
description: 场景结束后的清理工作：更新日记、关系、伏笔状态
version: 1.0.0
allowed_tools:
  - end_scene
  - add_character_diary
  - add_relation
  - update_foreshadow
  - update_character_card
context: inline
---

# 场景收尾清理流程

## 收尾步骤

### 1. 角色日记更新
为每个参与场景的角色撰写日记条目：
- **内容**：角色对场景事件的个人感受和思考
- **语气**：符合角色的 speaking_style 和 emotional_tendency
- **知识限制**：只写入角色"知道"的内容（遵循 knowledge_scope）
- **格式**：第一人称，100-300字

### 2. 关系演变
检查场景中是否有角色关系发生变化：
- 新建立的关系（初次互动）
- 强化的关系（共同经历）
- 恶化的关系（冲突/误解）
- 使用 add_relation 更新关系描述和 intimacy 值

### 3. 伏笔状态更新
- 标记本场景中回收的伏笔为 paid（使用 update_foreshadow）
- 推进正在发展的伏笔（更新状态或补充内容）
- 确认没有遗漏的伏笔线索

### 4. 秘密状态检查
- 本场景中是否有角色发现了新的信息？
- 是否需要更新 secret 的 aware_character_ids 或 suspicious_character_ids？
- 是否有秘密在本场景中被暴露？

### 5. 角色状态同步
- 角色在本场景后是否有成长/变化？
- 如有，使用 update_character_card 更新相关字段
- 特别注意 daily_goal 或 emotional_tendency 的变化

## 输出清单
- 已更新的日记条目列表
- 已更新的关系列表
- 已更新的伏笔列表
- 已更新的秘密列表
- 已更新的角色卡字段
