# CLAUDE.md

本文件适用于仓库根目录及所有子目录。仅保留本项目必须遵循的约束；通用编码、Git、安全和沟通规则沿用用户全局配置。

## 项目与检索

PassVault 是 Windows 桌面密码管理器（C++17 / Qt 6 Widgets / Windows 10+），通过 Google Drive 与 Android 端同步加密数据。

- 存在 `.codegraph/` 时，理解或定位代码先用 `codegraph explore` / `codegraph node`，无结果再用 `rg` 或直接读取文件；不要自行刷新索引。
- 文档优先级：`docs/progress.md` → `docs/android_interop.md` → `DESIGN.md` → `BUG_FIX.md`。
- 入口为 `src/main.cpp` → `passvault::app::Application::Run()`；命名空间统一使用 `passvault::<module>`。
- 模块依赖顺序：`model → crypto → storage / cloud_crypto_service → master_password + session → csv + generator + oauth → sync → hello → ui → app`。

## 构建

构建前设置 `QT_ROOT`、`MINGW_ROOT`、`VCPKG_ROOT`，确保 CMake 与 Ninja 在 `PATH`。Google OAuth 凭据只能通过环境变量或 CMake 缓存变量提供，不得写入仓库。

除非用户要求，不主动构建。日常开发、快速调试和定向验证必须复用 `build/mingw-x64-release` 中已有的 CMake/Ninja 缓存做目标级增量编译，避免重复执行依赖部署：

```bash
cmake --build --preset mingw-x64-release --target passvault -j
cmake --build --preset mingw-x64-release --target passvault_tests -j
```

只有正式 Release 验收、需要刷新部署依赖或用户明确要求时，才运行包含配置检查和 `windeployqt` 的完整构建脚本：

```bash
scripts/build-release.sh
```

`--clean` 会删除构建目录，执行前必须取得用户确认。脚本会继续执行失败后的步骤，因此不能只看最终退出码；还要检查 configure/build 日志、产物时间戳，并确认以下文件可运行：

```text
build/mingw-x64-release/PassVault.exe
build/mingw-x64-release/passvault_tests.exe
```

定向测试优先直接运行测试二进制并使用 GTest filter。

## 跨端协议锁点

修改加密、模型或同步代码前必须阅读 `docs/android_interop.md` 和 `docs/progress.md` 的锁点表。不得擅自“改进”以下约定：

- PBKDF2-HMAC-SHA256：600000 次、32B key、16B salt；AES-256-GCM：12B IV、16B tag，输出 `ciphertext || tag`。
- Base64 使用标准表且无换行；`CloudBackupFormat` / `SyncPayloadV2` 的 JSON 键名和顺序与 Android 一致。
- Drive 路径为 `PassVault/PassVault_Cloud_Backup.json`，scope 为 `drive.file`，用完整 `modifiedTime` 做乐观锁。
- UUID 冲突时 `local.updatedAt >= cloud.updatedAt` 保留本地；tombstone 保留 30 天；同步防抖 5000ms。
- 主密码本地哈希固定为 `sha256(utf8)` 小写十六进制，无 salt、无迭代。
- 未分类映射：`categoryId == 0` ↔ `categoryUuid == ""`；Windows 不读取 SyncPayload V1。

## 代码与测试结构

- POD 可使用 public `snake_case` 成员；类成员使用 `snake_case_`。加密字段用 `QByteArray`，毫秒时间戳用 `std::int64_t`。
- 子目录通过 `target_sources(passvault PRIVATE ...)` 挂载源码，不创建独立模块库。
- `tests/CMakeLists.txt` 直接构建 `passvault_tests` 并显式列出每个测试源和被测 `.cc`；新增测试时必须同步更新。
- Windows Hello 测试使用 stub。没有新的 Android 真机样本时，不得替换现有 NIST/RFC 加密向量。
- 认证、日志和截图中不得出现真实密码、密钥、Token 或 OAuth 凭据。

## 主 Agent 执行约定

默认由主 Agent 直接完成任务评估、代码或文档修改、审查、构建测试、Windows GUI 操作、UI 效果分析和结果汇总。除非用户明确要求，否则不创建或使用子 Agent。

