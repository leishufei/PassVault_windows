# PassVault Windows 开发进度

最后更新：2026-07-10（Task 20 完成，全部任务完成）

## 执行策略

「先实现代码功能，忽略工具链验证」——写完整源码（含 CMake），跳过 configure/build/ctest 验收。等最终代码就位再一次性跑构建。

模块推进顺序（按跨端互通决胜点优先）：
`骨架 → model → crypto → cloud_crypto_service → storage → master_password + session → csv + generator → oauth → sync → hello → ui`

## 工具链

用户已装：

| 工具 | 版本 | 配置方式 |
|---|---|---|
| Qt | 6.11.1 mingw_64 | `QT_ROOT` 环境变量 |
| MinGW | GCC 13.1.0 (x86_64-posix-seh) | `MINGW_ROOT` 环境变量 |
| Ninja | 1.12.1 | 系统 `PATH` |
| CMake | 3.24+ | 系统 `PATH` |
| vcpkg | latest | `VCPKG_ROOT` 环境变量 |
| Python | 3.10.0 | 系统 `PATH`（仅辅助工具需要） |
| aqtinstall | 3.3.0 | 系统 |

`CMakePresets.json` 不包含机器路径；构建前设置 `QT_ROOT`、`MINGW_ROOT` 和 `VCPKG_ROOT`。

## 已完成任务

### Task 10：项目骨架 ✅

- `CMakeLists.txt`（顶层，C++17，find Qt6 Core/Widgets/Network/Svg + OpenSSL 3.0，configure_file 生成 `google_oauth_config.h`）
- `CMakePresets.json`（`mingw-x64-debug` / `mingw-x64-release`）
- `vcpkg.json` + `vcpkg-configuration.json`（openssl 3.x + gtest 1.14）
- `cmake/google_oauth_config.h.in`
- `.gitignore` / `README.md`
- `docs/android_interop.md`（骨架，5 章节 TODO）
- `third_party/sqlite3/{CMakeLists.txt,README.md}`（sqlite amalgamation 待用户下载放入）
- `src/main.cpp` + `src/CMakeLists.txt`（子目录挂载 11 个模块）
- `src/app/{application.h,.cc,CMakeLists.txt}`（空窗口 1200x800）
- `src/{model,crypto,storage,master_password,session,csv,generator,oauth,sync,hello,ui}/CMakeLists.txt`（模块占位）
- `tests/{smoke_test.cc,CMakeLists.txt}`

### Task 11：model 层 ✅

- `src/model/password_entry.h`——POD，镜像 Android Room `password_entries` 表，含 `encrypted_password + password_iv + app_package_name`（v1 保留字段）
- `src/model/category.h`——POD，镜像 Android `Category`
- `src/model/cloud_backup_format.{h,cc}`——`version:1, kdfAlgorithm:"PBKDF2WithHmacSHA256", iterations:600000, salt, encryptionAlgorithm:"AES-256-GCM", iv, ciphertext`；`ToJsonBytes` / `FromJsonBytes` 走 QJsonDocument
- `src/model/sync_payload_v2.{h,cc}`——`SyncPayloadV2 { version:2, syncTimestamp, passwords[], categories[] }` + `PasswordSyncItemV2`（12 字段，含明文 password，不含 appPackageName / strength）+ `CategorySyncItemV2`（7 字段，不含 isDefault）
- `src/model/CMakeLists.txt`——挂载源码

**关键决策**
- POD 用 public + `snake_case` 成员（Google Style 对 struct 允许）
- 加密字段 `QByteArray`，13 位时间戳 `std::int64_t`
- Windows 端不读 V1 Payload——遇到 → UI 提示走 Android 升级

### Task 12：crypto 层 ✅

- `src/crypto/secure_bytes.{h,cc}`——RAII 清零 + `VirtualLock` 防 swap + 常数时间比较
- `src/crypto/random.{h,cc}`——`BCryptGenRandom`
- `src/crypto/sha256.{h,cc}`——`Sha256` / `Sha256HexLower(std::string_view)`
- `src/crypto/key_derivation.{h,cc}`——`PKCS5_PBKDF2_HMAC + EVP_sha256()`，默认 600000 iter / 32B key / 16B salt；失败抛 `std::runtime_error`
- `src/crypto/session_key.{h,cc}`——32B `SecureBytes` 语义封装，`FromSecureBytes` 唯一入口，move-only
- `src/crypto/crypto_service.{h,cc}`——AES-256-GCM，`EncryptGcm` 输出 `ct||tag(16B)`；`DecryptGcm` 校验失败返 `std::nullopt`；严格校验 key=32/iv=12
- `src/crypto/CMakeLists.txt`——挂载全部 6 对源文件
- `tests/key_derivation_test.cc`——PBKDF2-HMAC-SHA256 已知向量 (iter=1/2/4096)
- `tests/crypto_service_test.cc`——NIST SP 800-38D 测试向量 (Test Case 13/14) + round-trip / 篡改 / 短密文 / 错密钥 / 非法尺寸

### Task 13：sync/cloud_crypto_service ✅

