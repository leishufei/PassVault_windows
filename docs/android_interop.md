# Android 跨端互通设计

Windows 端与 Android 端通过 Google Drive 上的 `PassVault_Cloud_Backup.json` 交换加密数据。本文档记录必须逐字对齐的协议锁点。

## 1. 加密参数

| 项 | 值 |
|---|---|
| KDF | PBKDF2WithHmacSHA256 |
| 迭代次数 | 600000 |
| 密钥长度 | 32 字节（256 位） |
| Salt 长度 | 16 字节，随机 |
| IV 长度 | 12 字节，随机 |
| AES 模式 | AES-256-GCM，Tag 128 位 |
| 密文字节序 | `ciphertext ‖ tag(16B)`（Java Cipher 输出行为） |
| Base64 编码 | 标准表，无换行（Android `Base64.NO_WRAP`） |

Windows 端 OpenSSL EVP API 需手动 `EVP_CTRL_GCM_GET_TAG` 拼接 tag。

## 2. CloudBackupFormat JSON

（TODO：Android 生成样本 + Windows 解密测试向量）

## 3. SyncPayloadV2 JSON

（TODO：字段清单与 v1 兼容策略）

## 4. Google Drive 存储

（TODO：文件夹路径、乐观锁、scope）

## 5. 合并算法（mergeByUuid）

（TODO：UUID 相同时 updatedAt 优先级、tombstone 30 天、分类去重规则）
