# AGENTS.md

本文件为 Codex 在此仓库中工作时必须遵循的项目说明，作用范围为仓库根目录及所有子目录。

## 代码检索

仓库根目录存在 `.codegraph/` 时，需要理解或定位代码应优先使用 CodeGraph，再使用 `rg` 或直接读取文件：

```bash
codegraph explore "要查找的符号或问题"
codegraph node <符号或文件>
```

如果 CodeGraph 没有索引目标文件或无法回答，再使用其他只读检索方式。不要自行创建或刷新 CodeGraph 索引。

## 项目定位

PassVault Windows 桌面端密码管理器（C++17 / Qt 6 Widgets / Windows 10+），通过 Google Drive 加密文件与 PassVault Android 端做增量同步。加密参数与 JSON 契约必须与 Android 端**逐字对齐**，改动前先读 `docs/android_interop.md` 与 `docs/progress.md` 里的「跨端互通协议锁点」表。

## 常用命令

构建前设置 `QT_ROOT`、`MINGW_ROOT` 和 `VCPKG_ROOT`，并确保 Ninja 与 CMake 在 `PATH` 中。
除非用户要求，否则不要自行编译。

```bash
cmake --preset mingw-x64-release          # 配置
cmake --build --preset mingw-x64-release  # 构建
ctest --preset mingw-x64-release          # 跑全部测试
```

单测：直接跑二进制加 GTest filter，比 `ctest -R` 输出更好：

```bash
build/mingw-x64-debug/tests/passvault_tests.exe --gtest_filter=CryptoServiceTest.*
```

Release + windeployqt 一次到位（用户偏好用它验证发布产物）：

```bash
scripts/build-release.sh          # 增量
scripts/build-release.sh --clean  # 干净重建
```

## 构建前置（不做会直接失败）

1. **Google OAuth 凭据** —— 通过 `GOOGLE_DESKTOP_CLIENT_ID` / `GOOGLE_DESKTOP_CLIENT_SECRET` 环境变量或同名 CMake 缓存变量提供。CMake configure 阶段生成 `build/.../generated/passvault/oauth/google_oauth_config.h`；空值会编译过但 OAuth 登录跑不通。不要把真实凭据写入仓库文件。
2. **SQLite3 amalgamation** —— 仓库已包含 `third_party/sqlite3/sqlite3.c` 与 `sqlite3.h`。更新版本时从 sqlite.org 下载新的 amalgamation；缺文件时 CMake 会报错。
3. **vcpkg** —— `VCPKG_ROOT` 指向本地 vcpkg，triplet 是 `x64-mingw-dynamic`。首次 configure 会自动装 OpenSSL 3.0 + GTest 1.14。

## 代码组织

入口 `src/main.cpp` → `passvault::app::Application::Run()`（PIMPL，`unique_ptr<Impl>` 藏所有子系统）。所有代码走 `passvault::<module>` 命名空间。模块按跨端互通依赖顺序自下而上：

```
model → crypto → storage / cloud_crypto_service →
master_password + session →
csv + generator + oauth → sync → hello → ui → app
```

各模块职责：

| 目录 | 责任 |
|---|---|
| `src/model/` | POD：`PasswordEntry` / `Category` / `CloudBackupFormat` / `SyncPayloadV2`；字段名与 Android data class 一一对应，改名等于破坏协议 |
| `src/crypto/` | `SecureBytes`（RAII + `VirtualLock` + 常时比较）/ `KeyDerivation`（PBKDF2）/ `CryptoService`（AES-GCM）/ `Sha256` / `Random`（`BCryptGenRandom`）/ `SessionKey` |
| `src/storage/` | `Database` + `Statement` + `PasswordDao` + `CategoryDao`；本地 DB 走 `<applicationDirPath>/data/passvault.db`（绿色便携） |
| `src/master_password/` | 主密码 `sha256(utf8)` 小写十六进制哈希持久化 + 三态管理器（Setup / Unlock / Change） |
| `src/session/` | `SessionManager`（全局单例，供 `SyncManager` 拿 `SessionKey`）+ `AutoLockTimer` |
| `src/csv/` | 导入导出，UTF-8 BOM + 首行 `ID,分类,标题,用户名,密码,网站,备注` |
| `src/generator/` | `PasswordGenerator` + `PasswordStrength`（`len≥12 && types≥3 → 3` / `len≥8 && types≥2 → 2` / else 1） |
| `src/oauth/` | PKCE + 本机 loopback HTTP 回调 + `TokenStore`（Windows DPAPI） |
| `src/sync/` | `CloudCryptoService` / `GoogleDriveProvider` / `SyncManager` / `SyncScheduler` / `MergeAlgorithm` |
| `src/hello/` | `windows_hello_unlock.cc`（真实实现）+ `_stub.cc`（测试用桩） |
| `src/ui/` | `MainWindow` + `DetailPanel` / `EditorPanel` / `PreferencesPage` / `MasterPasswordDialog` / `OAuthWizardDialog` / `ImportPreviewDialog` / `GeneratorDialog` / `Toast` / `ThemeManager`（加载 `app.qss`） |
| `src/app/` | `Application` PIMPL，装配所有子系统 |