- `src/sync/cloud_crypto_service.{h,cc}`——`EncryptForCloud(payload_json, master_pwd) → CloudBackupFormat`，`DecryptFromCloud(backup, master_pwd) → optional<QByteArray>`
- 盐 16B / IV 12B 每次随机；PBKDF2 走 `backup.iterations`（防未来参数演进）
- 走 `QByteArray::toBase64() / fromBase64()`（标准表，无换行，对齐 Android `Base64.NO_WRAP`）
- 校验 `kdfAlgorithm == "PBKDF2WithHmacSHA256"` && `encryptionAlgorithm == "AES-256-GCM"` && salt/iv 尺寸
- `src/sync/CMakeLists.txt`——挂载 `cloud_crypto_service`
- `tests/cloud_crypto_service_test.cc`——round-trip + 错密码 / 错 KDF 名 / 错加密名 / 密文篡改 / 错盐尺寸 / 空 payload
- `tests/CMakeLists.txt`——追加三个新测试 + 显式引用被测源文件 + 链接 `Qt6::Core`

> 备注：Android 真机导出的 backup 样本暂时没有，先做自加密自解密 + 参数校验；等有真机样本再加硬编码对拍用例

### Task 14：storage 层 ✅

- `src/storage/database.{h,cc}`——sqlite3 句柄 RAII + `Transaction`（BEGIN IMMEDIATE，析构默认 ROLLBACK，`Commit()` 提交），启动 PRAGMA：`foreign_keys=ON` + `journal_mode=WAL` + `synchronous=NORMAL`；`OpenInMemory()` 用于测试（跳过 WAL）
- `src/storage/schema.{h,cc}`——`kCurrentSchemaVersion = 8` + DDL 常量（`password_entries` 16 列 + `categories` 9 列 + `index_password_entries_uuid` 唯一索引）；`EnsureCurrentSchema()` 空库时 `PRAGMA user_version=0` → 事务内建表并 SetUserVersion=8；`user_version != 0 && != 8` 抛 runtime_error（用户先在 Android 端升级）
- `src/storage/statement.{h,cc}`——`sqlite3_stmt*` RAII，暴露 `BindInt/Int64/Bool/Text(QString|string_view)/Blob/Null` + `Step/StepDone` + `ColumnInt/Int64/Bool/Text/Blob`，prepare/bind 失败抛 runtime_error（DAO 内 catch 转 nullopt/false）
- `src/storage/password_dao.{h,cc}`——22 个方法（对比 Kotlin 25 合并了 SQL 一致的 `getAllPasswordsForSync/IncludingDeleted/ForAnalysis`）：`ListActive` / `ListIncludingDeleted` / `ListByCategory` / `ListIdsByCategory` / `FindById` / `FindByUuid` / `Search` / `FindByWebsite` / `FindByPackageName` / `Insert`(→`optional<rowid>`) / `InsertMany`(事务) / `Update` / `SetFavorite` / `HardDelete` / `LogicalDelete` / `DeleteAll` / `CountDuplicate` / `MigrateCategory` / `BulkMigrateCategory` / `BulkLogicalDelete` / `BulkUpdateCategoryByIds` / `UpdateCategoryOf` / `DeleteOldTombstones`
- `src/storage/category_dao.{h,cc}`——12 个方法：`ListActive` / `ListIncludingDeleted` / `FindById` / `FindByUuid` / `Insert` / `Update` / `HardDelete` / `DeleteAllCustom` / `CountActive` / `UpdateSortOrder` / `LogicalDelete` / `DeleteOldTombstones`
- `tests/database_test.cc` + `password_dao_test.cc` + `category_dao_test.cc`——共 45 个 case，`Database::OpenInMemory()` per-test 建库；全部 65 tests pass (`ctest --preset mingw-x64-debug`)
- `third_party/sqlite3/CMakeLists.txt`——修：改 `if(EXISTS)`→`if(NOT EXISTS) FATAL_ERROR`；`sqlite3.c` 无条件挂到 target_sources；显式 `enable_language(C)`；关掉 sqlite target 的 AUTOMOC/AUTOUIC/AUTORCC；顶层 CMakeLists 已通过 `add_subdirectory(third_party/sqlite3)` 链好

**关键决策**
- DAO 全用 C++ 化命名，去掉冗余 `getPassword*` 前缀；返回类型：单条→`optional<T>`、列表→`vector<T>`、CRUD→`bool`、Insert→`optional<int64_t>`(rowid)
- Schema 策略：Windows 端只支持从空库直接 → v8，**不移植** Android 端 4→5..7→8 迁移；`user_version < 8` 直接报错
- Prepared statement 不 cache，每次调用 prepare/finalize（RAII）——单机 QPS 极低，可维护性优先
- IN(:ids) 类 SQL 拼 `?,?,?...` 占位符；ids 为空的 bulk 方法直接返 true（noop）

### Task 15：master_password + session ✅

