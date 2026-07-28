# GUI 自动化测试计划与执行记录

## 背景

PassVault 已完成 Task 10–20 全部功能，现有 25 个单元测试（Google Test + QSignalSpy）覆盖了
**逻辑层**（crypto、DAO、generator、strength、session、csv、sync、pkce 等），但**没有任何 GUI 层测试**。
本文档记录为现有 GUI 功能补齐自动化测试的计划、分阶段任务与执行总结。

GUI 测试聚焦**界面层**：信号接线、控件交互、校验提示、渲染状态，不重��测已被单元测试覆盖的底层逻辑。

## 关键决策

- **外部依赖功能**（Windows Hello 生物识别、Google Drive 真实同步、OAuth 真实授权）→ 跳过，标注人工验证。
- **MainWindow** → 完整集成测试（内存 DB + 注入 Deps + 真实数据流程）。
- **控件定位** → 给需要驱动的控件补唯一 objectName，测试用 `findChild<T*>("name")` 定位；
  已带 QSS 样式名的控件不改名，直接复用或按类型查找。

## 基础设施改动

- `tests/test_main.cc`：`QCoreApplication` → `QApplication`；构造前无头化
  （`QT_QPA_PLATFORM` 为空时 `qputenv(... "offscreen")`）。
- `tests/CMakeLists.txt`：`find_package` 与 `target_link_libraries` 追加 `Qt6::Widgets`、`Qt6::Svg`；
  追加 UI 源文件（main_window/detail_panel/editor_panel/preferences_page/generator_dialog/
  master_password_dialog/import_preview_dialog/toast/theme_manager/icon_loader/clipboard_manager）；追加 8 个测试文件。
- 生产 `.cc`：仅给当前无名的交互控件新增 `setObjectName`（非行为改动，无中文注释）。

## 分阶段任务

### Phase 1 — 基础设施打通
- [x] 改 `test_main.cc` 为 `QApplication` + offscreen
- [x] `tests/CMakeLists.txt` 加 `Qt6::Widgets`/`Qt6::Svg` + `toast.cc` + `toast_test.cc`
- [x] 写 `toast_test.cc`（Toast 已自带 objectName，无需改生产代码）
- [x] `cmake configure + build + ctest` 跑通，确认 GUI 测试链路可用
- [x] 追加：CMake POST_BUILD 部署 `qoffscreen.dll` 到 exe 旁 `platforms/`；`CMakePresets.json` 补 release 测试 preset

### Phase 2 — 独立对话框/面板
按顺序，每个：补 objectName → 写 `*_test.cc` → build + ctest 绿灯。
- [x] `generator_dialog`（slider/spin/4 checkbox 补名）
- [x] `preferences_page`（combo/2 spin/按钮/hello toggle 补名）
- [x] `master_password_dialog`（3 模式：edits/hello/primary 补名）
- [x] `editor_panel`（inputs/combo/notes/按钮补名）
- [x] `detail_panel`（copy/toggle/edit/favorite/website 按钮补名）
- [x] `import_preview_dialog`（tab/summary，主要靠 `validation()` 断言）

### Phase 3 — MainWindow 完整集成
- [ ] `main_window.cc` 补名（search/3 lists/count/sync labels/按钮）
- [ ] 写 `main_window_test.cc`：内存 DB + 种子数据 + 搜索/分类/选择/新建/编辑/删除/收藏/工具栏信号
- [ ] build + ctest 全绿

## 各组件测试用例

### GeneratorDialog
默认生成非空；slider↔spin 同步；改长度改输出长度；勾选字符类型影响字符集；全不勾→error 可见不崩；
`accept()` 后 `password()` 返回 preview；强度条更新。

### PreferencesPage
theme/auto-lock/clipboard 改值 → 对应 `*Changed` 信号；connect/disconnect/sync/import/export/change-master/back
按钮 → 对应 request 信号；hello toggle → Enable/Disable 信号（仅信号）；
`SetHelloAvailable/SetHelloEnabled/SetGoogleDriveConnected` 更新 UI 状态。

### MasterPasswordDialog
kSetup：一致且 ≥8 可 accept、`NewPassword()` 正确，不一致/过短被拦；kUnlock：hello 按钮可见性 + `HelloRequested`；
kChange：`OldPassword()`/`NewPassword()` 正确、confirm 校验；预览切换；`SetErrorText` 显示。

