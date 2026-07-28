---
version: 1.0.0
name: PassVault Desktop
description: >
  Windows 桌面密码管理器的 Figma 视觉规范。冷灰蓝画布托起一整块 22px 圆角的近白
  应用外壳，唯一结构色是 Notion 蓝；三栏工作区 + 右侧滑入编辑器 + 整页替换设置。
  中文正文用 Noto Sans SC，密码与快捷键提示切 DM Mono。

colors:
  # 品牌与强调 —— 唯一结构色
  primary: "#276cf0"           # Notion 蓝，主 CTA、active nav 底、tab 底线、密度条
  primary-pressed: "#1d60df"   # 按下加深
  primary-tint: "#e8f0ff"      # active nav 底 / 分类徽章底
  primary-tint-2: "#dbe9ff"    # 搜索 focus ring
  primary-text-on-tint: "#2369ee"  # active nav 文字
  primary-accent-text: "#3777e7"   # count / tag 文字（较饱和）
  primary-accent-alt: "#3675de"    # 侧栏 count 徽章文字
  primary-focus-border: "#8ab3ff"  # 搜索 focus 边
  primary-hover-surface: "#eaf0f8" # nav item hover 底
  primary-hover-surface-2: "#eaf2ff" # 锁定按钮 hover 底
  primary-hover-surface-3: "#eef4ff" # 详情图标按钮 hover
  primary-hover-surface-4: "#eff4fa" # 设置返回按钮 hover
  primary-hover-surface-5: "#edf2f8" # 编辑器 tab hover
  primary-hover-surface-6: "#f4f7fb" # 详情"复制密码"按钮 hover
  primary-tab-active-bg: "#e4efff" # 编辑器左侧 tab active 底
  primary-strength-bar: "#2771ef"  # 密度条（强度指示器）
  primary-strength-text: "#3979e7" # 密度条"强"文字
  primary-icon-cloud: "#4281f2"    # 侧栏云图标
  primary-icon-lock: "#3b78df"     # 顶栏锁定图标
  primary-setting-icon: "#3174e6"  # 设置行图标
  link: "#2771ed"                  # 网址链接文字

  # 表面与背景
  canvas: "#eef2f7"       # 页面画布（body 底）
  shell: "#fbfcfe"        # 应用外壳内容底 / 设置页底 / 表单输入底
  sidebar-bg: "#f5f8fc"   # 侧栏底 / 编辑器 tab 栏底
  detail-bg: "#fcfdff"    # 详情面板底（极微偏冷）
  card: "#ffffff"         # 密码卡片默认底 / 详情信息卡 / 设置外卡片
  card-selected: "#f4f8ff"# 密码卡片选中底
  card-hover: "#fbfcff"   # 密码卡片 hover 底
  search-bg: "#f7f9fc"    # 顶栏搜索框底
  soft-bg: "#f9fbfd"      # 时间戳卡片底
  soft-bg-sync: "#f7faff" # 同步信息块底
  soft-bg-select: "#fafcff"# 设置下拉底

  # 文字色阶（从深到浅）
  ink: "#1e2a38"          # 主要文字：页面标题、密码卡片标题、按钮文字
  ink-2: "#334354"        # 副文字：详情信息值、输入文字
  body: "#435466"         # 正文次要：textarea 文字
  body-2: "#506174"       # 二级按钮文字
  muted: "#59687a"        # 侧栏未选中 section 文字
  muted-2: "#5f7186"      # 详情图标按钮 / 设置返回按钮
  muted-3: "#5d6d80"      # 编辑器取消按钮
  muted-4: "#597087"      # 设置次级按钮
  muted-5: "#617084"      # 侧栏"新增分类"按钮
  muted-6: "#637488"      # 时间戳文字
  muted-7: "#65778b"      # 编辑器关闭按钮
  muted-8: "#667587"      # 侧栏设置按钮 / group nav
  muted-9: "#6b7b8f"      # 顶栏更多按钮
  muted-10: "#6d7e91"     # 详情密码显示切换
  muted-11: "#6e7d8e"     # 编辑器 label
  muted-12: "#708094"     # 编辑器 tab inactive
  muted-13: "#718197"     # 侧栏同步行
  muted-14: "#51677e"     # "修改主密码"按钮
  muted-15: "#42617f"     # 设置 tab hover 文字
  hint: "#7b899a"         # 详情 label
  hint-2: "#7b8b9e"       # 详情副标题
  hint-3: "#7c8a9b"       # 密码卡片副文字
  hint-4: "#7c8b9d"       # 设置描述
  hint-5: "#7e8c9b"       # 排序按钮
  hint-6: "#758499"       # 设置 tab inactive
  hint-7: "#768599"       # 搜索框图标
  hint-8: "#73859b"       # 编辑器输入眼睛图标
  hint-9: "#79889a"       # 设置行描述
  hint-10: "#627489"      # 同步信息 label
  placeholder: "#8a98aa"  # 搜索占位符
  placeholder-2: "#8491a1"# 时间戳文字
  placeholder-3: "#8694a5"# 快捷键提示文字
  placeholder-4: "#8b98a8"# 侧栏 eyebrow / 星标图标
  info-value: "#33465c"   # 同步信息值
  detail-copy: "#537292"  # 密码复制按钮
  detail-copy-2: "#648099"# 用户名复制按钮
  external-icon: "#5681bd"# 网址外链图标

  # 边框与分隔线
  border-shell: "#d9e1eb"    # 应用外壳外框
  border-section: "#e4e9ef"  # 顶栏底边 / 设置页顶栏
  border-section-2: "#e5eaf0"# 中间栏水平分隔
  border-section-3: "#e6ebf1"# 编辑器头部
  border-section-4: "#e8edf3"# 设置 tabs 底边
  border-panel: "#dce4ee"    # 输入框边 / 二级按钮边
  border-panel-2: "#dfe6ee"  # 编辑器抽屉左边
  border-panel-3: "#e1e8f0"  # 设置外卡片
  border-panel-4: "#e3e9f0"  # 详情信息卡边
  border-sidebar: "#e3e8ef"  # 侧栏右边
  border-sidebar-2: "#e2e8f0"# 侧栏内水平分隔
  border-nav-hover: "#e1eaf7"# nav "���增分类"按钮 hover
  border-drawer-inner: "#e5ebf1" # 编辑器底部分隔
  border-card: "#edf0f4"     # 密码卡片默认边 / 卡片内水平分隔
  border-card-hover: "#dce6f4" # 密码卡片 hover 边
  border-card-selected: "#79a9ff" # 密码卡片选中边（唯一"蓝边"）
  border-divider-hair: "#e1e6ed"  # 顶栏按钮组分隔

  # 语义色
  success: "#34a36a"         # 复制成功勾 / 同步点
  success-text: "#2e9d5b"    # 已连接徽章文字
  success-tint: "#e6f8ed"    # 已连接徽章底
  warn: "#d27d45"            # risky 图标
  warn-text: "#9d643c"       # risky 横幅文字
  warn-tint: "#fff8f2"       # risky 横幅底
  warn-border: "#f1d9c5"     # risky 横幅边
  danger-text: "#bd5b57"     # "清除本地数据"红字链接
  toggle-off: "#cbd5e1"      # Toggle 关闭态

  # 装饰色（应用图标品牌色，仅用于密码卡片图标方块）
  brand-google-bg: "#f7f8fa"
  brand-google-fg: "#4285f4"
  brand-github-bg: "#202725"
  brand-github-fg: "#ffffff"
  brand-microsoft-bg: "#ebf3ff"
  brand-microsoft-fg: "#2577ce"
  brand-alipay-bg: "#1677ff"
  brand-alipay-fg: "#ffffff"
  brand-wechat-bg: "#20b85a"
  brand-wechat-fg: "#ffffff"
  brand-amazon-bg: "#1e2930"
  brand-amazon-fg: "#f7ad2d"
  brand-notion-bg: "#ffffff"
  brand-notion-fg: "#171717"