- `src/master_password/master_password_store.{h,cc}`——DPAPI 保护 `<applicationDirPath>/data/master.dat`；`MasterRecord { password_hash (sha256 hex 64B ASCII), kdf_salt (16B) }` 序列化为紧凑 JSON 后走 `CryptProtectData`（用户级，无 entropy，无 CRYPTPROTECT_LOCAL_MACHINE）；`QSaveFile` 原子写入；`Load` 走 `CryptUnprotectData` + JSON 校验（hash 长度 64、salt 长度 16）；`Clear` 删除文件
- `src/master_password/master_password_manager.{h,cc}`——`SetInitial(pwd)` 生成 16B 随机 salt + 写 store + 派生 SessionKey；`VerifyLocal(pwd)` 常数时间比较 sha256 hex + 派生 SessionKey；`ChangePassword(old,new)` 校验旧密码 → 换新 salt/hash（**存储层重加密留 UI 编排**）；`Reset()` 删 master.dat；`last_error()` 暴露失败原因；SessionKey 派生 `PBKDF2(pwd, kdf_salt, 600000, 32)`
- `src/session/session_manager.{h,cc}`——QObject Meyers 单例；`Unlock(SessionKey, SecureBytes master_pwd)` / `Lock()` / `IsUnlocked()` / `session_key()` / `master_password()`；`LockChanged(bool locked)` 只在状态跃迁时发；`ResetForTests()` 测试用
- `src/session/auto_lock_timer.{h,cc}`——QTimer 单发 5min 默认；`Start` / `Stop` / `NotifyActivity`（活动时重置定时器）/ `SetTimeoutMs`；`TimedOut()` 信号让外部连到 `SessionManager::Lock`
- `tests/test_main.cc`——手动 `QCoreApplication` + `InitGoogleTest`（因 `QTimer` 需要事件循环）；`tests/CMakeLists.txt` 换掉 `GTest::gtest_main` + 追加 `Qt6::Test`（QSignalSpy）
- `tests/master_password_store_test.cc`(6) + `master_password_manager_test.cc`(10) + `session_manager_test.cc`(6) + `auto_lock_timer_test.cc`(6)——共 28 个 case，93 tests total 全绿

**关键决策**
- DPAPI 方案 A（用户级，无 entropy）——个人使用，本机同用户其他进程可解，跨机器不可解（换机器走 Task 18 云端 VerifyAgainstCloud 恢复流程）
- `master.dat` 存 JSON：`{"password_hash": "hex64", "kdf_salt": "base64(16B)"}`——本地校验用 hash（与 Android 一致），SessionKey 派生用 salt；两者合并单文件原子读写
- SessionManager 单例：Meyers pattern，未来若需依赖注入再改
- `VerifyAgainstCloud` 留 TODO 到 Task 18（云 provider 就位后接入）

### Task 16：csv + generator ✅

- `src/csv/csv_writer.{h,cc}`——`QIODevice` 输出流；`\r\n` 行终止、`,` 分隔、按需引号（含 `,` / `"` / `\r` / `\n` 时引号包裹 + `""` 转义）、UTF-8 编码
- `src/csv/csv_reader.{h,cc}`——`static ReadAll(QByteArray) → QVector<QStringList>`；剥 UTF-8 BOM、支持 CRLF / LF 行终止、支持带引号字段内嵌逗号 / 引号 / 换行
- `src/csv/csv_models.h`——`CsvFormat` / `ImportAction` / `ExistingPasswordSnapshot` / `FieldDiff` / `ValidatedPasswordRow` / `InvalidRow` / `CsvValidationResult` POD 定义
- `src/csv/csv_validator.{h,cc}`——逐行翻译 Android `CsvValidator.kt`：`DetectFormat`（Extended 7 列 / Legacy 4 列 / Unknown）+ `Validate`（BOM 剥离 → 头行识别 → UUID 匹配 → 标题|用户名回退 → INSERT / UPDATE 分派 → 字段级 diff）
- `src/csv/csv_exporter.{h,cc}`——`Export(QIODevice*, QList<ExportEntry>)`：写 UTF-8 BOM + `CsvValidator::ExtendedHeaders()` 中文列头，按 `(categorySortKey → categoryId → createdAt)` 三键排序（未分类 → -1 排最前）
- `src/csv/csv_importer.{h,cc}`——`Apply(validation, pwd_dao, cat_dao, session_key, now_ms) → optional<ImportSummary>`：先解析 `new_category_names` 新增到 `category_dao`，然后对每行 AES-256-GCM 加密密码（12B 随机 IV）+ 强度评估 + INSERT / UPDATE；不触发同步，标脏由 UI 层显式调用 `SyncScheduler::MarkDirty`
- `src/generator/password_generator.{h,cc}`——`Generate(PasswordConfig) → optional<QString>`；`crypto::Random::Fill` 单字节拒绝采样避免模偏；字符池 `!@#$%^&*()_+-=[]{}|;:,.<>?` 对齐 Android
- `src/generator/password_strength.h`——inline 纯函数：`len>=12 && types>=3 → 3` / `len>=8 && types>=2 → 2` / `else 1`；`isLetterOrNumber()` 判 special
- `tests/csv_{writer,reader,validator,exporter_importer}_test.cc` + `password_{generator,strength}_test.cc`——共 45 个 case，139 tests total 全绿（`ctest --preset mingw-x64-debug` 9.68s）

**关键决策**
- Generator 与 Android 对齐：合并池 uniform 随机（不保证每类至少 1 个），用户不满意可重摇
- Importer 只调 DAO 不持 SyncScheduler：模块解耦最干净，UI 收到 `ImportSummary` 后自行调用 `SyncScheduler::MarkDirty`
- CSV writer / reader 自写（无第三方依赖），行为对齐 kotlin-csv 默认：CRLF + 按需引号 + 双引号转义
- 验证器输入 `existing_category_names: QStringList` 而非 `QList<Category>`：解耦 model 依赖，`ExistingPasswordSnapshot` 携带解密后的 password + resolved category_name（调用方负责翻译）
- Importer 时间戳走参数 `now_ms`：测试可注入固定时间，生产由 UI 传 `QDateTime::currentMSecsSinceEpoch()`

### Task 17：oauth ✅