### EditorPanel
`OpenForCreate` 清空、`OpenForEdit` 回填；`Result()` 反映改动；save/cancel/generate 发信号；
`ApplyGeneratedPassword` 写入密码框；强度段更新。

### DetailPanel
`SetEntry`/`ClearEntry` → `HasEntry`/`entry_id`/渲染；edit/delete/copy-pw/copy-user/open-web/favorite
发对应信号携正确 id（favorite 带 desired）；密码显/隐切换。

### ImportPreviewDialog
按 N/M/K 构造 → 三 tab 计数 + summary 正确；`accept()`/`reject()`；`validation()` 一致。

### Toast
`Show` 后出现、label 文本正确；到时淡出销毁；不同 Level 应用不同样式。

### MainWindow（集成）
列表渲染 + count；搜索过滤 + Esc 清空；section/category 切换过滤；选中→detail 显示；
New/Ctrl+N 开编辑器；新建/编辑/删除/收藏落库并刷新；lock/settings/import/export/change-master 工具栏信号；
复制密码走通 ClipboardManager + Toast（不断言剪贴板内容）。

## 需人工验证（GUI 自动化跳过）

- Windows Hello 实际注册/解锁（真实生物识别硬件）。
- Google Drive 实际连接与上传/下载/合并（真实账号 + 网络）。
- OAuth 授权全流程与 `OAuthWizardDialog`（真实浏览器回调）。
- 剪贴板实际内容与自动清空计时（offscreen 下 `QClipboard` 受限）。

后端逻辑（SyncManager、SyncScheduler、windows_hello_unlock、pkce、token_store、loopback_http_server）已有单元测试覆盖。

## 验证方式

```bash
cmake --preset mingw-x64-release
cmake --build --preset mingw-x64-release --target passvault_tests
ctest --preset mingw-x64-release -R Toast --output-on-failure   # 单组
ctest --preset mingw-x64-release --output-on-failure            # 全量
```

> 注意：必须用 `ctest --preset`（含 Qt/MinGW bin 的 PATH 环境）跑；`--test-dir` 直跑会因缺 DLL 报 `0xc0000135`。

---

## 执行总结

> 每阶段完成后在此回写简短结果。

### Phase 1 ✅（基础设施打通）
- `test_main.cc` 改 `QApplication`，`QT_QPA_PLATFORM` 为空时设 `offscreen`。
- `tests/CMakeLists.txt` 加 `Qt6::Widgets`/`Qt6::Svg`，加 `src/ui/toast.cc` + `toast_test.cc`。
- 排障：改 `QApplication` 后 `gtest_discover_tests` 因 offscreen 插件缺失卡死 → CMake POST_BUILD 把
  `Qt6::QOffscreenIntegrationPlugin` 拷到 exe 旁 `platforms/`；`0xc0000135` 缺 DLL → 补 release 测试 preset，用 `ctest --preset` 跑。
- `toast_test.cc` 3 个用例全过（文本渲染 / Level→objectName / 到时自动销毁）。

### Phase 2 — generator_dialog ✅
- `generator_dialog.cc` 补 6 个交互控件 objectName（`GeneratorLengthSlider`/`GeneratorLengthSpin`/
  `GeneratorUpper`/`GeneratorLower`/`GeneratorNumber`/`GeneratorSymbol`）；显示型控件复用现有样式名
  （`StrengthBar`/`FormError`）。
- `generator_dialog_test.cc` 6 个用例全过：默认长度 16 / slider↔spin 同步 / 改长度改输出 /
  全不勾→error 可见且 preview 清空 / accept 后 `password()` 返回 preview / 强度条 >0。

### Phase 2 — preferences_page ✅
- `preferences_page.cc` 补 9 个交互控件 objectName（`ThemeCombo`/`AutoLockSpin`/`ClipboardSpin`/
  `DriveConnectButton`/`DriveDisconnectButton`/`SyncNowButton`/`ImportButton`/`ExportButton`/
  `ChangeMasterButton`/`HelloToggle`）；显示型控件复用现有样式名（`SettingRowStatus`/`PreferencesBack`）。
