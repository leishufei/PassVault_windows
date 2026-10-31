# PassVault

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform: Windows 10+](https://img.shields.io/badge/Platform-Windows%2010%2B-0078D4.svg)](#环境要求)
[![Language: C++17](https://img.shields.io/badge/Language-C%2B%2B17-00599C.svg)](#技术栈)

## 项目简介

PassVault 是一款面向 Windows 10 及以上系统的本地优先密码管理器，使用 C++17 与 Qt 6 Widgets 编写。密码经 AES-256-GCM 加密后保存在本地 SQLite 保险库中，并可选择通过 Google Drive 同步加密备份；当前版本 1.0。

- **本地优先**：保险库位于当前用户的 `%APPDATA%\PassVault` 目录，无网络时也可完整使用。
- **默认加密**：本地密码字段与云端备份均使用 AES-256-GCM。
- **可选同步**：只上传加密备份，按 UUID 与 `updatedAt` 合并本地和云端数据。

## 功能

- 密码保险库：新增、编辑、删除条目，保存标题、用户名、密码、网站、备注与分类，并支持收藏。
- 分类与检索：自定义分类，按关键词搜索，按更新时间、创建时间或标题排序。
- 回收站：逻辑删除的条目支持恢复、永久删除和清空回收站。
- 密码生成器：长度可在 8–32 位之间调整，自由组合大小写字母、数字与符号，并显示强度提示。
- CSV 导入导出：导出活跃条目；导入兼容 7 列扩展格式与 4 列精简格式，写入前预览新增、更新与无效行。
- Google Drive 加密同步：仅上传加密备份，使用完整 `modifiedTime` 乐观锁；同 UUID 冲突时保留 `updatedAt` 较新的一方，并清理 30 天前的 tombstone。
- 自动锁定与剪贴板清理：默认空闲 5 分钟锁定，敏感剪贴板内容默认 30 秒后清除，均可在设置中调整。
- 主密码管理：首次设置与修改，验证旧密码后可修改。
- 界面主题：跟随系统、浅色或深色。

## 技术栈

- 语言与 UI：C++17、Qt 6 Widgets（Core、Widgets、Network、Svg）。
- 加密：OpenSSL 3.0（AES-256-GCM、PBKDF2-HMAC-SHA256）。
- 存储：SQLite 3 amalgamation（仓库内 `third_party/sqlite3`）。
- 构建：CMake 3.24+、Ninja、vcpkg、MinGW-w64。
- 测试：GoogleTest，经 `gtest_discover_tests` 注册到 CTest。

## 加密与同步格式

| 项目 | 约定 |
| --- | --- |
| 密钥派生 | PBKDF2-HMAC-SHA256，600000 次迭代，32 字节密钥，16 字节 salt |
| 数据加密 | AES-256-GCM，12 字节 IV，16 字节 tag，输出 `ciphertext || tag` |
| 编码 | 标准 Base64，无换行 |
| 主密码本地校验 | `sha256(utf8)` 小写十六进制，无 salt、无迭代 |
| Drive 路径 | `PassVault/PassVault_Cloud_Backup.json` |
| OAuth 范围 | `https://www.googleapis.com/auth/drive.file` |
| 并发控制 | 完整 `modifiedTime` 乐观锁 |
| UUID 冲突 | `local.updatedAt >= cloud.updatedAt` 时保留本地 |
| 删除标记 | tombstone 保留 30 天 |
| 同步防抖 | 5000 ms |

云端文件外层为 `CloudBackupFormat` JSON，包含 `version`、`kdfAlgorithm`、`iterations`、`salt`、`encryptionAlgorithm`、`iv` 与 `ciphertext`；`ciphertext` 解密后为 `SyncPayloadV2`（`version`、`syncTimestamp`、`passwords`、`categories`），按 UUID 与时间戳合并。

## 环境要求

| 组件 | 要求 | 配置方式 |
| --- | --- | --- |
| Windows | 10 及以上 | - |
| Qt | 6.5 及以上，含 Core、Widgets、Network、Svg | 设置 `QT_ROOT` |
| MinGW-w64 | x86_64，与 Qt 套件匹配 | 设置 `MINGW_ROOT` |
| CMake | 3.24+ | 加入 `PATH` |
| Ninja | 任意近期版本 | 加入 `PATH` |
| vcpkg | 能提供 OpenSSL 3.0 与 GoogleTest 1.14+ | 设置 `VCPKG_ROOT` |

CMake preset 使用 vcpkg triplet `x64-mingw-dynamic`，并把 `MINGW_ROOT/bin`、`QT_ROOT/bin` 加入构建过程 `PATH`。代理等本机差异请通过环境变量或未跟踪的 `CMakeUserPresets.json` 配置。

## 快速开始

配置、构建并运行测试（Debug 或 Release）：

```bash
cmake --preset mingw-x64-debug
cmake --build --preset mingw-x64-debug -j
ctest --preset mingw-x64-debug
```

Release 只需把 preset 替换为 `mingw-x64-release`。产物位于 `build/<preset>/`，包含 `PassVault.exe` 与 `passvault_tests.exe`。定向运行测试可使用 GoogleTest filter：

```bash
./build/mingw-x64-release/passvault_tests.exe --gtest_filter='*CryptoService*'
```

正式发布运行 `scripts/build-release.sh`，它会依次执行 Release 配置、构建与 `windeployqt` 部署，并检查两个可执行文件是否生成。`--clean` 会先删除 `build/mingw-x64-release`；脚本不会因单步失败而提前退出，需同时检查 configure、build 和部署日志。

## Google Drive 同步配置

1. 在 Google Cloud Console 创建 Desktop 类型的 OAuth Client。
2. 通过 `GOOGLE_DESKTOP_CLIENT_ID`、`GOOGLE_DESKTOP_CLIENT_SECRET` 环境变量或同名 CMake 缓存变量提供凭据：

   ```bash
   cmake --preset mingw-x64-release -DGOOGLE_DESKTOP_CLIENT_ID=... -DGOOGLE_DESKTOP_CLIENT_SECRET=...
   ```

3. 配置阶段通过 `configure_file` 在构建目录生成 `passvault/oauth/google_oauth_config.h`；授权使用 loopback 回调与 PKCE，只申请 `drive.file` 范围。

真实凭据不得写入仓库、日志或截图。刷新令牌由 DPAPI 加密后保存在当前用户的 `%APPDATA%\PassVault\google_token.dat`。

## 数据与安全

- 本地保险库位于当前用户的 `%APPDATA%\PassVault`：`passvault.db`（SQLite，WAL 模式）、`master.dat`（DPAPI 保护的主密码记录）与 `hello_unlock.dat`。
- 首次设置的主密码不能为空。本地只保存 `sha256(utf8)` 小写十六进制校验值，记录本身经 Windows DPAPI 保护；会话密钥由 PBKDF2 从主密码和随机 salt 派生。
- 条目密码字段在写入 SQLite 前使用会话密钥加密；标题、用户名、网站与备注以明文存储。
- Google 登录刷新令牌使用 DPAPI 加密，并加入由 `MachineGuid` 派生的 entropy；访问令牌仅保存在内存。
- 上传到 Google Drive 的内容只有加密后的 `ciphertext` 及 KDF、salt、IV 等必要元数据，不包含明文条目字段。
- 锁定或退出时会停止自动锁定计时、清理敏感剪贴板内容并释放会话密钥。同一 Windows 用户仅允许运行一个实例。

## 目录结构

| 路径 | 说明 |
| --- | --- |
| `src/app` | 应用生命周期、单实例保护、本地数据清理 |
| `src/crypto`、`src/model`、`src/storage` | 加密原语，密码/分类/云备份模型，SQLite schema 与 DAO |
| `src/master_password`、`src/session` | 主密码记录、策略、修改协调，会话密钥与自动锁定 |
| `src/csv`、`src/generator` | CSV 读写/校验/导入导出，密码生成与强度估算 |
| `src/oauth`、`src/sync` | Google OAuth/PKCE/令牌存储，Drive 同步、合并与云端加密 |
| `src/hello`、`src/ui` | Windows Hello 模块，Qt Widgets 界面与主题 |
| `tests`、`resources` | GoogleTest 测试，QSS 主题、图标与字体 |
| `third_party/sqlite3`、`cmake`、`scripts` | SQLite 源码，vcpkg triplet/OAuth 模板，Release 构建脚本 |

## 许可证

本项目使用 MIT 许可证，详见 [LICENSE](LICENSE)。

Copyright (c) 2026 leishufei