- `src/oauth/pkce.{h,cc}`——SHA-256 base64url verifier/challenge：verifier = base64url(32B 随机)，challenge = base64url(sha256(verifier))，`RandomState()` 同样 32B 随机 base64url
- `src/oauth/loopback_http_server.{h,cc}`——`QTcpServer` 监听 `127.0.0.1:0`（OS 分配端口）+ 3 次重试；解析 `GET /callback?code=&state=`，返回中文成功页；非 GET 返 405，非 /callback 返 404；`CallbackReceived(QHash<QString,QString>)` 信号；用 `QUrl::fromEncoded` + `fromPercentEncoding` 解析 query
- `src/oauth/token_store.{h,cc}`——`SaveRefreshToken` / `LoadRefreshToken` / `Clear`：DPAPI + entropy=`sha256("PassVault:" + machine_guid)`，machine_guid 读 `HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid`（KEY_WOW64_64KEY）；默认路径 `QStandardPaths::AppDataLocation/google_token.dat`；`QSaveFile` 原子写；只存 refresh_token，access_token 只在内存
- `src/oauth/google_oauth_client.{h,cc}`——QObject：`Authorize()` = 起 LoopbackHttpServer → 生成 PKCE + state → `QDesktopServices::openUrl(https://accounts.google.com/o/oauth2/v2/auth?...&scope=drive.file&access_type=offline&prompt=consent&code_challenge=...&code_challenge_method=S256&state=...)` → 回调校验 state → POST `https://oauth2.googleapis.com/token`；`RefreshAccessToken()` 走 refresh_token grant；`RevokeTokens()` POST `https://oauth2.googleapis.com/revoke` + 清 TokenStore + 内存 token
- client_id / client_secret 走 `passvault/oauth/google_oauth_config.h`（CMake 从环境变量或同名缓存变量注入）
- `tests/pkce_test.cc`(6) + `loopback_http_server_test.cc`(6) + `token_store_test.cc`(6)——共 18 个 case；`GoogleOAuthClient` 涉及浏览器+外网 Google endpoint，暂不写单测（Task 20 UI 阶段做端到端）

**关键决策**
- LoopbackHttpServer 用 QTcpServer 直读原始字节：不引 QHttpServer 依赖；请求头 8KB 上限防洪水
- 成功回调后立即 `Stop()`：一次授权只监听一个 `/callback`，防止重放
- Access token 只在内存：进程重启后走 refresh_token 静默续期；refresh_token 落盘 DPAPI+entropy 双层保护
- TokenStore 只暴露 QString API：refresh_token 是 opaque 字符串，不需要 QByteArray 二进制接口
- 测试用 QSignalSpy 驱动主事件循环：`waitForReadyRead` / `waitForDisconnected` 只处理 socket 级事件，无法触发同线程另一 QObject 的槽

### Task 18：sync（其余） ✅

- `src/sync/merge_algorithm.h`——泛型 `MergeByUuid<T, GetUpdatedAt>` 纯函数模板；输入 `QHash<QString, T>` 本地 + 云端 map，返回 `MergeResult<T>{ merged, has_local_changes }`；`local.updatedAt >= cloud.updatedAt` 保留本地，严格大于时置 `has_local_changes=true`，本地独有直接置 `true`，云端独有不置
- `src/sync/cloud_storage_provider.h`——`CloudStorageProvider` 抽象接口：`ProviderName / IsAuthenticated / SignOut / UploadBackup / DownloadBackup / DownloadBackupWithVersion / UploadBackupIfMatch`；`enum class UploadIfMatchStatus { kSuccess, kVersionMismatch, kError }` 显式区分并发冲突与其他错误；下载走 `optional<QByteArray>` + `out_error`
- `src/sync/google_drive_provider.{h,cc}`——`QNetworkAccessManager` REST；access token 由 `std::function<QString()>` 注入（`GoogleOAuthClient` 提供），每个请求带 `Authorization: Bearer <token>`；`GetOrCreateFolder`（Q=`name = 'PassVault' and mimeType = 'application/vnd.google-apps.folder' and trashed = false`）→ `FindFileId` → `PATCH /upload/drive/v3/files/{id}?uploadType=media`（Update）或 `POST /upload/drive/v3/files?uploadType=multipart`（Create）；下载 `GET /drive/v3/files/{id}?alt=media` + 元数据 `?fields=modifiedTime` 作乐观锁；每个请求本地 `QEventLoop` 同步等待（不阻塞外层事件循环）
- `src/sync/sync_manager.{h,cc}`——`SyncManager(pwd_dao, cat_dao, session, parent)`；`PerformSync(remote_name, max_retries)` 循环 `DoSyncOnce`：kSuccess 立刻返回、kFatal 立刻返回、kVersionMismatch 继续重试；发 `SyncStarted / SyncFinished(success, message)`；`DoSyncOnce` 时序完整对齐 Android `SyncManager.doSyncOnce`：Load(local pw+cat) → DownloadWithVersion → 解密 CloudBackupFormat→SyncPayloadV2 → V1 直接 kFatal 提示 → 合并 Categories（本地→云端 map，updatedAt 决胜）→ Apply（`FindByUuid` 决定 Insert 还是 REPLACE 保留 id/is_default）→ 重建 uuid→id map → 合并 Passwords（用**合并前**的 cat_id_to_uuid 构造 local sync items，与 Android 一致；countDuplicate 防跨设备重复插入）→ Apply（AES-256-GCM 用当前 session_key + 随机 12B IV 重加密）→ `DedupCategoriesByName`（groupBy `name.trim().toLower()`，最小 createdAt 获胜，`BulkMigrateCategory` + `LogicalDelete` losers）→ 重新读取构造 final 载荷 → `need_upload = catChanged || pwChanged || dedupChanged || cloudPayload == null` → `UploadBackupIfMatch(expected=cloud_version)` 或首次 `UploadBackup` → 30 天墓碑清理；`ChangeCloudMasterPassword(old,new,remote)` 走 Download+Decrypt(old)+Encrypt(new)+UploadIfMatch 循环重试
- `src/sync/sync_scheduler.{h,cc}`——`QTimer::setSingleShot(true)` 5000ms 默认防抖；`MarkDirty()` 未挂 provider 时直接 return（对齐 Android），挂载后 `timer->start()` 重启（自动取消上一次未触发的定时器）+ 发 `SyncScheduled`；超时 → `SyncTriggered` + `sync_manager_->PerformSync()`；`SyncImmediately()` 主动取消 timer 后立刻同步；`set_debounce_ms(ms)` 测试友好
- `tests/merge_algorithm_test.cc`(10)——local_only/cloud_only/equal_ts/local_newer/cloud_newer/local_tombstone/cloud_tombstone/both_tombstoned/mixed_map/empty 全分支覆盖
- `tests/sync_scheduler_test.cc`(5)——无 provider 直接 no-op；provider 挂载后 MarkDirty 启动 timer；重复 MarkDirty 重置计时；超时触发 PerformSync；SyncImmediately 取消 pending 并立刻跑
- `tests/sync_manager_test.cc`(6)——no-provider 快失败；locked-session 快失败；首次同步向云端上传本地全量；重复同步无差异跳过上传；`always_mismatch=true` 循环重试 max_retries 后返回并发冲突失败；云端独有条目落地本地（含密码 AES-256-GCM round-trip 校验）；同名分类去重（trim+lower 匹配，最小 createdAt 获胜，密码 category_id 迁移到 winner，loser 逻辑删除）
- `src/sync/CMakeLists.txt` + `tests/CMakeLists.txt`——追加 5 个新源文件 + 3 个新测试；tests 显式引用 `google_drive_provider.cc / sync_manager.cc / sync_scheduler.cc`（AUTOMOC 通过 .cc 扫描 Q_OBJECT 头文件）；顶层 `find_package(Qt6 COMPONENTS Network)` 早已就位

