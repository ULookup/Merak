---
name: create_character_card
description: 创建完整的角色卡片，包含性格、背景、声音指纹
version: 1.0.0
allowed_tools:
  - create_character
  - update_character_card
context: inline
---

# 创建角色卡片流程

## 步骤
1. 与用户讨论角色定位：主角/配角/反派/NPC？
2. 使用 create_character 创建角色
3. 用 update_character_card 填充以下字段：
   - 基础信息（年龄、性别、种族、身份）
   - 性格特征（3-5个核心特质）
   - 情感倾向
   - 说话风格（含2-3个示例对话）
   - 核心欲望
   - 深层恐惧
   - 背景故事（300-500字）
   - 知识范围（角色知道什么、不知道什么）
   - 外表描述

## 检查清单
- 所有字段填充完毕
- 说话风格有示例对话
- 背景故事与世界观一致
- 知识范围明确，避免角色"全知"
- 角色与已有角色有区分度