typography:
  display-1:    { size: 17px, weight: 700, line-height: 1.5, letter-spacing: -0.03em, family: "Noto Sans SC" }
  title-lg:     { size: 18px, weight: 700, line-height: 1.5, family: "Noto Sans SC" }
  title-md:     { size: 16px, weight: 700, line-height: 1.5, family: "Noto Sans SC" }
  body-md:      { size: 14px, weight: 400, line-height: 1.5, family: "Noto Sans SC" }
  body-md-strong: { size: 14px, weight: 600, family: "Noto Sans SC" }
  body-sm:      { size: 13px, weight: 700, family: "Noto Sans SC" }
  caption:      { size: 12px, weight: 400, family: "Noto Sans SC" }
  caption-strong: { size: 12px, weight: 500, family: "Noto Sans SC" }
  caption-xs:   { size: 11px, weight: 400, family: "Noto Sans SC" }
  micro:        { size: 10px, weight: 500, family: "Noto Sans SC" }
  eyebrow:      { size: 10px, weight: 400, letter-spacing: 0.13em, transform: uppercase, family: "DM Mono" }
  kbd:          { size: 9px, weight: 400, family: "DM Mono" }
  password:     { size: 14px, weight: 400, letter-spacing: 0.16em, family: "DM Mono" }

rounded:
  xs:    4px      # 密度条 / 微条
  sm:    6px      # 图标小按钮
  md:    8px      # nav / 按钮 / 输入 / 选择器
  lg:    10px     # logo 容器 / 卡片图标方块
  xl:    12px     # 密码卡片 / info 卡片 / risky 横幅
  2xl:   16px     # 详情头像 / 设置外卡片
  shell: 22px     # 应用最外层圆角
  full:  9999px   # 徽章 pill / 状态点

spacing:
  base: 8px       # 8 基梯度，沿用 Tailwind 缺省 0.25rem 步进
  gutter-column: 20px   # 中间/详情栏内边距（p-5）
  gutter-column-lg: 28px# xl 断点上抬到 p-7
  gutter-sidebar-x: 12px# 侧栏水平内边（px-3）
  gutter-sidebar-y: 20px# 侧栏垂直内边（py-5）
  header-h: 68px        # 页面顶栏高度
  logo-h: 32px          # 侧栏 logo 图标方块
  nav-h: 36px           # 侧栏 section 行高
  group-nav-h: 32px     # 侧栏分类行高
  cta-h: 40px           # 侧栏"新建密码"按钮
  action-h: 36px        # 顶栏/编辑器/详情图标按钮
  input-h: 36px         # 编辑器输入
  strength-h: 4px       # 密度条

shadows:
  shell:        "0 24px 70px rgba(52,72,99,0.12)"   # 应用外壳整体阴影
  cta:          "0 5px 14px rgba(39,108,240,0.25)"  # 主 CTA 蓝色投影
  card-selected:"0 3px 10px rgba(53,105,193,0.09)"  # 密码卡片选中态微光
  drawer:       "-18px 0 45px rgba(50,73,105,0.14)" # 编辑器右抽屉左投影
  inset-hi:     "inset 0 0 0 1px rgba(255,255,255,0.25)" # logo 方块内高光

components:
  # 外壳与骨架
  app-shell:         # 应用最外层圆角容器
  three-column-grid: # 三栏工作区栅格
  top-header:        # 68px 顶栏（Vault 搜索版 / Preferences 返回版）
  # 侧边栏
  sidebar-shell:
  sidebar-logo:
  sidebar-cta-button:
  sidebar-section-item:  # 全部 / 收藏夹 / 未分类 / 回收站
  sidebar-eyebrow:       # "分类" 小标签
  sidebar-category-item: # 工作 / 个人 / 金融 …
  sidebar-sync-row:
  sidebar-settings-button:
  # 按钮
  button-primary:
  button-primary-large:  # 侧栏"新建密码" / 编辑器"保存"
  button-secondary:
  button-icon:
  button-destructive-link:
  # 输入
  input-search:
  input-text:
  input-password:
  select:
  textarea:
  # 卡片
  password-card:         # 默认 / hover / selected
  info-card:             # 详情三行信息
  timestamp-card:
  setting-outer-card:    # 设置页外层卡片
  # 徽章
  badge-tag:             # 分类蓝底徽章
  badge-count:           # 侧栏数字徽章
  badge-status:          # 已连接绿色徽章
  badge-kbd:             # Ctrl+F 白底黑字
  # 详情面板
  detail-header:
  detail-avatar:         # 48px 圆角首字母
  risky-banner:
  detail-info-row:       # 三行内嵌
  detail-footer-actions:
  # 编辑器
  editor-drawer:
  editor-tab-nav:
  editor-form-field:
  strength-indicator:
  editor-footer:
  # 设置
  preferences-tabs:
  setting-row:
  toggle-switch:
  # 反馈
  copy-success-check:
---

## Overview

PassVault 的界面像一张干净的工作台：冷灰蓝的画布 `{colors.canvas}` 托起一整块 22px 圆角的近白外壳 `{colors.shell}`，唯一的结构性强调色是 Notion 蓝 `{colors.primary}` —— 它出现在主 CTA、active 导航底、密码卡片选中环、tab 下划线、链接文字、密度条填充上；其他任何色相要么是装饰（应用图标品牌色），要么是语义（成功绿、警告橙）。

正文与 UI 文字全用 Noto Sans SC，遇到密码明文、快捷键提示、侧栏 "分类" 小标签时切到 DM Mono，构造工程感与信息密度感。整套 UI 通过密度而不是重阴影传达层次：一层微高光内嵌 + 一圈极浅描边 + 大量 8 的倍数间距，避免了任何 skeuomorphic 的沉重阴影。

**关键特征：**
- 冷灰蓝画布 + 一整块近白圆角应用外壳（22px shell、`{shadows.shell}`）
- 唯一结构色是 Notion 蓝 `#276cf0`；一切装饰色不参与 CTA
- 极浅描边 + 内嵌白高光 + 微阴影构建层次
- 三栏工作区：Sidebar 224px / 中间栏弹性 / 详情栏 360–430px
- 编辑不是模态而是 372px 右侧滑入抽屉
- 设置整页替换在中央 stack 上，不弹窗
- 中文正文 Noto Sans SC，密码/快捷键切等宽 DM Mono
- 支持 `sm` (640) / `lg` (1024) / `xl` (1280) 三档断点；`lg` 以下侧栏隐藏

## 颜色系统

> 所有颜色数值均从 `project/src/app/App.tsx` 的实际 Tailwind 类中提取。项目里另有一份
> `project/src/styles/theme.css` 定义了暖米色/深青绿主题，那是 shadcn 默认残留，未被
> 采用，忽略即可。

### 品牌与强调