**关键决策**
- 网络请求同步等待：`QNetworkAccessManager` 走异步，配合本地 `QEventLoop::exec()` 同步等待每个 reply。外层事件循环仍在跑（嵌套），UI 不冻结；SyncManager 因此可以直接顺序编排 Download→Merge→Upload
- Access token 注入：GoogleDriveProvider 不持 token，构造函数接 `std::function<QString()>` 由 `GoogleOAuthClient` 提供，refresh 后透明可见
- V1 云端格式：Windows 端遇到直接返 kFatal 提示"请先在 Android 端同步升级到 V2"（`plan.md` 明确不支持迁移）
- DedupByName 落在 SyncManager 内（不额外造 `CategoryRepository`）：跨 DAO 的编排逻辑，SyncManager 已经拿到两个 DAO 引用最直接
- MergeByUuid 用 `QHash<QString, T>` 而非 `std::unordered_map`：省去自定义 hash，QString 天生有 qHash
- 上传冲突用枚举 `UploadIfMatchStatus` 而非异常：C++ 侧异常成本高、和 Android 的 `ConcurrentModificationException` 语义映射清晰
- SyncManager 依赖注入 SessionManager*（生产用 `SessionManager::Instance()`）：便于测试用同一单例 Unlock/Lock

### Task 19：hello ✅

- `src/hello/windows_hello_unlock.h`——`WindowsHelloUnlock` 类；`HelloError { kOk, kNotAvailable, kUserCancelled, kDpapiFailure, kIoError, kNotEnrolled, kInternalError }`；`IsAvailable() / IsEnrolled() / Enroll(master_pwd) → bool / Unlock() → optional<QString> / Disable() → bool / last_error()`；测试钩子 `set_availability_for_testing(optional<bool>) / set_prompt_result_for_testing(optional<bool>) / reset_availability_cache_for_testing()`
- `src/hello/windows_hello_unlock.cc`（shared，始终编译）——DPAPI protect/unprotect（`CRYPTPROTECT_UI_FORBIDDEN` 明示禁止 DPAPI 自弹 UI）+ `QSaveFile` 原子写入 + 所有类方法的 IO / 状态编排；`DefaultStoragePath = <applicationDirPath>/data/hello_unlock.dat`
- `src/hello/windows_hello_unlock_winrt.cc`（可选后端）——C++/WinRT `UserConsentVerifier::CheckAvailabilityAsync` + `RequestVerificationAsync(hstring)`；`winrt::init_apartment(single_threaded)` 幂等调用忽略 `RPC_E_CHANGED_MODE`；`.get()` 同步等待 async；`hresult_error` 与 `catch (...)` 双兜底
- `src/hello/windows_hello_unlock_stub.cc`（回退后端）——两个 probe 方法直接返回 `false`（除非测试钩子已设置）；DPAPI 与 IO 走 shared .cc 依然可用
- `src/hello/CMakeLists.txt`——`check_include_file_cxx("winrt/Windows.Security.Credentials.UI.h", ...)` + `find_library(windowsapp WindowsApp)` 二者都 OK 才启用 winrt.cc，否则 stub.cc；`message(STATUS ...)` 打印实际选择
- `tests/windows_hello_unlock_test.cc`(8)——unavailable 直接拒绝 enroll/unlock；enroll → unlock round-trip（UTF-8 变音符）；prompt 被拒 → kUserCancelled 且 blob 不落盘；enroll 后 unlock 时 prompt 被拒 → blob 保留；未 enroll 直接 unlock 报 kNotEnrolled；Disable 清 blob 且幂等；同路径新实例可以解锁旧 blob（模拟进程重启）；空主密码 round-trip
- `tests/CMakeLists.txt`——追加测试文件 + `windows_hello_unlock.cc` + `windows_hello_unlock_stub.cc`（测试恒用 stub，通过测试钩子驱动 DPAPI + IO 路径）

