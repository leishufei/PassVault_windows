# PassVault Windows

Windows 桌面版密码管理器，与 PassVault Android 端通过 Google Drive 加密同步。

- 语言：C++17（Google C++ Style）
- UI：Qt 6 Widgets
- 目标平台：Windows 10+
- 加密：AES-256-GCM + PBKDF2-HMAC-SHA256 600000 iter（与 Android 端逐字对齐）

## 构建

依赖工具链：

| 工具 | 版本 | 配置方式 |
|---|---|---|
| Qt | 6.11.1 mingw_64 | 设置 `QT_ROOT` |
| MinGW | GCC 13.1.0 (x86_64-posix-seh) | 设置 `MINGW_ROOT` |
| Ninja | 1.12.1 | 加入系统 `PATH` |
| CMake | 3.24+ | 系统 PATH |
| vcpkg | latest | 设置 `VCPKG_ROOT` |

`QT_ROOT` 指向 Qt 的 MinGW kit 根目录，`MINGW_ROOT` 指向 MinGW 根目录。代理等本机配置请通过系统环境变量或忽略的 `CMakeUserPresets.json` 设置，不要写入共享 preset。

配置：

```bash
cmake --preset mingw-x64-debug
cmake --build --preset mingw-x64-debug
ctest --preset mingw-x64-debug
```

## Google Drive 同步

1. 从 Google Cloud Console 创建 Desktop 类型 OAuth Client
2. 配置前设置 `GOOGLE_DESKTOP_CLIENT_ID` 和 `GOOGLE_DESKTOP_CLIENT_SECRET` 环境变量，或通过 `-D` 传入同名 CMake 缓存变量
3. CMake 配置阶段会通过 `configure_file` 注入到 `google_oauth_config.h`；不要把真实凭据写入仓库文件

## SQLite

仓库包含构建所需的 `sqlite3.c` 和 `sqlite3.h` amalgamation 源码；更新方式见 `third_party/sqlite3/README.md`。

## 状态

- Stage 0（骨架）：完成
- Stage 1（crypto + model）：进行中
- Stage 2-7：待启动

跨端互通设计文档见 `docs/android_interop.md`。
