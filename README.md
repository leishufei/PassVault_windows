# PassVault

PassVault 是 Windows 桌面密码管理器，使用 C++17 与 Qt 6 Widgets 编写，通过 Google Drive 与 PassVault Android 端同步加密备份。当前版本 1.0.0。

## 功能

- 密码保险库：新增、编辑、删除密码条目，保存标题、用户名、密码、网站、备注与分类。
- 分类与检索：自定义分类，按关键词搜索，收藏条目，按更新时间、创建时间或标题排序。
- 回收站：逻辑删除的条目支持恢复、永久删除和清空回收站。
- 密码生成器：可选长度 8 至 32 位，按需组合大小写字母、数字和符号，并显示强度提示。
- CSV 导入导出：导出活跃条目；导入支持字段校验与新增、更新、无效三类预览。
- Google Drive 加密同步：仅上传 AES-256-GCM 加密后的备份，支持与 Android 端按 UUID 合并。
- 自动锁定与剪贴板清理：默认空闲 5 分钟锁定，敏感剪贴板内容默认 30 秒后清除，均可在设置中调整。

## 技术栈

- 语言：C++17
- UI：Qt 6 Widgets（Core、Widgets、Network、Svg）
- 加密：OpenSSL 3.0（AES-256-GCM、PBKDF2-HMAC-SHA256）
- 存储：SQLite 3 amalgamation（仓库内 `third_party/sqlite3`）
- 构建：CMake 3.24+、Ninja、vcpkg、MinGW-w64
- 测试：GoogleTest，CMake `gtest_discover_tests` 注册到 CTest

## 跨端协议要点

| 项目 | 约定 |
| --- | --- |
| 密钥派生 | PBKDF2-HMAC-SHA256，600000 次迭代，32 字节密钥，16 字节 salt |
| 数据加密 | AES-256-GCM，12 字节 IV，16 字节 tag，密文后拼接 tag |
| 编码 | Base64 标准表，无换行 |
| 主密码本地校验 | `sha256(utf8)` 小写十六进制，无 salt、无迭代 |
| Drive 路径 | `PassVault/PassVault_Cloud_Backup.json` |
| OAuth 范围 | `https://www.googleapis.com/auth/drive.file` |
| 并发控制 | 完整 `modifiedTime` 乐观锁 |
| UUID 冲突 | `local.updatedAt >= cloud.updatedAt` 时保留本地 |
| 删除标记 | tombstone 保留 30 天 |
| 同步防抖 | 5000 ms |

云备份外层为与 Android 端一致的 `CloudBackupFormat` JSON，内部为加密后的 `SyncPayloadV2` 数据；Windows 端不读取 V1 载荷。

## 环境要求

| 组件 | 要求 | 配置方式 |
| --- | --- | --- |
| Windows | 10 及以上 | - |
| Qt | 6.11.1 mingw_64（最低 6.5） | 设置 `QT_ROOT` |
| MinGW-w64 | GCC 13.1.0 x86_64-posix-seh | 设置 `MINGW_ROOT` |
| CMake | 3.24+ | 加入 `PATH` |
| Ninja | 1.12.1 | 加入 `PATH` |
| vcpkg | 最新 | 设置 `VCPKG_ROOT` |
| OpenSSL | 3.0+ | 由 vcpkg 提供 |

CMake preset 使用 vcpkg triplet `x64-mingw-dynamic`，并把 `MINGW_ROOT/bin` 和 `QT_ROOT/bin` 加入构建过程 `PATH`。代理等本机差异请通过环境变量或未跟踪的 `CMakeUserPresets.json` 配置，不要写入共享 preset。

## 构建与测试

配置并构建 Debug：

```bash
cmake --preset mingw-x64-debug
cmake --build --preset mingw-x64-debug -j
ctest --preset mingw-x64-debug
```

Release 使用同一流程：

```bash
cmake --preset mingw-x64-release
cmake --build --preset mingw-x64-release -j
ctest --preset mingw-x64-release
```