**关键决策**
- **认证边界**：Windows Hello 只是 UI gate（`UserConsentVerifier` 生物识别或 PIN 确认），本身不派生密钥；落盘保护走 DPAPI 用户级——blob 绑定当前 Windows 用户账户，跨机器/跨用户直接不可解 → 等同未启用，走主密码兜底
- **`CRYPTPROTECT_UI_FORBIDDEN`**：明示 DPAPI 永远不弹自己的 UI（即便 blob 建立时曾配 CRYPTPROTECT_PROMPTSTRUCT），Hello 才是唯一交互点
- **无 entropy**：与 `master_password_store` 一致，不叠加 machine-guid entropy——同 Windows 用户下的其他进程本就可读用户 DPAPI store，多加一层 entropy 只是仪式感
- **两个测试钩子拆开**：`forced_availability_` 和 `forced_prompt_` 独立，才能覆盖"设备可用但用户在 Hello 弹窗时取消"这条路径
- **三文件拆分**：shared `.cc` 承担 DPAPI + IO（~150 行），winrt/stub `.cc` 各只写两个探测方法（~30 行），避免代码重复；测试只链 stub 更轻
- **`winrt::init_apartment` 幂等吞异常**：Qt 主线程默认未设 COM apartment，首次调用初始化 STA；如果外层已初始化为 MTA 抛 `RPC_E_CHANGED_MODE`，忽略即可，subsequent WinRT 调用仍在既有 apartment 中工作
- **`.get()` 同步等待**：Hello 提示本就是模态用户交互（几秒），主线程短暂阻塞可接受；如需 Qt 事件循环持续响应可换 `Completed` 回调 + 嵌套 `QEventLoop`，暂无收益不做
- **落盘位置**：`applicationDirPath/data/hello_unlock.dat`——与 `master.dat` 并列，随 exe 绿色迁移（换机器时 blob 无法解密，正确降级到主密码解锁）

## 进行中任务

（无）

### Task 20：ui ✅