- `preferences_page_test.cc` 17 个用例全过：3 控件值改变发信号 / 9 按钮点击发信号 / hello toggle 勾选发 Enable/Disable /
  SetHelloAvailable/SetHelloEnabled 更新 UI / SetGoogleDriveConnected 更新状态文本与 syncNow 启用状态 / back 按钮发信号。
- 跳过按钮 visibility 断言（offscreen 平台下 isVisible 行为受限，实际运行正常）。

### Phase 2 — master_password_dialog ✅
- `master_password_dialog.cc` 补 6 个控件 objectName（`OldPasswordEdit`/`NewPasswordEdit`/
  `ConfirmPasswordEdit`/`PreviewCheckbox`/`HelloButton`/`PrimaryButton`）；`error_` 复用现有 `FormError`。
- `master_password_dialog_test.cc` 11 个用例全过：kSetup 一致且≥8 可 accept、不一致/过短被拦；
  kUnlock hello 按钮默认隐藏、SetHelloAvailable(true) 后点击发 HelloRequested、无 confirm/old 字段；
  kChange OldPassword/NewPassword 正确、confirm 不一致/与旧相同被拦；preview 切 echoMode；SetErrorText 显示。
- **offscreen 可见性经验**：顶层窗口未 `show()` 时 `isVisible()` 恒为 false（子控件受祖先影响）；
  改用 `isHidden()` 断言「显式隐藏标志」——不依赖窗口是否显示。所有 GUI 测试用 `isHidden()` 而非 `isVisible()`。

### Phase 2 — editor_panel ✅
- `editor_panel.cc` 补 10 个控件 objectName（`EditorTitleInput`/`EditorUsernameInput`/`EditorPasswordInput`/
  `EditorWebsiteInput`/`EditorNotesInput`/`EditorCategoryCombo`/`EditorPreviewToggle`/`EditorSaveButton`/
  `EditorCancelButton`/`EditorStrengthLabel`）；复用现有 `EditorHeaderTitle`/`EditorGenerateButton`。
- `editor_panel_test.cc` 12 个用例全过：OpenForCreate 清空+开+标题「新建密码」/ OpenForEdit 回填各字段+标题「编辑密码」/
  Result 反映编辑 / save 有效字段发信号、空标题不发 / cancel/generate 发信号 / ApplyGeneratedPassword 写密码框 /
  改密码更新强度标签（强度3需≥12位+≥3类）/ preview toggle 切 echoMode / SetCategories 填 combo / Close 置 IsOpen false。
- 无 parent 构造：`AnimateIn/Out`、`UpdatePositionFromParent` 提前返回，`IsOpen()`/`Close()` 仍正常。

### Phase 2 — detail_panel ✅
- **无需改生产代码**：所有交互控件早已带 QSS objectName。header/field 按钮共享名
  （`DetailHeaderButton`/`DetailFieldButton`），改用 `findChildren<T*>(name)` + 创建序索引定位
  （DFS = 构造序：field 按钮 0 复制用户名/1 密码显隐/2 复制密码/3 打开网站；header QToolButton 0 更多=删除/1 收藏；
  header-edit 是唯一同名 QPushButton）。复制密码/打开网站另有唯一名按钮（`DetailSecondaryButton`/`DetailPrimaryButton`）直接用。
- `detail_panel_test.cc` 9 个用例全过：SetEntry 渲染+HasEntry/entry_id / ClearEntry 复位 /
  edit/delete/copy-pw/copy-user/open-web 发信号携正确 id / favorite toggle 带 desired /
  密码显隐在圆点掩码与明文间切换。

### Phase 2 — import_preview_dialog ✅
- **无需改生产代码**：`summary_`=`DialogSubtitle`、`tabs`=`ImportPreviewTabs` 已带名；三棵 QTreeWidget 按
  `tabs->widget(0/1/2)` 取（insert/update/invalid 构造序）；确认/取消按钮按 `QDialogButtonBox` 的
  Accept/Reject role 定位。
- `import_preview_dialog_test.cc` 8 个用例全过：summary 文本计数 / 三 tab 标题带计数 /
  三树 topLevelItemCount 与 update 子项(diff)数 / valid_rows 空时确认禁用、非空时启用 /
  点确认→Accepted、点取消→Rejected / `validation()` 计数一致。