产物位于 `build/mingw-x64-debug` 或 `build/mingw-x64-release`，包含 `PassVault.exe` 和测试程序 `passvault_tests.exe`。定向运行测试可直接执行测试二进制并使用 GoogleTest filter：

```bash
./build/mingw-x64-release/passvault_tests.exe --gtest_filter='*CryptoService*'
```

### 正式发布构建

`scripts/build-release.sh` 依次执行 Release 配置、构建和 `windeployqt` 部署，并检查 `PassVault.exe` 与 `passvault_tests.exe` 是否生成：

```bash
scripts/build-release.sh
```

传入 `--clean` 会先删除 `build/mingw-x64-release`，请确认后再使用。该脚本不会因单步失败而提前退出，需要同时检查 configure、build 和部署日志。

## Google Drive 同步配置

1. 在 Google Cloud Console 创建 Desktop 类型的 OAuth Client。
2. 配置前设置 `GOOGLE_DESKTOP_CLIENT_ID` 和 `GOOGLE_DESKTOP_CLIENT_SECRET` 环境变量，或通过同名 CMake 缓存变量传入：

   ```bash
   cmake --preset mingw-x64-release \
       -DGOOGLE_DESKTOP_CLIENT_ID=... \
       -DGOOGLE_DESKTOP_CLIENT_SECRET=...
   ```

3. 配置阶段通过 `configure_file` 生成 `passvault/oauth/google_oauth_config.h`，供应用编译使用。
4. 桌面端使用 loopback 回调与 PKCE 完成授权，只申请 `drive.file` 范围。

真实凭据不得写入仓库、日志或截图。刷新令牌由 DPAPI 加密后保存在当前用户的 `QStandardPaths::AppDataLocation/google_token.dat`。

## 数据与安全

- 本地保险库位于程序所在目录的 `data/` 下：`passvault.db`（SQLite，WAL 模式）、`master.dat`（DPAPI 保护的主密码记录）和 `hello_unlock.dat`。应用为便携式布局，数据随可执行文件目录存放。
- 主密码首次设置要求至少 12 位。本地只保存 `sha256(utf8)` 小写十六进制校验值，记录本身经 Windows DPAPI 保护。
- Google 登录刷新令牌使用 DPAPI 加密，并加入由 `MachineGuid` 派生的 entropy；访问令牌仅保存在内存。
- 上传到 Google Drive 的内容只有加密后的 `ciphertext` 及 KDF、salt、IV 等必要元数据，不包含明文密码或未加密的条目字段。
- 锁定或退出时会停止自动锁定计时、清理敏感剪贴板内容并释放会话密钥。

## 目录结构

| 路径 | 说明 |
| --- | --- |
| `src/app` | 应用生命周期、单实例保护、本地数据清理 |
| `src/crypto` | AES-GCM、PBKDF2、SHA-256、随机数与安全字节容器 |
| `src/model` | 密码、分类、云备份格式与 SyncPayloadV2 模型 |
| `src/storage` | SQLite 数据库、schema 与 DAO |
| `src/master_password` | 主密码记录、策略与修改协调 |
| `src/session` | 会话密钥状态与自动锁定计时器 |
| `src/csv` | CSV 读写、校验、导入与导出 |
| `src/generator` | 密码生成与强度估算 |
| `src/oauth` | Google OAuth、PKCE、loopback 服务与令牌存储 |
| `src/sync` | Drive 提供方、同步调度、合并算法与云端加密 |
| `src/hello` | Windows Hello 可用性检测与凭据保护模块 |
| `src/ui` | Qt Widgets 界面、对话框与主题资源加载 |
| `tests` | GoogleTest 单元测试与 GUI 测试 |
| `resources` | QSS 主题、图标与字体 |
| `third_party/sqlite3` | SQLite amalgamation 源码 |
| `cmake` | vcpkg triplet 与 OAuth 配置模板 |
| `scripts` | 正式 Release 构建脚本 |

## 许可证

本项目使用 MIT 许可证，详见 [LICENSE](LICENSE)。

Copyright (c) 2026 leishufei