每个子目录自带 `CMakeLists.txt`，通过 `target_sources(passvault PRIVATE ...)` 把源文件挂到根目标上，不生成独立静态库。第三方 `sqlite3` 单独一个 `sqlite3_amalgamation` 静态库。

## 测试

- 用 GoogleTest + `gtest_discover_tests`。
- **不常规**：`tests/CMakeLists.txt` 里直接 `add_executable(passvault_tests ...)` 显式列出**每一个**被测 `.cc` 源文件（不是链接 `passvault` 目标），避免 Qt Widgets/UI 依赖污染纯逻辑测试。加新测试需要同步在 `tests/CMakeLists.txt` 追加：一行测试源、若干行被测源。
- Windows Hello 用 `windows_hello_unlock_stub.cc` 替换真实调用（见 `tests/CMakeLists.txt` 同时挂了两个文件）。
- `docs/android_interop.md` 里的加密向量是硬编码进 `crypto_service_test.cc` / `key_derivation_test.cc` 的 NIST 与 RFC 已知向量——**不要**在没有新真机样本时替换。

## 跨端协议锁点

改动这些常量前先读 `docs/android_interop.md` 与 `docs/progress.md`「跨端互通协议锁点」表，改错会造成 Android 端解密失败：

- PBKDF2-HMAC-SHA256，600000 iter，32B key，16B salt，12B IV
- AES-256-GCM，Tag 128 位，密文字节序 = `ciphertext ‖ tag(16B)`
- Base64 标准表无换行（对齐 Android `Base64.NO_WRAP`）
- `CloudBackupFormat` / `SyncPayloadV2` 的 JSON 键名与字段顺序
- Google Drive：根 `PassVault/PassVault_Cloud_Backup.json`，`drive.file` scope，`modifiedTime` 字符串完整比较做乐观锁
- `mergeByUuid`：UUID 相同时 `local.updatedAt >= cloud.updatedAt` 保留本地；tombstone 保留 30 天
- 主密码本地哈希：`sha256(utf8)` 小写十六进制，**无 salt、无迭代**（刻意与 Android 一致，不要"改进"）
- 未分类：`categoryId == 0` ↔ `categoryUuid == ""`
- 同步防抖 5000 ms

## 项目本地文档

优先级依次读：

1. `docs/progress.md` —— 模块进度、每 Task 决策记录、剩余 TODO；开发进度真源
2. `docs/android_interop.md` —— 加密参数与 JSON 字段契约
3. `DESIGN.md` —— Figma 视觉规范；`src/ui/theme_manager.cc` + `resources/qss/app.qss` 就是它的落地
4. `BUG_FIX.md` —— 用户报的 UI 缺陷清单

## 代码约定（本仓库特有）

- POD 结构体允许 public + `snake_case` 成员（Google Style 对 `struct` 的例外）；类成员按标准 `snake_case_` 加尾划线。
- 加密字段用 `QByteArray`，13 位毫秒时间戳用 `std::int64_t`。
- 顶层 CMake 的 `add_compile_definitions(UNICODE _UNICODE)` + Windows 平台链接 `crypt32 / bcrypt / ncrypt`——写涉及 Win32 加密/证书的模块时假定这些已经就位。
- Windows 端**不读** SyncPayload V1（v1 只在 Android 端，遇到会在 UI 提示走 Android 升级）。
