# XLab 个人工作区

`D:\XLab` 是用于**工作**、**学习**和**生活**的个人工作区。目录按用途和使用阶段组织，方便为新文件找到合适的位置，也方便日后查找旧文件。

## 1. 核心分类

确定文件归属时，按以下顺序判断：

1. **收件箱（Inbox）**：临时收集，尚未整理的内容。
2. **项目（Projects）**：有明确成果、截止时间或完成标准的工作。
3. **领域（Areas）**：没有最终完成日期的持续性职责。
4. **资源（Resources）**：以后可能用到的参考资料。
5. **笔记（Notes）**：思考记录、学习记录、会议纪要和复盘。
6. **成果（Outputs）**：值得保存或分享的最终成果。
7. **归档（Archive）**：已完成或暂不活跃的资料。

工作、学习和生活这三类目录始终使用英文名称：`Work`、`Study` 和 `Life`。

## 2. 目录结构

```text
D:\XLab
├─ 00_Inbox
│  ├─ Work
│  ├─ Study
│  └─ Life
├─ 01_Projects
│  ├─ Work
│  │  └─ EdgeAgent
│  ├─ Study
│  │  └─ Embedded
│  └─ Life
├─ 02_Areas
│  ├─ Work
│  │  ├─ Career
│  │  └─ Operations
│  ├─ Study
│  │  ├─ Embedded_Systems
│  │  └─ Software_Engineering
│  └─ Life
│     ├─ Health
│     ├─ Finance
│     └─ Personal_Growth
├─ 03_Resources
│  ├─ Work
│  │  ├─ Standards
│  │  ├─ Tools
│  │  └─ References
│  ├─ Study
│  │  ├─ Embedded_Systems
│  │  ├─ Software_Engineering
│  │  └─ Software_Design_Exam
│  └─ Life
│     ├─ Health
│     ├─ Finance
│     └─ Reference
├─ 04_Notes
│  ├─ Daily
│  ├─ Weekly_Reviews
│  ├─ Work
│  ├─ Study
│  └─ Life
├─ 05_Outputs
│  ├─ Work
│  ├─ Study
│  └─ Life
├─ 06_Tools
│  ├─ Templates
│  ├─ Scripts
│  └─ Utilities
├─ 07_Archive
│  ├─ Work
│  ├─ Study
│  └─ Life
└─ 99_System
```

## 3. 新文件放在哪里

### 工作

- 交付物、产品、功能或客户任务：`01_Projects\Work\Project_Name`
- 长期职业规划或工作职责：`02_Areas\Work\...`
- 技术标准、工具或可复用的参考资料：`03_Resources\Work\...`
- 会议纪要、决策记录和工作日志：`04_Notes\Work`
- 最终报告、发布内容或文档：`05_Outputs\Work`

### 学习

- 课程、认证、实验或学习目标：`01_Projects\Study\Project_Name`
- 需要长期积累的知识领域：`02_Areas\Study\...`
- 书籍、手册、论文、示例和考试资料：`03_Resources\Study\...`
- 学习笔记、问题、错题和总结：`04_Notes\Study`
- 教程、学习总结、代码示例和已完成的练习：`05_Outputs\Study`

### 生活

- 一次性的个人目标或活动：`01_Projects\Life\Project_Name`
- 健康、财务、人际关系或个人成长：`02_Areas\Life\...`
- 长期参考资料：`03_Resources\Life\...`
- 个人日志和反思：`04_Notes\Life`
- 已完成的计划、记录或可复用的清单：`05_Outputs\Life`

## 4. 现有内容的位置

- `01_Projects\Work\EdgeAgent`：原有的开发项目，包括 `EdgeAgent`、`EdgeAgentTester` 和构建目录。
- `01_Projects\Study\Embedded`：嵌入式开发项目及其项目文档。
- `03_Resources\Study\Software_Design_Exam`：软件设计考试书籍和练习题。

项目内的源代码、配置、文档和构建产物放在一起，以便项目正常运行，并避免破坏相对路径。项目文件夹内可以保留原有文件名；仅统一使用英文文件夹名称。

## 5. 命名规则

- 文件夹名称：使用简洁、稳定的英文名称。采用 `PascalCase` 或 `snake_case`，同一项目内不要混用。
- 新笔记文件：使用 `YYYY-MM-DD_topic.md` 格式，例如 `2026-09-26_pointer_review.md`。
- 项目名称：使用有明确含义的产品名或目标名，不要只用日期命名。
- 如果源文件或下载书籍的原标题有助于检索，保留原文件名。
- 除非项目工具有要求，否则目录层级尽量不要超过四到五层。

## 6. 日常流程

1. 将暂时无法确定归属的下载文件、截图、想法和临时文件放入对应的 `00_Inbox\Work`、`00_Inbox\Study` 或 `00_Inbox\Life` 文件夹。
2. 日常简短记录写入 `04_Notes\Daily`，文件名使用日期。
3. 每周复盘时，将收件箱中的每项内容移至 `Projects`、`Areas`、`Resources`、`Notes` 或 `Outputs`。
4. 项目完成或暂时不再推进时，将整个项目文件夹移至 `07_Archive`，不要直接删除。

## 7. 项目记录规范

每个新项目的根目录都应有一份 `README.md`，记录：

- 目标和预期成果
- 当前状态
- 下一步行动
- 重要链接或依赖
- 决策记录
- 完成日期和最终成果位置

学习项目还应记录：学到了什么、哪里没有成功、完成目标的证据，以及下一步要练习的技能。

## 8. 每周复盘问题

- 这周完成了什么？
- 目前有哪些阻碍？下一步具体要做什么？
- 学到了哪些值得整理成可复用笔记的内容？
- 收件箱中还有哪些文件？
- 哪些项目或领域不再需要持续关注？
- 哪些内容需要归档、总结或整理成成果？

本 README 是工作区的入口。如果需要新增分类，优先放在用途最接近的现有目录下；只有工作流程确实不同时，才新增顶层目录。