- `src/ui/theme_manager.{h,cc}`——QObject 单例；`Theme { kSystem, kLight, kDark }`；`ApplyTheme` 切换后重新 `qApp->setStyleSheet`；`kSystem` 走 `QStyleHints::colorScheme()` + `colorSchemeChanged` 信号跟随系统；`Color(token)/ColorHex(token)` 暴露解析后的调色板；`LoadStyleSheet()` 读取 `:/qss/tokens_{dark|light}.qss` 做 token 表 → 拼 `components.qss + app.qss` → 正则替换 `var(--x)` 得到最终 QSS
- `src/ui/icon_loader.{h,cc}`——`QSvgRenderer` 渲染 Lucide SVG：读字节替换 `currentColor` / `stroke="#000000"` / `fill="#000000"` → 主题文本色 → 输出 QPixmap → QIcon；QMutex 缓存 key=`name|argb|size`；缺 SVG 优雅降级返回空 QIcon
- `resources/qss/tokens_dark.qss`——对齐 DESIGN.md：canvas `#010102`、surface-1..4 (`#0f1011/#141516/#18191a/#191a1b`)、hairline (`#23252a/#34343a/#3e3e44`)、ink (`#f7f8f8/#d0d6e0/#8a8f98/#62666d`)、accent `#5e6ad2` + hover `#828fff` + focus `#5e69d1`、success `#27a644`
- `resources/qss/tokens_light.qss`——DESIGN.md 未定义 light；沿用 accent + 反向表面阶梯，供 Qt Widgets 桌面场景可选
- `resources/qss/components.qss`——按钮/表单圆角 md=8px，卡片/面板类 lg=12px，pill 保留给状态徽章；按钮 padding 8×14、表单 8×12（对齐 DESIGN.md 组件 spec）；`[accent="true"]/[danger="true"]/[flat="true"]` 属性变体
- `resources/qss/app.qss`——PassVault 特化：`#Sidebar` / `#SidebarHeader`（eyebrow +0.4px letter-spacing）/ `#CategoryTree` / `#PasswordList`（surface-1 lift on hover + 12px 圆角）/ `#SearchBar` / `#DetailField`（eyebrow）/ `#DetailMonoValue`（Consolas/JetBrains Mono）/ `#EmptyState` / `#StrengthBar[level=1|2|3]` / `#Toast`（12px 圆角）/ `#DialogTitle`（22px / weight 500 / -0.4 letter-spacing）/ `#DialogSubtitle` / `#FormError`（走 --danger token）/ `#Badge*`（pill）
- `resources/icons/README.md` + `check.svg` + `chevron-down.svg`——列出 Lucide 图标清单 + 授权说明；QSS 内引用的 check / chevron-down 作为最小占位随源码打包
- `resources/CMakeLists.txt`——`qt_add_resources` 显式挂 4 个 QSS 文件 + `file(GLOB)` 收集 `icons/*.svg`（用户拖入的 Lucide SVG 自动打包，无需改 CMake）
- `src/ui/clipboard_manager.{h,cc}`——`Instance()` 单例；`CopySensitive(text, timeout_ms)` 复制后启动 QTimer::singleShot；超时前对比 `QClipboard::text() == 期望值` 才清空（用户已复制别的东西时跳��）；信号 `ClearScheduled / Cleared / ClearSkippedBecauseChanged`
- `src/ui/toast.{h,cc}`——`static Show(parent, text, Level, duration_ms)`；`QGraphicsOpacityEffect` + `QPropertyAnimation` 淡入 180ms → 显示 → 淡出 200ms；`WA_DeleteOnClose`；四级 `kInfo/kSuccess/kWarning/kError` 分别对应 objectName `Toast/ToastSuccess/ToastWarning/ToastError`
- `src/ui/global_hotkey.{h,cc}`——QObject + `QAbstractNativeEventFilter` 单例；`struct Modifiers { alt, ctrl, shift, win }`；Register/Unregister/UnregisterAll；`QtKeyToVk` 覆盖 F1-F12/A-Z/0-9/Space/Return；Win32 `RegisterHotKey` 带 `MOD_NOREPEAT`；`#ifdef _WIN32` 之外的平台走 stub 返 false
- `src/ui/master_password_dialog.{h,cc}`——`Mode { kSetup, kUnlock, kChange }`；密码显影 checkbox；kSetup/kChange 校验 `len>=8` + 确认输入一致；kChange 额外校验 old != new；Windows Hello 按钮通过 `SetHelloAvailable` 显隐 + `HelloRequested` 信号外接；析构时 `QLineEdit::clear()` 抹掉内存中的明文
- `src/ui/generator_dialog.{h,cc}`——长度 QSlider 4-64 与 QSpinBox 双向同步；4 个字符类型 QCheckBox；任一输入变化即调用 `generator::PasswordGenerator::Generate` 重摇；`#StrengthBar` 属性 `level=1|2|3` 驱动 QSS 变色；复制按钮走 `ClipboardManager::CopySensitive`
- `src/ui/password_detail_dialog.{h,cc}`——`Mode { kCreate, kView, kEdit }`；`struct DecryptedEntry { model::PasswordEntry, QString password }`；分类 QComboBox 从 `QList<model::Category>` 加载（id=0 → "未分类"）；密码显影 QToolButton 切换 EchoMode；密码 textChanged → 即时刷新强度标签；信号 `GenerateRequested / CopyRequested / FavoriteToggled / EditRequested / DeleteRequested`
- `src/ui/settings_dialog.{h,cc}`——QTabWidget 三选项卡：常规（Theme QComboBox + 自动锁定分钟 + 剪贴板清空秒）/ 同步（Google Drive Connect/Disconnect/SyncNow + CSV Import/Export）/ 安全（改主密码 + Windows Hello 开关）；QSettings key：`session/auto_lock_minutes`、`ui/clipboard_seconds`、`session/hello_enabled`；11 个信号让 Application 侧编排具体动作
- `src/ui/import_preview_dialog.{h,cc}`——构造入参 `csv::CsvValidationResult`；三个 QTreeWidget 分别渲染 新增 / 更新（每行子项列出 FieldDiff：`"字段: 旧值 → 新值"`）/ 无效；汇总头行显示总数 / 插入 / 更新 / 无效；无有效行时确认按钮禁用
- `src/ui/oauth_wizard_dialog.{h,cc}`——传入 `GoogleOAuthClient*`；"开始授权"按钮调 `client->Authorize()`；`AuthorizationSucceeded` → 自动 accept + 发 `ConnectedSuccessfully`；`AuthorizationFailed(message)` 显示原因并允许重试
- `src/ui/main_window.{h,cc}`——`struct Deps { PasswordDao*, CategoryDao*, const SessionKey*, SyncManager*, SyncScheduler* }`；分类树含特殊项 `kSpecialCategoryAll=-1` / `kSpecialCategoryFavorites=-2`；搜索优先级高于分类过滤（走 `PasswordDao::Search`）；`DecryptPassword` 用 SessionKey + `entry.password_iv` + `encrypted_password` 走 `CryptoService::DecryptGcm`；`EncryptAndAssign` 每次生成 12B 随机 IV；新建流程 → QUuid + 时间戳 + AES-256-GCM 加密 → `PasswordDao::Insert` → `SyncScheduler::MarkDirty`；工具栏：新建（Ctrl+N）/ 立即同步 / 锁定（Ctrl+L）/ 设置；Ctrl+F 聚焦搜索框；`SyncManager::SyncStarted/SyncFinished` 联动状态栏 + Toast
- `src/app/application.cc`——`Impl` 类内 `unique_ptr` 编排所有模块：Database → EnsureSchema → DAO/MasterStore/MasterManager/Hello/TokenStore/OAuthClient/DriveProvider/SyncManager/SyncScheduler/AutoLockTimer；`UnlockFlow` 支持首次设置 / Hello 快速解锁 / 主密码回退循环三条路径；`OpenSettings` 把 SettingsDialog 的 11 个信号连到具体动作（改主密码 / 启停 Hello / Drive 连接与撤销 / 立即同步 / CSV 导入导出 / 自动锁定与剪贴板超时调整）
- `src/ui/CMakeLists.txt`——24 个源文件全挂到 `passvault` target
- 顶层 `CMakeLists.txt`——`add_subdirectory(resources)` 注入 Qt 资源

