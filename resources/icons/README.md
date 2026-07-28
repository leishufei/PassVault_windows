# PassVault 图标资源

本目录托管从 [Lucide Icons](https://lucide.dev/) 下载的 SVG 集合。

## 需要的图标清单

MainWindow / 工具栏 / 状态栏：
- `plus.svg` — 新增密码
- `search.svg` — 搜索框前缀
- `settings.svg` — 设置
- `refresh-cw.svg` — 立即同步
- `cloud.svg` — 云同步（默认状态）
- `cloud-off.svg` — 未启用云同步
- `lock.svg` — 手动锁定
- `unlock.svg` — 已解锁
- `menu.svg` — 汉堡按钮

分类树 / 侧边栏：
- `folder.svg` — 分类
- `star.svg` — 收藏

密码列表 / 详情：
- `copy.svg` — 复制
- `eye.svg` / `eye-off.svg` — 显示/隐藏密码
- `pencil.svg` — 编辑
- `trash-2.svg` — 删除
- `external-link.svg` — 打开网站
- `key.svg` — 密码占位

生成器 / 校验：
- `dices.svg` — 生成
- `check.svg` — 已选中 / 通过
- `x.svg` — 关闭 / 失败
- `alert-triangle.svg` — 警告

下拉 / 展开：
- `chevron-down.svg` — 下拉指示
- `chevron-right.svg` — 分类树折叠

主密码 / OAuth：
- `shield.svg` — 主密码
- `fingerprint.svg` — Windows Hello

## 下载方式

到 <https://lucide.dev/icons/> 逐个复制 SVG，或使用 CLI：

```bash
# 一次拉取全��（需要 Node）
npx @lucide/cli download --output-dir resources/icons
```

## 授权

Lucide 采用 ISC License，允许商业和个人使用。请保留一份 `LICENSE-lucide.txt`（可从 Lucide 仓库根目录拷贝）。

## 未落盘时的降级

`resources.qrc` 只登记本目录下**实际存在**的 svg 文件。缺失的图标在运行时会由 `IconLoader` 返回空 `QIcon`，功能不受影响，仅视觉降级。