- 多步骤任务先明确范围、成功标准和验证顺序，再开始修改。
- 构建、依赖安装等长命令进行期间，每 60 秒内提供一次包含活跃进程、日志修改时间、日志增长或具体产物的可核验证据，禁止无证据空转。
- 保留单写者原则：同一时间只允许一个执行过程修改源文件或写入同一构建目录。
- 主 Agent 必须审核最终 diff、测试有效性、安全风险和无关改动，并区分通过、失败、未执行和环境阻塞。
- 用户明确要求使用子 Agent 时，再按任务范围约定分工；子 Agent 不是默认或强制验收条件。

### 快速调试层

快速调试层仅用于定位和修复已知失败或挂起用例的内循环，不得代替最终验收。主 Agent 可修改最小范围的代码或测试、直接增量构建受影响的测试目标，并且只运行能复现问题的最小定向用例。

- 默认不调用 `scripts/build-release.sh`，不创建正式 acceptance 目录，不执行完整回归、GUI 自动化或 UI 效果检查。
- 禁止使用 `--clean`、删除文件、访问真实数据、真实云端或认证流程，以及在日志或产物中输出密码、密钥、Token、OAuth 凭据等敏感信息。
- 进行中的状态更新间隔仍不得超过 60 秒，并提供活跃进程、日志修改时间或增长情况、具体产物等证据。用例挂起时，仅可在真正缺少活跃证据后精确终止相关测试进程。
- 调试日志和产物可写入 `build/diagnostics/<stage>/<timestamp>/` 等清晰标识的临时或诊断目录，但不得放入 `build/acceptance/` 或冒充正式验收证据。
- 最小定向用例确认稳定通过后，必须恢复正式验收流程，且不得降低最终通过条件。

### 阶段验收原则

- 以当前阶段文档中的检查清单为唯一验收范围；不主动扩展到其他阶段、通用自动化能力或清单未要求的状态组合。
- 开始前把清单整理为可独立判断的项目，并标明核心项、可选项和用户已批准的例外。优先验证用户可见的核心流程和真实结果。
- 复用本阶段已有的有效证据。已经通过且相关代码、环境和验收条件未变化的项目不得重复执行。
- 代码变更后只重跑受影响的定向测试和必要的相邻测试；仅在跨模块、高风险改动或用户明确要求时执行完整回归。
- GUI 自动化只服务于当前清单，不得为单个验收项扩建通用框架、大幅重构脚本或追求工具层面的完美覆盖。
- 自动化工具不支持某个控件时，可做一次最小替代尝试，并通过可观察的业务结果断言。仍无法稳定执行时标记为“自动化阻塞”或“需人工确认”，继续其他独立项目，不得反复重跑整个阶段。
- 区分产品缺陷与测试工具限制。只有应用的真实行为或结果不符合清单才算产品失败；像素级差异、全状态矩阵和非核心几何细节仅在清单明确要求时检查。

### 验收流程

1. **确定范围**：读取当前阶段检查清单，列出尚未验证的项目、既有证据和批准例外；没有受影响的已通过项直接保留结论。
2. **构建与代码测试**：复用 `build/mingw-x64-release` 做目标级增量构建，运行阶段定向测试和受影响的相邻测试，并检查产物可运行。
3. **GUI 检查**：使用隔离的 Release 副本、一次性主密码和固定演示数据。按小型独立场景执行清单，操作后验证实际业务结果；不得接触真实保险库、云端或认证账户。
4. **处理失败**：真实产品问题立即记录并做最小修复；自动化或环境阻塞单列后跳过。修复后只重跑失败项和受影响项目，不得默认重跑整段流程。
5. **收尾**：正常关闭测试进程，确认原数据未变化，按清单汇总通过、失败、阻塞、未执行和批准例外；未经用户确认不删除验收目录。

正式 GUI 验收目录必须唯一，启动前创建一次性运行标记，禁止复用已启动的目录。优先使用 Microsoft UI Automation；Qt 控件可回退到稳定的 Win32 输入，但必须验证操作后的可观察结果。

### 证据与通过条件

验收产物保存到：

```text
build/acceptance/<stage>/<timestamp>/
```

证据以能够判断当前阶段清单为限，至少保存结果汇总、相关构建/测试日志，以及清单要求的关键截图或测量结果。只有清单明确要求且具备可用参考时才生成像素差异图；不得为了补齐固定产物清单增加无关测试。

结论必须按检查项列出通过、失败、自动化/环境阻塞、未执行和用户批准例外。核心项通过且其余未通过项均属于批准例外时，可以建议阶段通过；未确认的核心项必须明确说明需要人工确认，不得伪装成自动化通过。用户负责最终人工 UI 效果确认。
