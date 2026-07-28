# Figma 一比一复刻进度

**参考文件**：Figma Make `TPpSXEnazzdS6HPH8iBzh5`（Redesign Password Manager UI）  
**开始时间**：2026-07-10  
**方针**：按 Figma 浅色三栏工作区进行整体视觉与结构重构，一次只交付一个可验收的"页面"，用户手动编译验证后再进入下一页。

---

## Page 1 — Vault Workspace（主窗口三栏 + 视觉底座）

**状态**：✅ 代码就绪，等待用户编译验证  
**完成时间**：2026-07-11

### 视觉基础

| 类别      | 文件                              | 变更概要 |
|-----------|-----------------------------------|----------|
| 令牌      | `resources/qss/tokens_light.qss`  | 整份改写为 Figma 浅色令牌：`--bg-primary #eef2f7` / `--bg-canvas #fbfcfe` / `--bg-sidebar #f5f8fc` / `--accent #276cf0` / `--radius-lg 22px` 等 |
| 令牌      | `resources/qss/tokens_dark.qss`   | 同步替换 accent 为 `#276cf0` 家族，保持深色 canvas 与四阶灰度 |
| 组件      | `resources/qss/components.qss`    | 按钮 / QLineEdit / QListWidget / QComboBox / QScrollBar 全部按 Figma 卡片风重写；提供 `[accent] [soft] [danger] [flat]` 变体 |
| 应用样式  | `resources/qss/app.qss`           | 新增 22 圆角外壳、三栏 objectName（`#WorkspaceRoot #WorkspaceContainer #Sidebar #PasswordListColumn #DetailPanel` 等）、卡片和详情面板细节样式 |

### 图标

新增 22 个 lucide 风格 SVG（`resources/icons/`）：
`vault`, `plus`, `search`, `list`, `star`, `folder`, `folder-open`, `trash-2`, `cloud`, `settings-2`, `lock-keyhole`, `more-horizontal`, `pencil`, `copy`, `eye`, `eye-off`, `external-link`, `arrow-left`, `x`, `shield-alert`, `shield-check`, `key-round`。

图标通过 `IconLoader::Load(name, color, size)` 加载，`currentColor` / `stroke="#000000"` / `fill="#000000"` 会被替换为传入色值，缓存复用。

### 三栏工作区

| 列          | 宽度 / 布局           | 主要 widget |
|-------------|-----------------------|-------------|
| Sidebar     | 固定 240px           | Logo + 新建密码按钮 + Sections 列表 + 分隔线 + 分类标题+新增按钮 + Categories 列表 + 底部同步状态 + 设置按钮 |
| Password 列 | 弹性，min 380px      | 搜索 header + 更多/锁定按钮 + 标题栏（"所有密码 N 项" + 排序）+ 卡片列表 |
| Detail      | 固定 400px           | 空态 / 内容态（头部 icon+标题+标签+编辑/更多/收藏，风险横幅，用户名/密码/网址卡片，时间戳卡片，底部两个按钮） |

- 中央使用 `QStackedWidget central_stack_` 承载 workspace 页与后续 Page 3 的 preferences 页
- 侧栏 Sections 与 Categories 两个 `QListWidget` 选中态互斥（一个变化会 `clearSelection` 另一个）
- 密码列表用 `QListWidget::setItemWidget` 渲染每张卡片（头像 + 标题 + 用户名 + 相对时间 + 可选星标）

### 详情面板（`src/ui/detail_panel.h/cc` 新增）

- `QFrame` 派生的独立组件，内部用 `QStackedLayout` 切换空态/内容态
- 头部：48×48 圆角首字母头像 + 标题 + 分类徽章 + 收藏切换按钮 + 编辑按钮 + 更多按钮
- 内容卡片三行：用户名 / 密码（DM Mono + 显示/复制按钮）/ 网址（可点击图标打开）
- 时间戳卡片：创建时间 / 最近更新（相对时间格式）
- 底部两个大按钮：复制密码 / 打开网站
- 通过 6 个 signal 与 `MainWindow` 解耦（EditRequested / DeleteRequested / CopyPassword / CopyUsername / OpenWebsite / FavoriteToggle）
- 风险横幅（`#DetailRiskyBanner`）作为视觉占位，本次固定隐藏（数据模型未加 risky 字段）

### 主窗口改造（`src/ui/main_window.h/cc`）

- 移除 `QToolBar` / `QStatusBar` / `QSplitter`（Figma 无这些元素）
- 移除 `QTreeWidget category_tree_` / `QLabel status_message_` / `QLabel empty_state_`（旧版）
- 新增 `BuildSidebar()` / `BuildPasswordListColumn()` / `BuildDetailColumn()`
- 新增 slot：`OnSectionSelectionChanged` / `OnPasswordSelectionChanged` / DetailPanel 系列信号槽
- 保留：`Reload()` / 加密解密流程 / `ShowPasswordCreate()` / 同步信号处理
- 编辑 / 新建走 `EditorPanel` 右侧滑入面板（Page 2 已落地）
- 快捷键：`Ctrl+F` 聚焦搜索、`Ctrl+L` 锁定、`Ctrl+N` 新建