Notion 蓝 `{colors.primary}` (#276cf0) 是这套设计里唯一的结构性强调色。它只做七件事：

- 主 CTA 按钮底色（侧栏"新建密码"、详情"打开网站"、��辑器"保存"、设置"立即同步"）
- Logo 方块底色（配 `{shadows.inset-hi}` 内嵌白高光）
- Active 导航文字（配 `{colors.primary-tint}` 淡蓝底）
- 密度条填充 `{colors.primary-strength-bar}`（几乎同色，实为 `#2771ef`）
- 网址链接文字 `{colors.link}` (`#2771ed`)
- 设置页 tab 下划线 + active tab 文字
- 编辑器左侧 active tab `{colors.primary-tab-active-bg}` (#e4efff) 底 + `{colors.primary}` 字

按下态统一切到 `{colors.primary-pressed}` (#1d60df)；主 CTA 会额外套一层 `{shadows.cta}` 蓝色投影。

### 表面与背景

从外到内五层浅色梯度：

| Token | 值 | 位置 |
|---|---|---|
| `{colors.canvas}` | `#eef2f7` | 页面 body，视觉最"冷"的一层 |
| `{colors.shell}` | `#fbfcfe` | 应用最外层内容底、设置整页底、编辑器表单输入底 |
| `{colors.sidebar-bg}` | `#f5f8fc` | 左侧栏、编辑器左侧 tab 栏 |
| `{colors.detail-bg}` | `#fcfdff` | 详情面板底（几乎白，微偏冷） |
| `{colors.card}` | `#ffffff` | 密码卡片默认、详情信息卡、设置外卡片 |

选中/悬浮的密码卡片单独有两个稀释色：`{colors.card-selected}` (#f4f8ff) 与 `{colors.card-hover}` (#fbfcff)。此外还有一系列"浅底信息块"用途的辅助色：`{colors.search-bg}`、`{colors.soft-bg}` (#f9fbfd) 用于时间戳卡片，`{colors.soft-bg-sync}` (#f7faff) 用于同步信息块。

### 文字色阶

正文文字有 4 个语义层级：

- `{colors.ink}` (#1e2a38) —— 主要标题、密码卡片标题、按钮文字
- `{colors.ink-2}` (#334354) —— 详情信息值、输入文字
- `{colors.body}` / `{colors.body-2}` —— 正文次级
- `{colors.muted}` … `{colors.muted-15}` —— 侧栏未选中、图标按钮、辅助操作文字
- `{colors.hint}` … `{colors.hint-10}` —— label 文字、卡片副文字、描述

设计实际存在 30+ 个近似冷灰色。它们不是随意的，App.tsx 里每处 hex 各自表达微妙的层级——一个搜索框图标 `#768599` 会比它旁边的占位符 `#8a98aa` 深一档，凑成"图标醒目、hint 退让"的视觉分级。**实现时不必逐一还原**，选取 4–5 个代表色即可（例如 `hint-1..hint-5`，对应 `#7b899a → #8b98a8`），但保留这份原始列表以便追溯每处的原始意图。

占位符层与再往浅的辅助色一律在 `{colors.placeholder}` 家族 (#8a…#8b) 之间取。

### 边框与分隔线

边框全部走冷灰蓝色系，绝不用中性灰：

| Token | 值 | 用途 |
|---|---|---|
| `{colors.border-shell}` | `#d9e1eb` | 应用外壳唯一外框 |
| `{colors.border-section}` | `#e4e9ef` | 顶栏底边、设置页顶栏底 |
| `{colors.border-panel}` | `#dce4ee` | 输入框、二级按钮、下拉选择 |
| `{colors.border-card}` | `#edf0f4` | 密码卡片默认边 + 卡片内水平分隔 |
| `{colors.border-card-selected}` | `#79a9ff` | **密码卡片选中态**（唯一亮蓝边） |
| `{colors.border-drawer-inner}` | `#e5ebf1` | 编辑器底部分隔 |

其余细分见 YAML front matter；同一"层级"内不同位置的边框在原设计里有 ±1 位的微差（例如 `#e3e8ef` vs `#e5eaf0`），可以合并为一个 token。

### 语义色

只在四个位置出现语义色：

- **成功**：`{colors.success}` (#34a36a) 复制成功勾、同步就绪圆点；徽章场景切 `{colors.success-text}` (#2e9d5b) 文字 + `{colors.success-tint}` (#e6f8ed) 底
- **警告**：risky 横幅一套四色 —— 底 `{colors.warn-tint}` (#fff8f2)、边 `{colors.warn-border}` (#f1d9c5)、文字 `{colors.warn-text}` (#9d643c)、图标 `{colors.warn}` (#d27d45)
- **破坏**：仅 `{colors.danger-text}` (#bd5b57) 一处 —— 设置页"清除本设备数据"红字文字链接
- **关闭态**：Toggle 关闭态 `{colors.toggle-off}` (#cbd5e1)

**没有传统意义的错误红填充色**（红底白字）。破坏性动作用红色文字链接而不是按钮。

### 装饰色（应用图标品牌色）

密码卡片头像方块用应用自身的品牌色，仅在这一处使用，永不作为 UI 元素颜色：

| 应用 | 背景 | 文字/首字母 |
|---|---|---|
| Google | `#f7f8fa` | `#4285f4` |
| GitHub | `#202725` | `#ffffff` |
| Microsoft | `#ebf3ff` | `#2577ce` |
| 支付宝 | `#1677ff` | `#ffffff` |
| 微信 | `#20b85a` | `#ffffff` |
| Amazon | `#1e2930` | `#f7ad2d` |
| Notion | `#ffffff` | `#171717` |

## 排版

### 字体家族

三套字体在 Google Fonts 中导入：

- **Noto Sans SC** (400/500/600/700) —— 中文与拉丁通用正文字体
- **DM Mono** (400/500) —— 等宽，专用于密码明文、快捷键、"分类" eyebrow
- **Playfair Display** (500/600 及 italic 500) —— 已导入但当前 UI 未使用，为未来营销/引言留白

App 主容器的 `font-['Noto_Sans_SC']` 定基调；密码/快捷键场景显式切 `font-['DM_Mono']`。

### 层级

| Token | Size | Weight | Line H. | Letter Sp. | Family | 用途 |
|---|---|---|---|---|---|---|
| `{typography.display-1}` | 17px | 700 | 1.5 | -0.03em | Noto Sans SC | 侧栏品牌 logo "PassVault" |
| `{typography.title-lg}` | 18px | 700 | 1.5 | — | Noto Sans SC | 详情面板标题、设置分区大标题 |
| `{typography.title-md}` | 16px | 700 | 1.5 | — | Noto Sans SC | "所有密码"、"设置" h1 |
| `{typography.body-md}` | 14px | 400/500/600 | 1.5 | — | Noto Sans SC | 侧栏 nav、按钮、通用正文 |
| `{typography.body-sm}` | 13px | 700 | — | — | Noto Sans SC | 卡片首字母头像文字 |
| `{typography.caption}` | 12px | 400/500 | — | — | Noto Sans SC | 卡片副文字、输入 hint |
| `{typography.caption-xs}` | 11px | 400/500 | — | — | Noto Sans SC | 密码卡片副文字、label |
| `{typography.micro}` | 10px | 500/600 | — | — | Noto Sans SC | 徽章 / 时间戳 / 小 hint |
| `{typography.eyebrow}` | 10px | 400 | — | 0.13em uppercase | DM Mono | 侧栏"分类"小标签 |
| `{typography.kbd}` | 9px | 400 | — | — | DM Mono | 快捷键提示（Ctrl+F 徽章） |
| `{typography.password}` | 14px | 400 | — | 0.16em | DM Mono | 密码明文显示 |

### 使用原则

1. **中文优先** —— 中英混排时用 Noto Sans SC 一套字体保证节奏一致
2. **DM Mono 是信号词** —— 只要看到等宽，用户就知道这是"机器数据"（密码、快捷键、系统 token）
3. **粗体承载信息层级** —— 14px/700 用于卡片标题、按钮 label；同样 14px 的 400 是次级正文
4. **文字与图标同色** —— 每个按钮内的图标与文字使用同一个 muted 家族颜色

### 字体降级方案

Noto Sans SC 无法加载时的降级顺序：`"Noto Sans SC", "PingFang SC", "Microsoft YaHei", "Hiragino Sans GB", sans-serif`。DM Mono 降级到 `ui-monospace, "Cascadia Code", Consolas, monospace`。

## 布局

### 间距尺度

沿用 Tailwind 4 缺省的 8 基（0.25rem = 4px）梯度：

- `4/8/12/16/20/24/28/32/40/44` —— 常用像素值
- 4px = `gap-1` = 微对齐；8px = `gap-2` = 图标与文字；12px = `px-3` = 侧栏行水平内边；20px = `p-5` = 中/详列内边；28px = `p-7` = xl 断点上抬后的中/详列内边
- 特殊像素：`h-9` (36) `h-10` (40) `size-8` (32) `size-9` (36) `size-12` (48)

### 应用外壳规格

外壳是页面里视觉重量最大的元素：

- 最大宽度 `max-w-[1600px]`，水平居中
- 最小高度 `min-h-[760px]`
- 圆角 `{rounded.shell}` (22px)
- 边框 `1px solid {colors.border-shell}`
- 阴影 `{shadows.shell}`
- 底色 `{colors.shell}`
- 页面 body 外圈留 `p-2` (8px) 或 `sm:p-4` (16px) 呼吸空间

### 三栏工作区规格

| 列 | 宽度 | 内容 |
|---|---|---|
| Sidebar | 固定 `224px` (`w-[224px]`) | Logo + 新建 CTA + Sections + 分类 + 底部同步/设置 |
| 中间栏 | 弹性 `minmax(430px, 1fr)` | 顶栏搜索 + "所有密码 N 项" + 密码卡片列表 |
| 详情栏 | 弹性 `minmax(360px, 430px)` | 头像/标题 + 风险横幅 + 三行信息 + 时间戳 + 双按钮 |

只有 `xl` (≥1280px) 断点上展开三栏；`xl` 以下详情栏塌陷到中间栏底部。侧栏在 `lg` (<1024px) 以下整体隐藏。

### 页面共同头部

Vault 与 Preferences 两页的顶栏形制统一：

- 高度 `68px`（`h-[68px]`）
- 底部一条 `border-b {colors.border-section}`
- 左对齐首元素（Vault 是搜索框，Preferences 是"返回"按钮）
- 右对齐辅助按钮（Vault 是锁定 + 更多，Preferences 无）

### 响应式断点

| 断点 | 阈值 | 行为 |
|---|---|---|
| Mobile | < 640px | 侧栏 + 详情栏隐藏；仅中间栏单列滚动 |
| `sm` | ≥ 640px | 主容器 padding 从 8px 上抬到 16px；顶栏内水平内边 `sm:px-7` |
| `lg` | ≥ 1024px | Sidebar 显示（`hidden lg:flex`） |
| `xl` | ≥ 1280px | 中间栏与详情栏并排（`xl:grid-cols-[minmax(430px,1fr)_minmax(360px,430px)]`） |

触摸目标最小 44 × 44 px；桌面场景所有 icon-only 按钮实际都是 `size-8` (32px) 或 `size-9` (36px)，鼠标操作足够。

### 空白哲学

设计不用留白讲究，而是用**信息密度差**制造分级。密码卡片行高只有 ~60px，靠 4 个元素（头像 / 标题+账号 / 时间 / 星标）分列布局；详情面板反而给每个字段独占一整块 `p-4` 卡片内嵌。同一个列表里稠密，同一个卡片里疏朗。

## 高度与阴影

阴影只出现在五处，且总量克制：

| Token | 配方 | 场景 |
|---|---|---|
| `{shadows.shell}` | `0 24px 70px rgba(52,72,99,0.12)` | 应用外壳一整块投在冷灰画布上 |
| `{shadows.cta}` | `0 5px 14px rgba(39,108,240,0.25)` | 侧栏"新建密码"蓝色 CTA 投影 |
| `{shadows.card-selected}` | `0 3px 10px rgba(53,105,193,0.09)` | 密码卡片选中态微光 |
| `{shadows.drawer}` | `-18px 0 45px rgba(50,73,105,0.14)` | 编辑器抽屉左侧向画布投影 |
| `{shadows.inset-hi}` | `inset 0 0 0 1px rgba(255,255,255,0.25)` | 侧栏 logo 蓝方块内嵌一圈白高光 |

**高度哲学**：不用重阴影模拟"悬浮"。层次靠底色变化、极浅描边、微高光内嵌来表达。除以上五处以外，密码卡片头像方块内隐藏一层 `shadow-sm`（Tailwind 缺省的极弱阴影），也只是为了让方块从卡片底稍稍浮起来。

## 圆角

| Token | 值 | Tailwind 来源 | 用途 |
|---|---|---|---|
| `{rounded.xs}` | 4px | `rounded` | 密度条段、微条 |
| `{rounded.sm}` | 6px | `rounded-md` | 图标小按钮 |
| `{rounded.md}` | 8px | `rounded-lg` | nav 项、按钮、输入框、select、textarea |
| `{rounded.lg}` | 10px | `rounded-[10px]` | 侧栏 logo 方块、密码卡片图标方块 |
| `{rounded.xl}` | 12px | `rounded-xl` | 密码卡片、info 卡片、risky 横幅 |
| `{rounded.2xl}` | 16px | `rounded-2xl` | 详情头像、设置页外卡片、setting 图标方块 |
| `{rounded.shell}` | 22px | `rounded-[22px]` | 应用最外层 |
| `{rounded.full}` | 9999px | `rounded-full` | 徽章 pill、状态圆点、Toggle |

**升级链**：数值随元素体量递增。图标按钮 8 → 密码卡片 12 → 详情大头像 16 → 应用外壳 22。徽章/圆点/Toggle 一律 `full`。

## 图标

### 图标库

系统采用 **Lucide** 图标（`lucide-react` 通过 npm 引入）。所有图标默认 stroke 描边，宽度在 1.8–2.4 之间：

- 主 CTA 图标 (`Plus` in "新建密码")：stroke 2.4
- 常规操作图标（Copy/Eye/EyeOff/Pencil/…）：stroke 2.0
- 次级/装饰图标（Folder/Cloud/Star/…）：stroke 1.8

**尺寸约定**：以像素单位命名，`size-3.5` (14px) / `size-4` (16px) / `size-[17px]` / `size-5` (20px)。图标与相邻文字颜色同 hex，颜色不单独设。

### 图标清单

App 当前使用 22 个 Lucide 图标（与 `docs/figma_refactor.md` 里已翻译到 SVG 的清单一致）：

`vault` `plus` `search` `list` `star` `folder` `folder-open` `trash-2` `cloud` `settings-2` `lock-keyhole` `more-horizontal` `pencil` `copy` `eye` `eye-off` `external-link` `arrow-left` `x` `shield-alert` `shield-check` `key-round` `check` `archive` `chevron-down`

三个图标独立表达状态：`ShieldAlert` = 风险重复密码警告；`ShieldCheck` = Windows Hello 保护；`Check` = 复制成功反馈。

## Components

### 外壳与骨架

**`app-shell`** — 应用整体圆角容器
- 底色 `{colors.shell}`，边框 `1px solid {colors.border-shell}`，圆角 `{rounded.shell}`，阴影 `{shadows.shell}`
- 尺寸 `max-w-[1600px] min-h-[760px]`，父层 body 底色为 `{colors.canvas}`
- 使用 `overflow-hidden` 让子元素与圆角剪裁对齐

**`three-column-grid`** — 三栏工作区栅格
- CSS Grid：`grid-cols-1 xl:grid-cols-[minmax(430px,1fr)_minmax(360px,430px)]`
- 中间栏与详情栏由一条 `border-r {colors.border-section-2}` 分隔（仅 xl 断点上生效）；xl 以下由 `border-b` 分隔

**`top-header`** — 68px 顶栏
- 高度 `h-[68px]`，横向 flex 布局，底边 `border-b {colors.border-section}`
- 水平内边 `px-5 sm:px-7`
- Vault 版：左侧最大 `max-w-[390px]` 的 `input-search`，右侧 `button-icon` 组（锁定 + 分隔竖线 + 更多）
- Preferences 版：左侧 `button-icon` 返回 + `{typography.title-md}` 文字"设置"

### 侧边栏

**`sidebar-shell`** — 侧栏容器
- 宽度 `w-[224px]`，`hidden lg:flex flex-col`
- 底色 `{colors.sidebar-bg}`，右边 `border-r {colors.border-sidebar}`
- 内边 `px-3 py-5`

**`sidebar-logo`** — 品牌行
- 32×32 圆角 `{rounded.lg}` 蓝方块 `{colors.primary}`，内嵌 `{shadows.inset-hi}` ���高光
- 白色 `vault` 图标 17px stroke 2.0
- 文字 "PassVault" 用 `{typography.display-1}`

**`sidebar-cta-button`** — "新建密码"主按钮
- 高度 `h-10` (40px)，横满 sidebar 内容宽
- 底色 `{colors.primary}`，按下切 `{colors.primary-pressed}`
- 阴影 `{shadows.cta}`
- 按下额外 `scale(0.98)` 变换
- 圆角 `{rounded.md}`，白色 `{typography.body-md-strong}` 文字 + `Plus` 图标

**`sidebar-section-item`** — Sections 行（全部/收藏/未分类/回收站）
- 高度 `h-9` (36px)，圆角 `{rounded.md}`，横向 flex 图标 + 文字 + 数字徽章
- 默认：图标与文字 `{colors.muted}` (#59687a)，hover 底切 `{colors.primary-hover-surface}`
- Active：底 `{colors.primary-tint}`，文字 `{colors.primary-text-on-tint}`，权重升到 600

**`sidebar-eyebrow`** — "分类" 小标签
- `{typography.eyebrow}` (DM Mono 10px uppercase, letter-spacing 0.13em)
- 颜色 `{colors.placeholder-4}`
- 右侧带一个 24×24 "新增分类"图标按钮，hover 图标切 `{colors.primary}`

**`sidebar-category-item`** — 分类行（工作/个人/…）
- 高度 `h-8` (32px)，同 `sidebar-section-item` 的圆角/交互
- 图标 `folder` 15px stroke 1.8
- 数字徽章：底 `{colors.border-nav-hover}` alt 底 `#e3ebf7`，文字 `{colors.muted-8}`
- Active 状态色与 section 一致

**`sidebar-sync-row`** — 底部同步状态行
- 高度 `h-9`，图标 `cloud` 16px `{colors.primary-icon-cloud}` + 文字 "已同步到云端" + 右侧 6×6 圆点 `{colors.success}`
- 顶部一条 `border-t {colors.border-sidebar-2}` 分隔

**`sidebar-settings-button`** — "设���" 按钮
- 与 `sidebar-section-item` 同规格，图标 `settings-2` + 文字 "设置"
- 底部固定（`mt-auto`）

### 按钮

**`button-primary`** — 主 CTA（40px 大号 / 32px 中号两种）
- 底 `{colors.primary}` / 按下 `{colors.primary-pressed}`
- 白文字 `{typography.body-md-strong}` (中号 caption-xs)
- 圆角 `{rounded.md}`；大号带 `{shadows.cta}`
- 侧栏"新建密码"、编辑器"保存"、设置"立即同步"、详情"打开网站"都是这一件

**`button-secondary`** — 边框按钮
- 高度 `h-9`，圆角 `{rounded.md}`
- 边 `1px solid {colors.border-panel}`，无底色（透明或近白）
- 文字 `{typography.caption-strong}` `{colors.muted-3}` / `{colors.body-2}`
- 用于编辑器"取消"、详情"复制密码"、设置"管理连接"、"修改主密码"

**`button-icon`** — 无背景图标按钮
- 尺寸 `size-8` 或 `size-9`，圆角 `{rounded.md}`
- 默认无底色，图标着 `{colors.muted-2}` 或 `{colors.muted-9}`
- Hover 底色进入所在容器的"primary-hover-surface"家族色（因场景而异，见 YAML colors）
- 顶栏锁定按钮的图标色是 `{colors.primary-icon-lock}` 醒目蓝

**`button-destructive-link`** — 危险文字链接
- 无边无底，仅 `{typography.caption}` `{colors.danger-text}` (#bd5b57)
- 前置 `trash-2` 图标
- 只用于"清除本设备本地数据"

### 输入

**`input-search`** — 顶栏搜索框
- 高度 `h-9`，圆角 `{rounded.md}`
- 边 `1px solid {colors.border-panel}`，底 `{colors.search-bg}`
- Focus：边切 `{colors.primary-focus-border}` + 两圈 `ring-2 {colors.primary-tint-2}` focus ring
- 左侧 `search` 图标 16px `{colors.hint-7}`
- 中间 input，占位符 `{colors.placeholder}`
- 右侧 kbd 徽章 "Ctrl + F"

**`input-text`** — 编辑器文本输入
- 高度 `h-9`，圆角 `{rounded.md}`，边 `{colors.border-panel}`，底 `{colors.shell}` (fbfcfe)
- 文字 `{typography.caption}` `{colors.ink-2}`
- Label 独立一行 `{typography.caption-xs}` `{colors.muted-11}` 位于输入框上方

**`input-password`** — 同 `input-text` 但右侧带 `eye` 图标 (14px, `{colors.hint-8}`) 用于切换显隐

**`select`** — 下拉选择
- 与 `input-text` 同外观（高度 36 / 圆角 8 / 边色 / 底色）
- 底色可用 `{colors.soft-bg-select}` (#fafcff) 略偏浅

**`textarea`** — 多行文本
- 高度 `h-24` (96px)，禁止拉伸 (`resize-none`)
- 其余同 `input-text`；文字 `{colors.body}` (#435466)

### 卡片

**`password-card`** — 密码列表卡片
- 布局：flex 图标方块(36px) + 标题栏(标题 + risky图标 + 副账号) + 时间戳 + 星标
- 圆角 `{rounded.xl}`，内边 `p-3`
- **默认态**：底 `{colors.card}`，边 `{colors.border-card}`
- **Hover 态**：底 `{colors.card-hover}` (#fbfcff)，边 `{colors.border-card-hover}` (#dce6f4)
- **选中态**：底 `{colors.card-selected}` (#f4f8ff)，边 `{colors.border-card-selected}` (#79a9ff)，阴影 `{shadows.card-selected}`
- 图标方块 `{rounded.lg}` (10px) + 品牌色（见"装饰色"表）+ `{typography.body-sm}` (13px/700) 首字母

**`info-card`** — 详情面板三行信息卡
- 圆角 `{rounded.xl}`，边 `1px solid {colors.border-panel-4}` (#e3e9f0)，底 `{colors.card}`
- 内边 `p-4`
- 三行由 `border-t {colors.border-card}` (#edf0f4) 水平分隔
- 每行 label `{typography.caption-xs}` `{colors.hint}`  + 值区域

**`timestamp-card`** — 时间戳卡片
- 圆角 `{rounded.xl}`，边 `{colors.border-section}` (#e4e9ef)，底 `{colors.soft-bg}` (#f9fbfd)
- 内边 `p-4`
- 内容 `{typography.caption-xs}` `{colors.muted-6}`，`text-[11px] leading-5`

**`setting-outer-card`** — 设置整页里的白卡
- 圆角 `{rounded.2xl}` (16px)，边 `1px solid {colors.border-panel-3}` (#e1e8f0)，底 `{colors.card}`
- 顶部一段 `preferences-tabs`，内容区 `p-5 sm:p-7`

### 徽章

**`badge-tag`** — 分类徽章（"工作"、"个人"）
- 圆角 `{rounded.full}` pill
- 底 `{colors.primary-tint}` (#e8f0ff)，文字 `{colors.primary-accent-text}` (#3777e7)
- 内边 `px-2 py-0.5`，`{typography.micro}` 10px/500

**`badge-count`** — 侧栏计数徽章
- 圆角 `{rounded.full}` pill
- Section 未激活：底 `#d8e7ff`，文字 `{colors.primary-accent-alt}` (#3675de)
- 分类未激活：底 `#e3ebf7`，文字 `{colors.muted-8}`
- 内边 `px-1.5 py-px`，`{typography.kbd}`-ish 10px/600

**`badge-status`** — "已连接" 绿色徽章
- 圆角 `{rounded.full}`
- 底 `{colors.success-tint}` (#e6f8ed)，文字 `{colors.success-text}` (#2e9d5b)
- 10px/600

**`badge-kbd`** — 快捷键提示
- 底 `#ffffff`，文字 `{colors.placeholder-3}` (#8694a5)
- `{typography.kbd}` DM Mono 9px
- 圆角小 `rounded` (Tailwind 默认 4px)

### 详情面板

**`detail-header`** — 详情面板头部
- 底色 `{colors.detail-bg}` (#fcfdff)，内边 `p-5 sm:p-7`
- 右上角浮 `button-icon` 组（编辑 + 更多），`absolute top-5 right-6`
- 主体是 flex 头像 + (标题行 + 副标题) 两段

**`detail-avatar`** — 48×48 圆角首字母
- 圆角 `{rounded.2xl}` (16px)，底与文字色来自 `{colors.brand-*}` 家族
- `{typography.title-lg}`-ish 但更粗（`text-lg font-bold`）

**`risky-banner`** — 风险重复密码横幅
- 圆角 `{rounded.xl}` (12px)，内边 `p-3`
- 底 `{colors.warn-tint}` (#fff8f2)，边 `{colors.warn-border}` (#f1d9c5)
- `shield-alert` 图标 16px `{colors.warn}` (#d27d45)
- 文字 `{typography.caption}` `{colors.warn-text}` (#9d643c)

**`detail-info-row`** — 三行信息（用户名 / 密码 / 网址）
- 标签 `{typography.caption-xs}` `{colors.hint}`  + 值行
- 用户名值：`{typography.caption}` `{colors.ink-2}`，右侧 `copy` 图标按钮
- **密码值**：`{typography.password}` (DM Mono 14px 0.16em)，右侧两个按钮 `eye/eye-off` (显隐) + `copy`
- 网址值：`{typography.caption}` `{colors.link}` (#2771ed)，右侧 `external-link` 图标 `{colors.external-icon}`
- 复制成功切换 `copy-success-check`：`check` 图标 `{colors.success}`，1200ms 后回退

**`detail-footer-actions`** — 底部双按钮
- `grid grid-cols-2 gap-2`
- 左：`button-secondary` "复制密码" + `copy` 图标
- 右：`button-primary` (中号) "打开网站" + `external-link` 图标

### 编辑器

**`editor-drawer`** — 右侧滑入抽屉
- 定位 `absolute inset-y-0 right-0 z-20 max-w-[372px] w-full`
- 底 `{colors.card}` (#ffffff)，左边 `border-l {colors.border-panel-2}` (#dfe6ee)
- 阴影 `{shadows.drawer}`
- 顶部 `h-[68px]` header（返回箭头 + 标题 + 关闭 X）与页面顶栏等高
- 底部 `editor-footer` 固定

**`editor-tab-nav`** — 左侧纵向 tab 栏
- 宽度 `w-[98px]`，底 `{colors.sidebar-bg}` (#f5f8fc)
- 每个 tab：圆角 `{rounded.md}`，内边 `px-2 py-2`，`{typography.caption-xs}` 11px
- 默认文字 `{colors.muted-12}` (#708094)，hover 底 `{colors.primary-hover-surface-5}` (#edf2f8)
- Active：底 `{colors.primary-tab-active-bg}` (#e4efff)，文字 `{colors.primary}` 权重 600
- Tabs 内容：`基本信息 / 更多信息 / 高级设置`

**`editor-form-field`** — 标签 + 输入的组合
- Label 独立一行 `{typography.caption-xs}` `{colors.muted-11}`
- 输入见 `input-text` / `input-password`
- 字段间距 `mb-4` (16px)

**`strength-indicator`** — 密度指示条
- 4 段横向 flex，`h-1` (4px)，`rounded` (`{rounded.xs}`)，`gap-1`
- 强度填色 `{colors.primary-strength-bar}` (#2771ef)
- 未填段是背景 `{colors.card}` 或近白
- 右下角文字 `{typography.kbd}`-size (10px) `{colors.primary-strength-text}` (#3979e7) "强"

**`editor-footer`** — 底部动作条
- 顶边 `border-t {colors.border-drawer-inner}` (#e5ebf1)，内边 `p-4`
- 两个等宽按钮：左 `button-secondary` "取消"，右 `button-primary` "保存更改"/"创建密码"

### 设置

**`preferences-tabs`** — 水平下划线 tab
- 三个 tab 平铺：常规 / 同步 / 安全
- 每个 tab：`px-4 py-3.5`，底部 `border-b-2 border-transparent`
- 默认文字 `{colors.hint-6}` (#758499)，hover 切 `{colors.muted-15}` (#42617f)
- Active：`border-b-2 {colors.primary}` + 文字 `{colors.primary}` 权重 600
- 整条 tabs 下方 `border-b {colors.border-section-4}` (#e8edf3)

**`setting-row`** — 单行设置项
- 布局：36×36 图标方块 + (标题 + 描述) 两段 + 右侧控件
- 图标方块：圆角 `{rounded.2xl}` (16px)，底 `#edf4ff`，图标色 `{colors.primary-setting-icon}` (#3174e6)
- 标题 `{typography.body-md-strong}` `{colors.ink}`
- 描述 `{typography.caption-xs}` `{colors.hint-9}` (#79889a)
- 行间用 `border-b {colors.border-card}` 分隔，末行 `last:border-0`
- 右侧控件可能是 `toggle-switch` 或 `select` 或 `button-secondary`

**`toggle-switch`** — 开关
- 尺寸 `w-9 h-5` (36×20)，圆角 `{rounded.full}`
- On：底 `{colors.primary}`，圆点靠右 (`left-4`)
- Off：底 `{colors.toggle-off}` (#cbd5e1)，圆点靠左 (`left-0.5`)
- 圆点：16×16 白色 `{rounded.full}`，`shadow-sm`

### 反馈

**`copy-success-check`** — 复制成功指示
- 复制按钮点击后 1200ms 内切换：`copy` 图标 → `check` 图标
- 颜色 `{colors.success}` (#34a36a)
- 状态由 JS 定时器回退，不用长驻组件

## 页面

### Vault Workspace

保险库主视图 = `sidebar` + `three-column-grid` 的组合。中间栏与详情栏用 `xl` 断点切换并排/堆叠。

- 中间栏顶部：`{typography.title-md}` "所有密码" + `{typography.micro}` 蓝色 "128 项" + 右对齐"按更新时间"排序按钮
- 密码卡片列表：`space-y-1.5` 垂直堆叠
- 详情面板：默认选中列表第一条；空态在设计里未定义（当前实现忽略）

小屏（<lg）时侧栏隐藏，用户仍能通过中间栏顶栏搜索操作。

### Editor Panel

右侧 372px 抽屉从画布右缘滑入，覆盖住详情栏并部分覆盖中间栏。

- 头部：`ArrowLeft` 图标 + 标题（"新建密码" / "编辑密码 · {name}"） + 关闭 X
- 主体：左侧 `editor-tab-nav` + 右侧滚动表单区
- 表单顺序：标题 → 用户名 → 密码 → 密度条 → 网址 → 分类下拉 → 备注 textarea
- 底部：`editor-footer` 双按钮

打开时不 dim 背景（无遮罩层），关闭有从右滑出的过渡（`transition`）。

### Preferences

从 Vault 的中央 stack 切换到设置整页，替换而不是弹窗。

- 顶栏：`ArrowLeft` 图标 + `{typography.title-md}` "设置"
- 内容居中容器 `max-w-[920px] mx-auto p-6 sm:p-10`
- 外层 `setting-outer-card` 承载 `preferences-tabs` + tab 对应内容
- 三个 tab：**常规**（最小化到托盘 / 自动锁定）、**同步**（连接状态徽章 + 同步方式/最后同步 + 立即同步 按钮）、**安全**（主密码 / Windows Hello / 离开时锁定 / 清除本地数据）

`setting-row` 行数视 tab 不同，一般 2–4 行；安全 tab 底部另有 `button-destructive-link` "清除本设备的本地数据"。

## Do's and Don'ts

### Do

- 用 `{colors.primary}` **只做**主 CTA、active 导航底、密码卡片选中环、tab 下划线、链接文字、密度条填充 —— 其他一律不用
- 页面永远坐在 `{colors.canvas}` 冷灰蓝上，应用外壳用 `{colors.shell}`，卡片才 `#ffffff`
- 圆角遵循升级链：按钮/nav 8 → 卡片 12 → 详情/设置外框 16 → 应用外壳 22 → 徽章/圆点 full
- 中文一律 Noto Sans SC；密码明文和快捷键提示强制切 DM Mono
- 阴影只做五件事：应用外壳、主 CTA 投影、卡片选中态微光、编辑器抽屉左投影、logo 内高光
- 密码卡片选中态用 4 个信号同时表达：底色、边框色、微阴影、tag 徽章保留
- 应用图标品牌色（Google 蓝、GitHub 黑、微信绿…）仅用于列表和详情的图标方块，永不作为 UI 元素颜色
- 破坏性操作用红字文字链接（`{colors.danger-text}`）而不是红色按钮

### Don't

- 不要把品牌图标色（`{colors.brand-google-fg}` 等）用到按钮、边框、link 上
- 不要在 CTA 上再叠第二个高饱和色 —— 蓝色是本设计里唯一强调
- 不要把 pill 徽章形状套到按钮上；按钮永远是 `{rounded.md}` 8px 圆角
- 不要给卡片用重阴影模拟悬浮 —— 用底色变化 + 极浅微光即可
- 不要在设置或编辑器里用模态对话框 —— 设置整页替换，编辑器右侧抽屉
- 不要在 `lg` 以下断点强留侧栏 —— 保持 `hidden lg:flex` 让侧栏在窄屏消失
- 不要引用 `project/src/styles/theme.css` 里的暖米色/深青绿 tokens —— 那是 shadcn 默认残留，Figma 未采用
- 不要在正文里塞非 Noto Sans SC 字体，Playfair Display 目前预留未启用