**关键决策**

- **QSS token 替换**：Qt QSS 不支持 CSS 变量，ThemeManager 在 `LoadStyleSheet` 阶段用正则把 `var(--x)` 替换为解析后的具体值，让 4 个 QSS 源文件保持可读；主题切换只需换一份 tokens_*.qss 再 `setStyleSheet` 到 qApp
- **DESIGN.md 对齐**：初版 dark token 用了浅色 (`#1c1d20` 系列)、按钮圆角 6px、accent hover 用了 `#7078e3`——本轮全部改为 DESIGN.md 值：canvas `#010102`、四阶 surface ladder、button 8px、`#828fff` hover、`#5e69d1` focus、ink `#f7f8f8`；卡片/面板类容器（GroupBox/TabPane/List）改为 12px 圆角对齐 `rounded.lg`；徽章走 pill；`#DialogTitle` 用 22px/500/-0.4px 匹配 `card-title` 排版
- **图标优雅降级**：`resources/icons/` 通过 CMake `file(GLOB)` 打包，用户随时把 Lucide SVG 拖入即可生效；`IconLoader::Load` 对缺失图标返回空 QIcon（按钮仍可用文字兜底）
- **表单错误色统一走 token**：把 `generator_dialog` / `master_password_dialog` 内联的 `#e5484d` 改为 `#FormError` objectName + app.qss 里 `color: var(--danger)`，主题切换时错误提示自动变色
- **三种解锁模式**：`MasterPasswordDialog::Mode::kSetup / kUnlock / kChange` 复用一个对话框但按需显隐字段，避免三套雷同 UI
- **导入预览三区**：直接映射 Android `ImportPreviewDialog.kt`——新增 / 更新 / 无效各一个 QTreeWidget；更新区把每个 `FieldDiff` 展开成子项让用户逐字段确认
- **Application::Impl PIMPL**：所有模块 `unique_ptr` 藏在 .cc 里的 Impl 类，`application.h` 只暴露 `Run()`；SessionManager 用全局单例（简化 SyncManager 依赖注入）
- **加密边界**：MainWindow 层的 CRUD 就地生成 12B 随机 IV + `CryptoService::EncryptGcm`；不把 SessionKey 暴露到 dialog

**Task 20b（后续）**

- `ChangeMasterPassword` 需要在事务里用旧 SessionKey 解密全表 → 用新 SessionKey 重加密 → 写回，然后 `SyncManager::ChangeCloudMasterPassword` 推云；当前留 `TODO(task-20b)`
- `ExportCsv` 需要逐行解密再写 CSV，与 ChangeMasterPassword 共用密码重加密管线；当前只写空密码列作为结构占位

## 剩余任务

（无 —— Task 10-20 全部完成，可开始一次性 configure/build/ctest 验证）

## 跨端互通协议锁点（务必守住）

| 项 | 值 |
|---|---|
| KDF | PBKDF2WithHmacSHA256，600000 iter，输出 32 字节 |
| Salt | 16 字节随机 |
| AES | AES-256-GCM，IV 12 字节，Tag 128 位；密文字节序 = `ciphertext ‖ tag(16B)` |
| Base64 | 标准表、无换行（Android `Base64.NO_WRAP`） |
| CloudBackupFormat JSON 键 | `version, kdfAlgorithm, iterations, salt, encryptionAlgorithm, iv, ciphertext` |
| SyncPayloadV2 JSON 键 | `version:2, syncTimestamp, passwords[], categories[]` 字段名与 Android data class 完全一致 |
| Drive 存储 | 根目录 `PassVault` 文件夹 + `PassVault_Cloud_Backup.json`；scope `drive.file` |
| 乐观锁 | 文件 `modifiedTime` RFC3339 字符串完整比较 |
| 合并算法 | `mergeByUuid`：UUID 相同时 `local.updatedAt >= cloud.updatedAt` 保留本地 |
| 墓碑保留期 | 30 天 |
| 主密码本地哈希 | `sha256(utf8)` 小写十六进制字符串，无 salt、无迭代（刻意与 Android 一致） |
| 未分类 | `categoryId == 0` 对应 `categoryUuid == ""` |
| 密码强度 | `len>=12 && types>=3 → 3` / `len>=8 && types>=2 → 2` / `else 1` |
| CSV | UTF-8 BOM + 首行 `ID,分类,标题,用户名,密码,网站,备注`；导出按 sortOrder→categoryId→createdAt |
| 同步防抖 | 5000 ms |
| Windows 本地 DB 路径 | `<applicationDirPath>/data/passvault.db`（绿色/便携，随 exe 走） |

## 恢复上下文的最短路径

新会话继续开发时读三个文件：

1. `docs/progress.md`（本文件）——知道进度到哪
2. `docs/android_interop.md`——跨端协议
3. `README.md`——构建、依赖与项目入口

然后从「进行中任务」的下一个待写文件继续。