### CMake

`src/ui/CMakeLists.txt` 追加 `detail_panel.h` / `detail_panel.cc`。

### 验收清单

- [ ] 编译通过（`cmake --build build/mingw-x64-debug --target PassVault`）
- [ ] 启动后看到浅色三栏布局（左侧栏 / 中间密码列表 / 右侧详情）
- [ ] 侧栏 sections（全部/收藏夹/未分类/回收站）可切换，与分类列表选中态互斥
- [ ] 中间搜索框有左侧 search 图标；输入过滤密码列表
- [ ] 密码卡片显示：首字母头像 + 标题 + 用户名 + 相对时间 + 收藏星标（如适用）
- [ ] 点击某条 → 右侧详情面板显示对应内容，包括分类徽章
- [ ] 详情"密码"字段可显示/隐藏切换、可复制（走 `ClipboardManager::CopySensitive`）
- [ ] 详情"用户名"、"网址"字段可复制/打开
- [ ] 点击详情"编辑"按钮 → 右侧滑入 `EditorPanel`
- [ ] 点击详情"更多"（当前挂载删除）→ 二次确认后逻辑删除
- [ ] 点击收藏星标 → `SetFavorite` 生效，列表刷新
- [ ] 侧栏"新建密码"打开 `EditorPanel`；"设置"按钮切到 `PreferencesPage` 整页
- [ ] 侧栏底部"已同步/同步中"状态行随 SyncManager 信号更新
- [ ] 中英字符（Noto Sans SC）字体渲染正常，密码用 DM Mono

### 已知限制（Page 1 不动，后续处理）

- 排序菜单按钮无实际功能（仅视觉占位）
- 侧栏"新增分类"按钮无功能
- 回收站 section 仅列出已删除条目，无恢复/彻底删除按钮
- 风险横幅永远隐藏（等 `PasswordEntry.risky` 字段落地）
- `#SyncStatusDot` 状态色未随 idle/busy/error 变化（QSS 未定义 `[state]` 分支）
- 深色主题（tokens_dark）配色未按 Figma 定制，仅使用了新 accent
- MasterPasswordDialog / GeneratorDialog / OAuthWizardDialog / ImportPreviewDialog / Toast 视觉未更新

---

## Page 2 — Editor Panel（右侧滑入编辑器）

**状态**：✅ 代码就绪  
**落地**：`src/ui/editor_panel.{h,cc}` —— `QFrame` overlay，372px 右侧滑入（`raise()` + slide-in），左 98px 纵向 tab（Overview 实做，More info / Advanced 占位），右侧 Overview 表单（标题 / URL / 用户名 / 密码 + `GeneratorDialog` 触发 / 4 段强度 / 分类 / 备注），底部 Cancel（ghost）+ Save（primary）。`main_window` 编辑 / 新建入口全部改走 `EditorPanel::OpenForCreate()` / `OpenForEdit()`。

---

## Page 3 — Preferences Page（整页设置）

**状态**：✅ 代码就绪  
**落地**：`src/ui/preferences_page.{h,cc}` —— `QWidget` 塞入 `central_stack_` 替换 `preferences_placeholder_` stub。68px header + 920px 居中卡片 + 下划线三 tab（常规 / 同步 / 安全），行结构走 `MakeSettingRow` 辅助（36×36 图标方块 + 标题 + 描述 + 右侧控件）。信号契约 100% 复刻 `SettingsDialog`，`Application::WirePreferencesPage()` 一次性布线。

---

## 本轮改动清单（Phase 1–7）

- **Phase 1**：`tokens_light.qss` / `tokens_dark.qss` 从 ~15 变量扩至 ~110（`colors:` 全量 + 8 阶 `rounded`）
- **Phase 2**：`components.qss` + `app.qss` 补齐 badges / cards / tabs / strength-indicator / detail-icon-avatar / toggle-switch / eye-toggle
- **Phase 3**：Vault 三栏细节 tokens 化，`SyncStatusDot` 加 `[state]` 三色分支
- **Phase 4**：新增 `editor_panel.{h,cc}` 右侧滑入编辑器，替换旧 `PasswordDetailDialog`
- **Phase 5**：新增 `preferences_page.{h,cc}` 整页设置，替换旧 `SettingsDialog`
- **Phase 6**：4 个保留对话框（Master / Generator / OAuth / ImportPreview）视觉重构，业务逻辑零改动
- **Phase 7**：删除孤立的 `password_detail_dialog.{h,cc}` + `settings_dialog.{h,cc}`（共 642 行），`src/ui/CMakeLists.txt` 移除 4 行

---

## 后续任务（不在本轮）

- `PasswordEntry` 增加 `tag_color` / `risky` 字段 + 数据库 migration
- 深色主题（tokens_dark.qss）按 Figma dark spec 精细调色
- Editor 更多信息 / 高级设置 tab 的实际功能
- 侧栏新增分类、回收站恢复/清空业务逻辑
