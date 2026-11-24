<p align="center">
  <a href="README.md">English</a> · <b>中文</b>
</p>

<h1 align="center">3DShell</h1>

<p align="center">
  <img src="icon.png" alt="DSSH icon" width="96" height="96">
</p>

<p align="center">
  <b>Nintendo 3DS 中文 SSH 客户端 — 拼音 IME · 语音输入 · ANSI 终端 · 双屏一体</b><br>
  上屏 ANSI 终端 · 下屏自绘软键盘 + 拼音 IME · RSA 公钥认证<br>
  按 <b>START+L</b> 把上下屏拼成一块 66×40 大终端（再按恢复软键盘）<br>
  按住 <b>R</b> 直接说中文给 Claude Code、松手即发，剩下的拼音键盘补打<br>
  从 3DS 直接连服务器跑 tmux + claude-code，离不开沙发也能 coding
</p>

<p align="center">
  <a href="https://github.com/Fishason/DSSH"><img alt="基于" src="https://img.shields.io/badge/基于-Fishason%2FDSSH-blue?logo=github"></a>
  <img alt="平台" src="https://img.shields.io/badge/platform-Nintendo%203DS-FE0016?logo=nintendo3ds&logoColor=white">
  <img alt="协议" src="https://img.shields.io/badge/license-MIT-blue">
  <img alt="构建" src="https://img.shields.io/badge/build-devkitARM-orange">
</p>

> [!NOTE]
> **3DShell** 基于开源项目 [Fishason/DSSH](https://github.com/Fishason/DSSH) 二次开发，新增了交互式多主机选择器、AI 编码智能体项目卡片管理器（支持 `claude` / `codex` / `opencode` 等）、多窗口终端会话、自定义软键盘主题与像素吉祥物等功能。

<p align="center">
  <img src="design/machine-page/list-dark-top.png" alt="SSH 机器选择上屏" width="360">
  <img src="design/machine-page/list-dark-bot.png" alt="SSH 机器选择下屏" width="300"><br>
  <sub>SSH 机器选择页（上屏：当前选中机器详情 · 下屏：机器列表）</sub>
</p>

<p align="center">
  <img src="design/project-page/card-list-top.png" alt="项目卡片上屏" width="360">
  <img src="design/project-page/card-list.png" alt="项目卡片下屏" width="300"><br>
  <sub>项目卡片页（上屏：启动命令与说明 · 下屏：交互式卡片选择）</sub>
</p>

<p align="center">
  <img src="docs/media/mascot_preview.png" alt="吉祥物预览" width="600"><br>
  <sub>像素动态吉祥物：螃蟹、蟹特Crab、坤坤、菌菌、大眼、牛来、刀盾狗</sub>
</p>

---

## 功能

- **完整 ANSI/VT100 终端**：tmux 状态栏、claude-code spinner、box-drawing
  边框、256-color、TrueColor、Braille — 都正常显示
- **中文渲染**：内置 Zpix 12px 像素字体覆盖 21,000+ 个 CJK 统一汉字 +
  Terminus 6×12 ASCII，中英混排 baseline 对齐
- **自绘软键盘**：iOS 风格 3px 圆角键 + 平滑下沉动画，字母 / 符号双页
- **拼音输入法**：rime-ice 顶部 30 万词典 + 缩写匹配（`nh` → 你好）+
  前缀 fallback（`nihaoz` 自动回退到 `nihao`）+ 候选条游标导航
- **语音输入（v1.0）**：按住 **R** 说中文 → 松手即停并发 →
  ~1-2 秒后转写文字直接进 SSH 终端。默认走 OpenRouter 上的
  Qwen3-ASR-Flash（`qwen/qwen3-asr-flash-2026-02-10`）云端推理——v1.2
  起取代 Whisper Large V3 Turbo，单价约为 Whisper Turbo 的两倍，个人用
  一个月仍只需几美分；也可装自部署 whisper.cpp 副轨当离线 / 隐私后路。
- **语音问 AI（v1.1 新增）**：按住 **R + START** 录中文（录音途中按
  **START** 也能升级成问答）→ 立刻收尾发送 → 1-2 秒后底屏弹出
  **问答 modal**，DeepSeek-Chat 答案带
  markdown 渲染（标题黄色、行内代码青色、列表 • 前缀等），顶屏
  SSH 会话完全不受打扰。modal 里按 **A** 关窗保留 history（可追问），
  按 **B** 关窗清空 history（新对话）。
- **RSA-4096 公钥认证**：libssh2 + mbedTLS，私钥放 SD 卡读
- **多窗口终端（最多 4 个）**：**X + Y + ▲** 在**同一次 SSH 登录**上再开一条
  shell channel（没有第二次握手、没有第二条 Tailscale 隧道），**X + Y + ▼**
  关闭当前窗口，**X + Y + ◀ ▶**（或 **L / R**）切换。同一时刻只显示一个窗口，但全部窗口
  都在跑——足够同时挂着三个 `opencode` 来回翻
- **原生 Tailscale 传输**：可选地让 3DS 加入 tailnet，通过直连 UDP、
  Tailscale Peer Relay 或 DERP 承载 SSH，不运行 Go 或 `tailscaled`
- **物理键全映射**：D-pad 方向键、修饰键 hold-style（Y=Ctrl / X=Alt）、
  Circle Pad scrollback（全屏程序内改发 PageUp / PageDown）。Shift 在下屏软键盘的 ⇧ 上。
- **跨所有 3DS 型号统一按键**：语音、AI 问答、IME 模式切换、双屏触发、
  多窗口管理——所有功能都用**每款 3DS 型号都有的实体键**，不需要任何
  机型检测。从最便宜的 2DS 到最新的 New 3DS 全功能一致。完整速查见
  [`KEYBINDING.md`](KEYBINDING.md)。
- **呆萌像素宠物吉祥物**：支持 7 款生动动画伴侣（🦀 经典小螃蟹、🦀 蟹特Crab、🐣 坤坤、🔬 紫晶噬菌体、👽 大眼外星人、🐂 牛来、🐕 刀盾狗），底行漫步、支持点击躲避、打字敲击、断线警报与重连欢呼；每个物种还有各自的按键音效
- **设置与 Debug 页面**：单击右上 ⚙ 打开**双屏设置页**（上屏是当前分页的条目列表 + 下屏详情卡，共四页：外观 / 输入 / 语音 / 项目，外加 Debug 入口）；Debug 页纯展示：SSH 字节流 + 物理键说明
- **多机器选择**：`config.ini` 里配了不止一台机器时，启动（以及断线后按 SELECT 重连）先弹**机器选择页**：下屏按**名字在上、地址在下**两行列出每台机器（上下两屏都跟随主题），上屏是高亮那台的名称 / 主机 / 用户 / 端口 / 密钥详情。选一台 → 连过去 → 再弹项目页。机器可以**在机内改名**（**X**，或点某行的 ✎ 条）。每台机器有**自己的** `settings.<N>.ini`：卡片、主题、界面语言、吉祥物、录音上限全部互相独立。详见 [SSH 机器选择](#ssh-机器选择多台机器)。
- **项目卡片**：握手完成后弹出**双屏项目页**，选「进哪个目录、在那儿启动哪个终端 agent」——预设含 `claude` / `codex` / `opencode` / `agy` / `gemini` / `aider`，也可以自己敲。下屏是卡片列表 + 四段式编辑器（目录 / 工具 / 参数 / 自定义），上屏显示当前卡片的介绍与**将要执行的确切命令**。**每个窗口各挑各的**，所以不同窗口可以在不同目录下跑不同工具。详见[项目卡片](#项目卡片选目录--agent-工具)。

## 目录

- [安装](#安装)
- [服务器侧准备](#服务器侧准备一次性)
- [配置 config.ini](#配置-configini)
- [语音输入](#语音输入)
- [SSH 机器选择](#ssh-机器选择多台机器)
- [项目卡片](#项目卡片选目录--agent-工具)
- [按键说明](KEYBINDING.md)（完整速查；下表为 README 内精简版）
- [输入法用法](#输入法用法)
- [设置与 Debug 页面](#设置与-debug-页面)
- [已知限制](#已知限制)
- [从源码构建](#从源码构建)
- [项目结构](#项目结构)
- [致谢](#致谢)
- [License](#license)

---

## 安装

DSSH 跑在 **破解过的 3DS / 2DS / New 3DS** 上。需要 Homebrew Launcher
（HBL）或 FBI 一类的 CIA 安装器。

### 方案 A — `.cia` 安装（推荐）

1. 下载本仓库 [Releases](../../releases) 里的 `DSSH.cia`（约 14 MB）
2. 拷到 SD 卡，路径任意（比如 `/cias/DSSH.cia`）
3. 启 FBI → SD → 选 `DSSH.cia` → `Install CIA`
4. HOME 菜单里出现橙色 DSSH 图标

### 方案 B — `.3dsx` 直跑

1. Releases 里下 `3dssh.3dsx`
2. 拷到 SD 卡 `/3ds/dssh/3dssh.3dsx`
3. 启 HBL → 选 DSSH

### 方案 C — `3dslink` WiFi 推送（开发用）

```bash
# 3DS 上 HBL 按 Y 进 "Waiting for 3dslink..."
3dslink -a <3DS-LAN-IP> 3dssh.3dsx
```

---

## 服务器侧准备（一次性）

3DS 上的 libssh2 用的是 mbedTLS 后端，**硬编码不支持 ed25519**。所以要
专门给 3DS 生成一把 RSA-4096 公私钥对，原来 PC 上的 ed25519 不动。

> ⚠️ **`-m PEM` 这个参数必加。**  devkitPro 仓库里的 `3ds-mbedtls`
> 还是 2.28.x 版本，**只能解析传统 PEM 格式**的私钥（首行
> `-----BEGIN RSA PRIVATE KEY-----`）。Ubuntu 18.04+ / macOS 上的
> 新版 `ssh-keygen` 默认输出的是**新 OpenSSH 格式**（首行
> `-----BEGIN OPENSSH PRIVATE KEY-----`），mbedtls 2.28 **解析不了**
> ——不加 `-m PEM` 的话 DSSH 会在握手时报 "auth failed"。

```bash
# 1. 在你的 PC 上生成 3DS 专用 RSA 密钥。-m PEM 强制传统 PEM 格式，
#    这样 3DS 上的 mbedtls 才能解析。
ssh-keygen -t rsa -b 4096 -m PEM -f ~/.ssh/id_rsa_3ds -C "3ds-ssh-client"

# 2. 把公钥复制到服务器的 authorized_keys
ssh-copy-id -i ~/.ssh/id_rsa_3ds.pub user@your-server.example.com

# 3. 验证从 PC 用新 RSA 能连
ssh -i ~/.ssh/id_rsa_3ds user@your-server.example.com 'echo OK'
```

**已经有 OpenSSH 格式的私钥怎么办？**  原地转格式即可，**不用**再
往服务器加公钥（公钥不变）：

```bash
ssh-keygen -p -m PEM -f ~/.ssh/id_rsa_3ds
# 输入旧 passphrase（如果原来没有就直接回车）
# 转换后文件首行应该是：-----BEGIN RSA PRIVATE KEY-----
```

可以用 `head -1 ~/.ssh/id_rsa_3ds` 检查首行确认格式。

**安全建议**：编辑服务器的 `~/.ssh/authorized_keys`，给这把 RSA 那一行
前面加 `from="<家里公网 IP>"`，这样即使 SD 卡丢了，钥匙也只能从你家网
络登入。

把 **私钥** `~/.ssh/id_rsa_3ds` 通过读卡器复制到 SD 卡的
`/3ds/3dssh/id_rsa`（注意：3DS 装 CIA 后 title-id 路径不同，但**配置 +
密钥**始终读 `sdmc:/3ds/3dssh/`）。

> ⚠️ SD 卡是明文。物理持有 SD 的人就能登服务器。务必加 `from="..."` IP
> 限制或 `command="..."` 命令限制。
>
> ⚠️ 另一半风险在线路上：DSSH **不校验服务器的主机密钥**，处在网络中间的人
> 可以冒充你的服务器。详见[已知限制](#已知限制)。

---

## 配置 config.ini

把仓库里的 `sd_template/3ds/3dssh/config.ini.example` 拷到 SD 卡的
`/3ds/3dssh/config.ini`，按你的服务器改：

```ini
host       = your-server.example.com
port       = 22
user       = ubuntu
key_path   = sdmc:/3ds/3dssh/id_rsa
passphrase =

# 可选：自动解锁当前 SSH 用户的 macOS 登录 keychain
macos_keychain_password =

# 可选：通过 tailnet 连接 SSH
tailscale_auth_key =
tailscale_hostname = dssh-3ds
tailscale_state = sdmc:/3ds/3dssh/tailscale.state
tailscale_control_url = https://controlplane.tailscale.com
```

| 字段 | 说明 |
|------|------|
| `host` | 服务器 IP 或域名 |
| `port` | 端口（默认 22） |
| `user` | 服务器登录用户名 |
| `key_path` | 私钥路径，`sdmc:/...` 是 3DS 标准 SD 路径前缀 |
| `passphrase` | 私钥口令；建议留空（SD 卡上输 passphrase 体验差） |
| `macos_keychain_password` | 可选的 macOS 登录密码；填写后为当前 SSH 用户启用自动解锁 |
| `tailscale_auth_key` | 注册使用的 auth key；填写后自动开启 Tailscale |
| `tailscale_hostname` | tailnet 中显示的 3DS 设备名 |
| `tailscale_state` | 持久化机器、WireGuard 与 DISCO 身份的文件 |
| `tailscale_autostart` | `1` = 连接前先拉起 Tailscale。注册成功时由 DSSH 自己写入，这样下次开机不必再填 auth key；想取消就设成 `0`（或删掉这行） |
| `config_version` | 由 app 读写的内容格式标记，不是给你手工设置的项，见下文说明 |
| `tailscale_control_url` | 控制服务器，默认使用 Tailscale SaaS |

### 多台机器

想连不止一台服务器，就把每组 `host` / `port` / `user` / `key_path` 加一个 `_N`
后缀（`N` = 0 … 7）。带后缀的键**优先**，不写后缀的那一组自动变成机器 0，所以
旧文件不用改：

```ini
name_0     = 家里的群晖      # 可选显示名；选择页列表里显示的就是它
host_0     = server-a.example.com
user_0     = ubuntu
key_path_0 = sdmc:/3ds/3dssh/id_a

name_1     = 编译机          # 名字里有 # 或 " 要加引号："a # b"
host_1     = server-b.example.com
user_1     = mac
key_path_1 = sdmc:/3ds/3dssh/id_b

selected_machine = 0     # 选择页默认高亮哪一台（超出范围会被夹回 0）
```

`name_N` 是可选的，最长 31 字节（更长的值由加载器截断）。不写名字的机器就和
加这个键之前一样，列表里用主机名代替。名字**可以在机器上直接改**——见
[SSH 机器选择](#ssh-机器选择多台机器)。

**机器编号要从 0 开始连续，不能留洞。**列表停在第一个空槽：写了 `host_0` 和
`host_2`、没写 `host_1`，那只算一台机器——否则选择页会把那个洞当成一行给你选，
点下去只有一个连不上去的主机名，看起来像网络故障。文件里的内容不会被动到，
被落下的机器有几台也会在启动画面上说清楚，但在你把编号补齐之前它们进不来。

配了两台以上，启动时就会先弹**机器选择页**（见 [SSH 机器选择](#ssh-机器选择多台机器)）。
Tailscale 是**全局**的，不加后缀，所有机器共用。

填写 `macos_keychain_password` 后，DSSH 会等待当前交互式 PTY shell 初始化完成，
然后执行
`/usr/bin/security unlock-keychain "$HOME/Library/Keychains/login.keychain-db"`。
只有检测到 macOS 输出 `password to unlock` 后才会通过 PTY 发送密码；不使用
`-p`，不会进入远端进程参数或 shell history。这满足 macOS Keychain 对交互式
会话的要求，并兼容 fish、zsh 和 bash。之后 DSSH 会清除内存中的密码副本。
即使解锁失败，普通 SSH shell 仍可正常使用，并会显示一条警告。之后再启动
`claude`，它就可以读取登录 keychain 中已有的 credential。

解锁成功时不会再显示诊断日志，DSSH 会清理 bootstrap 输出后再由 fish
重绘提示符。失败时只保留一条包含 unlock/verify 状态码的简短警告；密码
本身永远不会显示。

如果密码包含 `#` 或首尾空格，请加引号：

```ini
macos_keychain_password = "my # password"
```

> ⚠️ 此功能会把你的 **macOS 登录密码以明文保存在可移除的 SD 卡上**，其
> 敏感程度高于一把专用 SSH 密钥。建议尽量使用专门的 macOS 账户，妥善保管
> SD 卡；只有接受这一风险时才填写该字段。

### 原生 Tailscale 传输

Tailscale 支持由 [`cadl/libts3ds`](https://github.com/cadl/libts3ds)
提供。它是从 MicroLink 派生、面向 Nintendo 3DS 的实验性原生 C 客户端，
不是官方 Go `tailscale/libtailscale`，也不会运行 `tailscaled`。DSSH 通过
`libts3ds` git submodule 固定到经过真机验证的
[`libts3ds-v0.1.0`](https://github.com/cadl/libts3ds/releases/tag/libts3ds-v0.1.0)
源码；顶层 `make` 会自动先构建静态库。

开启后，SSH 使用 libts3ds 内部的私有 lwIP TCP 栈，可走直连 UDP、
Tailscale Peer Relay 或 DERP。默认构建自动选路；诊断构建可在编译时通过
`DSSH_TAILSCALE_PATH=direct`、`peer-relay` 或 `derp` 固定数据路径。

Tailscale 只在这两种情况下启动：配了 auth key，或者 `tailscale_autostart = 1`
写着用户要它启动——**除此之外都不启动**。`tailscale.state` 文件本身不构成启动
理由：它只是一个身份文件，而让"文件在不在"决定开机行为，意味着旧版本留下的
一份 state 会让之后每一次开机都先花几秒去做一件用户从没要过的事。注册成功时
DSSH 会自己写 `tailscale_autostart = 1`，所以即使你事后把 auth key 从文件里删
掉，已注册的节点下次开机仍然会回来；不想这样就把这行删掉或设为 `0`。
`tailscale_hostname` 默认是 `dssh-3ds`，`tailscale_state` 默认是
`sdmc:/3ds/3dssh/tailscale.state`。注册成功后 auth key 可以继续保留在配置中；
如果担心 SD 卡遗失导致 key 暴露，也可以手动删除。

卡上的两个文件各带一个格式标记——`config.ini` 里的 `config_version` 和
`settings.ini` 里的 `settings_version`——由 app 写入，不需要你管。它只为一件事
存在：保护一张**被更新版本 DSSH 用过**的卡。旧版本会照常读它（拒绝加载等于把
用户配的机器全丢了），但不会再写它，并在启动画面上说明原因——因为它不认识更新
版本的键。没有标记、或标记版本较低的，会在下次保存时原地补上。

state 文件包含机器、WireGuard 和 DISCO 私钥身份，必须和 auth key 一样按
敏感信息保护。本集成为非官方社区项目，与 Tailscale Inc. 无隶属或背书关系。


最终 SD 卡布局：

```
sdmc:/3ds/3dssh/
├── config.ini
├── id_rsa
├── theme.ini.example          # 可选；见下方「自定义主题」
└── tailscale.state            # Tailscale 注册后生成
```

---

## 语音输入

> **太长不看**：装 **API 轨**（一行 curl + 一个 API key、~30 KB 落地、
> 端到端 1-2 秒）。自部署轨道仓库里也带，但**对服务器要求高、不建议**
> ——见本节末尾的警告。

按住 **R** 录音 → 说一句中文 → 松手停止并发送；约 1-2 秒后
转写文字直接进 SSH 终端，效果如同您逐字打出来。整段中文只用嘴说就行，
软键盘可以省略。录音键从 v1.2 起固定是 **R**——更早版本用 **L**
做录音键、设置里「录音按键」可以在 L / START 之间切换；该方案已在
M11 退役，改成单一、跨机型一致的 R 入口。详见 [`KEYBINDING.md`](KEYBINDING.md)。

3DS 用内置麦克风录 16 kHz PCM mono，单次上传一整段录音（时长就是设置里
的「录音上限」15 / 30 / 60 秒）通过**复用现有 SSH
session 的第二个 channel** 上传（零新端口、零新认证、零防火墙改动），
服务器端的 shim 负责转写——`api` 轨走 OpenRouter，`local` 轨走本地
whisper.cpp daemon。

**状态指示**（软键盘左上角）：
- 🔴 **REC**（红色脉冲）— 录音中，同时下屏底边浮出一条随剩余秒数缩短的
  倒计时条（绿 → 剩 20% 黄 → 剩 10% 红）
- ⠋⠙⠹⠸（青色旋转）— 上传 + 转写中
- **ERR**（红色 2 秒）— 失败，再按录音键重试

### 推荐安装 — API 轨

语音功能在服务器端需要**两把 API key**。两把加起来日常使用月成本几美分：

| Key | 申请地址 | 用途 | 是否必需 |
|---|---|---|---|
| **OpenRouter** | [openrouter.ai/settings/keys](https://openrouter.ai/settings/keys) | Qwen3-ASR-Flash `qwen/qwen3-asr-flash-2026-02-10`（语音→文字）| **必需**——语音 IME 与 AI 问答 (R+START) 都用 |
| **DeepSeek** | [platform.deepseek.com/api_keys](https://platform.deepseek.com/api_keys) | DeepSeek-Chat（AI 回答）| 可选——只有 R+START AI 问答用；语音 IME 不需要 |

费用：OpenRouter 的 Qwen3-ASR-Flash 单价**约为 Whisper Turbo 的两倍**，
个人用一个月仍是几美分级别；具体价格以 openrouter.ai 实时报价为准，
不要照抄文档里的数字。DeepSeek-Chat 单次问答 `~$0.0001`。两边注册都送
少量免费额度，足够上手。

#### 一条命令安装

SSH 进服务器后：

```bash
git clone https://github.com/oli-bot/3DShell.git ~/dssh-repo
bash ~/dssh-repo/tools/install_whisper_api.sh
```

安装脚本会**交互式**地依次提示输入两把 key：

```
▶ checking prerequisites...
✓ python3 3.10
▶ writing track config...
✓ default track set to 'api' (use 'dssh-whisper switch' to change)

Need your OpenRouter API key (https://openrouter.ai/settings/keys).
Used for Whisper transcription.  Looks like:  sk-or-v1-...
Paste your OpenRouter key (or empty to skip): █

✓ api-key saved at /home/you/.config/dssh-whisper/api-key (chmod 0600)

Optional: DeepSeek API key (https://platform.deepseek.com/api_keys).
Used only by the R+START voice AI-ask modal — skip if you only
want plain voice IME.  Looks like:  sk-...
Paste your DeepSeek key (or empty to skip): █

✓ deepseek-key saved at /home/you/.config/dssh-whisper/deepseek-key (chmod 0600)
✓ dssh-whisper-api installed (lightweight, OpenRouter Qwen3-ASR-Flash).
```

**非交互式**安装（适合 CI / Ansible / 重装）：

```bash
OPENROUTER_API_KEY="sk-or-v1-..." \
DEEPSEEK_API_KEY="sk-..." \
bash ~/dssh-repo/tools/install_whisper_api.sh
```

#### 验证安装

```bash
$ dssh-whisper status
active track: api
(API-only install — no local daemon)
api-key:  ✓ /home/you/.config/dssh-whisper/api-key
```

3DS 每次录音都通过 SSH-exec 调用 `~/.local/bin/dssh-whisper-shim`；
服务器端**没有任何后续操作**——装一次终身使用。

#### 安装后服务器上的文件

```
~/.config/dssh-whisper/
├── track            # "api" 或 "local"
├── api-key          # chmod 0600 — OpenRouter
├── deepseek-key     # chmod 0600 — DeepSeek（可选）
~/.local/bin/
├── dssh-whisper          # CLI（start/stop/status/switch/uninstall）
└── dssh-whisper-shim     # 3DS 通过 SSH-exec 调这个
~/.local/share/dssh-whisper/
└── whisper_shim.py       # 实际跟两个 API 通信的 Python 脚本
```

总占用：~30 KB。无 daemon、无模型、无需运维。

#### 可选覆盖文件（安装器不会创建）

下面两个文件可以把 R+START AI 问答的请求指向另一个 OpenAI 兼容网关和
模型。**两个安装脚本都不写这两个文件**——只有你需要时才手工创建；文件
不存在时 shim 用内置默认值：

```
~/.config/dssh-whisper/
├── deepseek-url     # chat-completions 完整 URL
│                    #   默认：https://api.deepseek.com/chat/completions
└── deepseek-model   # 模型名
                     #   默认：deepseek-chat
```

适用场合：`api.deepseek.com` 在你的网络里不可达（部分运营商 / CDN 会返
回 403）时改走网关。纯文本、取第一行，shim 每次调用都重新读，无需重启。
只影响 AI 问答 modal，语音 IME 不涉及。

#### 后续轮换 key

key 泄漏 / 额度用完 / 换 provider 时直接覆写文件：

```bash
echo 'sk-or-v1-NEW_KEY' > ~/.config/dssh-whisper/api-key
chmod 0600 ~/.config/dssh-whisper/api-key
# 不需重启任何服务——shim 每次调用都重新读 key
```

| 项 | 值 |
|---|---|
| 推理 | OpenRouter Qwen3-ASR-Flash（云端）+ DeepSeek-Chat（云端）|
| 4 秒音频延迟 | ~1-2 秒（STT），AI 问答 +1-2 秒（LLM）|
| 费用 | 约为 Whisper Turbo 两倍 / 音频小时（个人用一个月几美分）+ ~$0.0001 / AI 问答 |
| 服务器占用 | ~30 KB |
| 服务器 CPU 负载 | 几乎零 |
| 是否需联网 | 是（HTTPS 到 openrouter.ai + api.deepseek.com）|

99% 用户用 API 轨即可——装好、按住 R 说话、收工。

### `dssh-whisper` CLI

```
dssh-whisper status                # 当前 track + daemon 状态 + key 是否在
dssh-whisper switch                # toggle api ↔ local
dssh-whisper switch [api|local]    # 显式指定
dssh-whisper start                 # 启动本地 daemon（仅双轨装态有效）
dssh-whisper stop | close          # 停本地 daemon
dssh-whisper restart
dssh-whisper logs [-f]             # tail systemd-user 日志
dssh-whisper uninstall             # 全卸载
```

API-only 装态下 daemon 相关命令会优雅退化（打印 "no daemon to ..."，非致命）。

### 快速 AI 问答（R + START）— v1.1

在 yazi 里忘了快捷键？想要一行 vim 正则？按住 **R** 同时按 **START**
—— 这下您是在问 AI、不是给 SSH 输入。底屏弹出一个问答 modal，顶屏的
SSH 会话完全不动。

| 在窗口里 | 效果 |
|---|---|
| **A** | 关闭，**保留**这条 Q&A 进 history；下次 R+START 接续上下文 |
| **B** 或下屏**任意触屏** | 关闭，**清空** history；下次 R+START 是全新对话 |

History 上限 5 轮；连续按 A 超过 5 轮会按 FIFO 丢掉最旧的一轮。

模型是 `deepseek-chat`（DeepSeek 官方 API，不经 OpenRouter）—— 选它
是因为响应快（~1-2 秒）+ 单价极低（个人用一次 ~$0.0001）。回答通常 6-15
句，长答案在窗口里自动换行，超长尾部用 `...` 截。

#### Modal 中的 markdown 渲染

DeepSeek 默认会用 markdown 排版。modal 把常见格式渲染成颜色，**不是**
显示原始符号：

| Markdown 源 | modal 渲染 |
|---|---|
| `# 标题` / `## 标题` / `### 标题` | 黄色强调，`#` 隐去 |
| `` `行内代码` `` | 青色，反引号隐去 |
| ` ``` …代码块… ``` ` | 青色多行，三反引号围栏隐去 |
| `- 项` / `* 项` | `• 项`（灰色项目符号 + 普通色内容）|
| `**加粗**` / `*斜体*` | 普通色 — syntax 隐去，无加粗 / 斜体效果（位图字体撑不起字形变化）|
| `[链接文字](url)` | `链接文字` — URL 丢弃 |
| `~~删除线~~` | 普通色 — syntax 隐去 |

#### DeepSeek key

需要单独配置一把 **DeepSeek API key**（在
[platform.deepseek.com/api_keys](https://platform.deepseek.com/api_keys)
申请）；安装脚本在 OpenRouter key 之外会再提示这把（可跳过——纯语音
IME 不需要它）。存放在 `~/.config/dssh-whisper/deepseek-key` chmod 0600
——如需轮换见上面 [后续轮换 key](#后续轮换-key) 段。

如果你的网络无法直连 `api.deepseek.com`（比如返回 403），可在同目录再建
`deepseek-url`（完整 chat-completions URL）与 `deepseek-model`（模型名）
两个文件，把 AI 问答指到任一 OpenAI 兼容网关；不建这两个文件就用官方端点。

### 注意事项

- 单次录音时长由设置里的「录音上限」决定（15 / 30 / 60 秒），硬上限
  ~63 秒（2 MB mic buffer 在 16.36 kHz × 16-bit 下满）。想提前结束：
  松开 **R** 即停止并发送。
- 退出 DSSH 用 **HOME** 键（不是 START）—— START 已绑定 AI 问答和弦。
- `~/.config/dssh-whisper/` 已在 `.gitignore` 里；轮换 API key 一行即可：
  `echo 'sk-or-v1-NEW' > ~/.config/dssh-whisper/api-key`
- 3DS 端通过 libssh2 exec 调 `~/.local/bin/dssh-whisper-shim`，shim 读
  当前 track 配置后分发——**切换轨道无需重启 3DS 或 SSH 会话**。

### 进阶 — 双轨（自部署，⚠️ 不建议）

> ⚠️ **请注意**：自部署轨道会把 `whisper-small` 模型加载进内存（约 1 GB
> 常驻），每次录音都跑一次 CPU 推理。在 2-vCPU 的 AWS t3.medium 上、
> 同时跑 VS Code Remote / claude-code / tmux / chrome-devtools-mcp 时，
> 转写 4 秒音频耗时 **~40 秒**——同一时刻走 OpenRouter 仅 **~1.5 秒**。
> 云端单价约为 Whisper Turbo 的 2 倍 / *音频*小时，个人用一个月仍是
> 几美分级别，除非您有强需求把音频留在本地，否则**强烈建议走云端**。
>
> 如果您还是要装本地轨，请确保服务器：
>
> - **4+ 颗空闲 vCPU**（≥3 GHz）——再低 3DS 上的 spinner 会让人难受
> - **2+ GB 空闲内存**给 small.zh 模型 + buffer
> - **转写时刻没有别的吃 CPU 的进程**（vscode、ide、playwright 等）
> - **~600 MB 磁盘**给模型 + venv
> - **有 systemd**——`install_whisper_dual.sh` 要注册 user service，没有
>   `systemctl` 会直接报 `systemctl not available. This installer assumes
>   systemd.` 退出。macOS 没有 systemd：拿 Mac 当服务器时双轨安装器跑不起来，
>   请留在 API 轨，或自己写一个 Launch Agent 跑 `tools/whisper_daemon.py`
>   （shim 只要求 `/tmp/dssh-whisper.sock` 这个 socket 在场）

如果您确实要离线部署，仓库里的双轨 installer 可以让 API 和本地共存，
随时切换：

```bash
git clone https://github.com/oli-bot/3DShell.git ~/dssh-repo
bash ~/dssh-repo/tools/install_whisper_dual.sh
```

双轨装好后**默认仍用 API**，需要时再切：

```bash
dssh-whisper switch local   # 下一次录音 → 本地 whisper.cpp
dssh-whisper switch api     # 下一次录音 → OpenRouter（默认）
dssh-whisper switch         # 不带参数即 toggle
```

---

## SSH 机器选择（多台机器）

`config.ini` 里配了不止一台机器时，DSSH 会**先让你选一台再连**——因为项目卡片、
主题、界面语言、吉祥物、录音上限都是**按机器存的**，不先确定是哪台机器就不知道
该加载哪一份设置。

```
启动 → 机器选择页 → 连接 → 项目页 → cd <目录> && <工具> <参数>
        （>1 台时）        （有卡片且 show_project_picker = 1 时）
```

- 只配了**一台**（或一台都没配）机器时这一页不出现，和以前一样直接连。
- 断线后按 **SELECT** 重连也会重新弹这一页，换机器不用重启应用。
- 下屏列表**一屏 5 行**，配了 6 台以上会滚动，高亮行永远留在屏内。每行是这台机器的
  **名字**在上、**地址**在下（`host`，端口不是 22 时是 `host:port`）——两行排版，
  长名字和长主机名不会挤同一行的宽度。`config.ini` 里没写 `name_N` 的机器保持原来
  那种居中单行 `1. host` 的样子。
- 上屏显示高亮机器的 `名称` / `主机` / `用户` / `端口` / `密钥` 和 `第几台 / 共几台`，
  每个值都裁到面板宽度——过长的主机名或密钥路径会省略号收尾，而不是画到屏幕外面去。
- 上下两屏都取**当前软键盘调色板**（`softkb_palette()`），所以这一页和其他页面一样
  跟随主题。给软键盘上色的 `theme.ini` 也就给这一页上了色，没有单独一套机器页主题。
- 选中的机器会写回 `config.ini` 的 `selected_machine`，下次启动默认高亮它——但
  **每次启动仍然会问**，不会替你决定。
- 每台机器一份设置文件：机器 0 用 `settings.ini`（兼容旧安装），机器 N 用
  `settings.<N>.ini`。切机器时那一份立刻生效，主题 / 界面语言 / 吉祥物 /
  录音上限都跟着换。

| 键 | 效果 |
|---|---|
| **D-pad ↑↓** | 移动选中项（循环；越过第 5 行时列表跟着滚）|
| **A** | 连接这台机器；连上后弹出项目页 |
| **B** | 取消。此时**还没有建立连接**，按 **SELECT** 可以重新打开这一页 |
| **X** | 给高亮的那台机器改名（见下）|
| 点按某一行 | 选中并直接连接（行与行之间的缝隙、以及底部那行提示文字都不算命中）|
| 点按 ✎ 条 | 给这一行改名——每行最右边 30 px。这条永远不会触发连接，所以纯触屏用户有一条和「点行即连」不冲突的改名入口 |

### 在机器上改名

按 **X**，或者点某一行右边的 **✎** 条，上屏会弹出一个预填当前名字的输入框，用软键盘
输入。**A** 或键盘的 ✓ 保存；**B** / **START** / **SELECT** 放弃。空名字是合法的保存
——它会把名字清掉，列表回到显示主机名。

保存只往 SD 卡的 `config.ini` 里写 `name_<N>` 这一个键：文件是逐行重建的，其余每个
字节（注释、空行、键的顺序、换行符）都原样保留。会让往返读写出错的字符——`#`、`"`、
反斜杠、首尾空格——会加引号写出去，因为加载器在解析之前会先剥注释、再 trim 空白。

列表里**其余字段仍然只能在电脑上改**：它们住在 SD 卡的 `config.ini`
（`/3ds/3dssh/config.ini`），机内没有主机 / 端口 / 密钥路径的编辑器（项目卡片有）。
写法见 `sd_template/3ds/3dssh/config.ini.example` 里的 `host_0` / `host_1` 段。

想直接看长什么样：`make preview-machine-page`。它把列表在**全部五套软键盘调色板**上
各渲染一遍，外加 8 台机器的滚动窗口、改名输入框和英文底栏，输出到
`design/machine-page/*.png`（用的是测试那套宿主桩，文字从真实字体图集里点出来）。
扫一遍主题是这个工具的重点：某个角色在 Tokyo Night 下读得清、在 Solarized 下读不清，
只渲一张图是看不出来的。

---

## 项目卡片（选目录 + agent 工具）

一张**卡片** = 一个目录 + 在这个目录里启动的终端 agent 工具。
存在的理由很直接：3DS 的软键盘很难打字，`cd ~/work/dssh && claude --continue`
这种事不该每次连接都手敲一遍。

三个入口会拉起这一页，而且拉起的**是同一个页面**，所以下面这套键在哪儿都一样：

1. **SSH 握手成功后**——当 `show_project_picker = 1`（默认）且至少有一张卡片时。
   页面在进入主循环**之前**就打开、由主循环驱动（旧版选择器自己套了一个内层循环，
   这也是它没法被复用、没法从别处再打开的原因）。
2. **`X + Y + ▲`**——每个新窗口自己挑一张卡片。这就是这个功能的意义所在：
   窗口 1 在 `~/a` 跑 `claude`，窗口 2 在 `~/b` 跑 `opencode`。
3. **设置 → 项目**——纯管理入口，UI 完全相同，只是 **A** 不会执行任何东西。

### 卡片列表（下屏）

五张 36 px 的卡片，方向键或触屏都行。上屏显示当前高亮卡片的信息：
目录、**将要发出的确切命令**、你自己写的备注、以及用过几次 / 最近什么时候用的。

| 键 | 效果 |
|---|---|
| **D-pad ↑↓** | 移动选中项（循环，卡片和末尾的 **+ 新增项目** 行一起转）|
| **A** | 在当前窗口执行选中的卡片。停在 **+ 新增项目** 行时改为打开编辑器 |
| **X** | 编辑选中的卡片 |
| **Y** | 删除——**要按两次**；第一次只是"待确认"（「删除」按钮文字会变），防止误按一下就把卡片抹掉 |
| **B** / **START** | 关闭本页 |
| 点按卡片 | 选中它；点按**已经选中**的那张则直接执行 |

### 编辑器（下屏）

四个分段 tab，上屏实时显示最终会执行的命令。

| 分段 | 内容 |
|---|---|
| **目录** | 卡片在哪个目录下运行。可以手输，也可以按「取用当前目录」直接取当前窗口的。另有自由填写的**备注**，显示在上屏 |
| **工具** | 用哪个 agent CLI。预设含 `claude` / `codex` / `opencode` / `agy` / `gemini` / `aider`，每个 chip 左上角是它自己的[标记](#工具标记)，也可以自己敲命令名 |
| **参数** | 所选工具的可选参数，点一下就勾上——`--dangerously-skip-permissions`、`--continue`、`--yolo`、`--approval-mode auto_edit` 等。**列表会随所选工具变化**，也可以手动追加任意内容 |
| **自定义** | 不分段，整行命令完全由你写。适合需要先 `source` 虚拟环境、导出环境变量、或串联两条命令的场景。**不会自动加 `cd`** |

| 键 | 效果 |
|---|---|
| **D-pad ◀▶** / **L** / **R** | 切换分段 |
| **D-pad ▲▼** | 在当前分段内移动 |
| **A** | 对当前行执行动作——选工具、勾参数、取用当前目录，或开始输入 |
| **B** | 保存并回到列表 |
| **START** | 保存并关闭本页 |

文本字段借用下屏软键盘。从「自定义」切回某个分段 tab 是**非破坏性**的：
你手写的那行还留在 `cmd` 里，切回去就还在；但**实际执行的是三段式的结果**，
因为表单上显示的就是三段式。

### 工具标记

每个已知 agent CLI 带一枚 16×16 的像素标记（`proj_logo_t`，数据在
`source/project_presets.c`）：claude 是放射状爆点、codex 是六边形、opencode 是
终端窗口、agy 是起飞的箭头、gemini 是四角星、aider 是两条 commit 合并成一条、
自定义是一支铅笔。标记出现在三处——工具页每个 chip 的左上角、卡片列表每张卡片的
左侧、以及上屏标题旁边（2× 放大，编辑器里则是右上角，标的是当前草稿跑的工具）。
所以哪张卡片跑哪个 CLI，不用读名字就能看出来。

标记是**照着各工具的气质自己画的，不是任何人的商标**：16 px 画不下真 logo，
把别人的商标塞进自制程序里也不合适。配色也不按品牌走，而是取所在表面的主文字色
（`'#'` 实心、`'+'` 半透明），这样任何一套键盘主题下都可读——浅色主题尤其重要，
白底上画白色 logo 就是一片空白。

`raw=1` 的卡片全页都标成「自定义」，所以它的标记也是铅笔，而不是它 `tool`
字段里残留的那个名字。数据形状由 `make test-project-presets` 盯着（每个槽位都有
标记、网格必须是矩形、两个工具的标记不能一模一样），画到屏幕上的位置和颜色由
`make test-project-ui` 逐像素校验——那套测试带自检，会先故意验错三遍，确认校验器
真的会失败，再验对的。

想直接看长什么样：`make preview-project-page`。它用同一套宿主桩把列表、工具页、
参数页连同上下屏渲染成 `design/project-page/*.png`（文字是从真实字体图集里点的，
不是占位符），改完布局不用刷机就能看。

### 「当前目录」是怎么来的

两个来源，按这个顺序：

1. **DSSH 最后一次让这个窗口的 shell `cd` 到的地方。** 这是 ground truth，
   因为是 DSSH 自己打的。
2. **走辅助 exec 通道跑一次 `pwd`。** 这里有个必须说清的语义：第二条 SSH 通道
   会起**自己的**进程，落在**登录目录**，所以那里的 `pwd` 回答的是
   「一个新 shell 会落在哪」，而不是「这个窗口的 shell 飘到哪了」。
   对刚开出来的窗口这正好是对的，对你已经 `cd` 过很多次的 shell 就是错的——
   所以 (1) 优先。**DSSH 不监听你手打的 `cd`**，所以你自己 `cd` 走之后，
   这一页是不知道的。

探测失败时按钮会写「探测失败，点此重试」，点一下重试。

### 存储格式

卡片存在 `settings.ini` 的 `[projects]` 段：

```ini
[projects]
count = 2
selected = 0
project_0 = work|/home/me/work|claude|--continue|0||my main repo|12|1757836800
project_1 = venv|/srv/app|||1|source .venv/bin/activate && aider|needs the venv|3|1757830000
```

字段为
`name|path|tool|args|raw|cmd|note|uses|last_used`，`\|` 与 `\\` 会转义，
所以命令里写管道没问题。`raw=1` 表示执行的是 `cmd`，`path`/`tool`/`args` 被忽略。

**旧格式（4.0 之前）会自动迁移。** 旧格式是 `name|path|cmd|raw`；
形状是按**未转义的** `|` 数量判定的，所以已有文件不改也能继续用，
`raw=0` 的行会把原来那条命令字符串无损拆成 tool + args
（迁移前后拼出的命令逐字节相同）。你下次保存任何东西时就会写成新格式。

往返与迁移由 `make test-config-projects` 覆盖，预设表由
`make test-project-presets` 覆盖，页面自己的状态机由 `make test-project-ui`
覆盖。

## 按键说明

### 物理键

| 键 | 功能 | 备注 |
|---|---|---|
| **A** | Enter（EN）/ **拼音 buffer 当英文直出**（IME） | CN 模式下不小心打了英文？按 A 直接把 buffer 作为 ASCII 发出去并清空，不用切换模式重打 |
| **B** | Backspace / 消拼音 buffer | hold-style 自动重复（最快 60/秒） |
| **X** | Alt 修饰 | hold-style，按住时下次按键加 Alt |
| **Y** | Ctrl 修饰 | hold-style，按住 Y + 点 c → Ctrl-C |
| **X + Y + ▲** | **新建窗口** | 在**同一次 SSH 登录**上开第 2（3、4）条 shell channel。两个修饰键必须同时物理按住——粘滞锁存的 X/Y 不算。详见下面 [多窗口终端](#多窗口终端) |
| **X + Y + ▼** | **关闭当前窗口** | 直接拆掉那条 channel——远端进程收到 `SIGHUP`，所以全屏 TUI 吞掉 stdin 也照样关得掉。只剩一个窗口时无效 |
| **X + Y + ◀** 或 **X + Y + L** | 上一个窗口 | 绕回；这次按键会被吞——`←` 不会发给远端，L 也不会触发双屏。方向键本来就不带修饰键编码（`X + ◀` 也只是一个普通 `←`），所以改义的只有"两个修饰键同时按住"这一种组合 |
| **X + Y + ▶** 或 **X + Y + R** | 下一个窗口 | 同上、方向相反；R 在这次按下不会起录（和弦把 held 位偷走了）|
| **R** | **按住说话录音** | 按住即起录、松手即停止并发送。**R + START** = AI 问答。详见 [语音输入](#语音输入) |
| **R + START** | **AI 问答 modal** | 按住 R、点 START——录音后答案弹在底屏 modal。详见 [语音输入](#语音输入) |
| **L** | （空闲）/ **+ START → 双屏一体开关** | L 单按空闲。作为 **START + L** 的修饰键把上下屏拼成一块 66×40 大终端（详见 [双屏一体终端](#双屏一体终端)）。L 已经不是 Shift 修饰键：上档字符在软键盘的 ⇧ 上 |
| **START** | **+ R → AI 问答** | START 单按空闲。组合「按住 R」触发 AI 问答 modal；组合「按住 L」触发双屏一体开关。退出 DSSH 用 **HOME** 键 |
| **SELECT** | Esc | 单点立即发；SSH 会话死链时（例如合盖睡眠后）触发重连 |
| **拼/EN**（软键盘）| **切换中/英 IME 模式** | 取代原 BSP 软键。底栏状态条会闪 **C** / **E** 提示标签（目标模式——物理 L 是 **START + L** 双屏修饰，本身不切换 IME）。详见 [输入法用法](#输入法用法) |
| **Space**（软键盘）| 普通空格（EN）/ **提交当前高亮候选**（IME） | 跟 sogou/fcitx 一致 |
| **。**（软键盘）| **。**（全角句号 U+3002） | 中文符号页（**123**）上的独立按键。不是 `Shift + .` 的产物：字母页压根没有 `.` 键，而符号页与 Shift 无关（键面 = 点下去发什么）|
| **⇧**（软键盘）| 一次性上档：把下一个字母变大写（`a` → `A`）。长按 = CapsLock | 只作用于**字母页**。符号页与 Shift 无关——那里的键面就是点下去发出的字符，所以 `{ } "` `\|` `_` `:` `?` `~` 各自独立成键，而不是 Shift 配对 |
| **D-pad ↑↓** | 方向键 / IME 翻页 | IME buffer 非空时翻候选页；按住 **X + Y** 时这两个键改成新建/关闭窗口 |
| **D-pad ←→** | 方向键 / IME 选词 | IME buffer 非空时移动候选游标；按住 **X + Y** 时改成切换窗口 |
| **Circle Pad ↑↓** | 滚动 scrollback / 全屏程序翻页 | 普通 shell 下滚的是 DSSH 的**本地 scrollback**；远端处于全屏程序（opencode、htop、vim…）时改发 **PageUp / PageDown**——这类程序原地重绘，内容不会滚出屏幕，本地缓冲区永远是空的 |

> **完整按键模型**在 [`KEYBINDING.md`](KEYBINDING.md)——那里是权威、跨机型一致
> 的速查（每个键在每款 3DS 型号上都是物理按键）。上表是 README 内的精简版。

> 长按 D-pad 或 B 键（「标准」档）：250ms 启动重复，随后 12/秒，0.75s 起
> 30/秒，1.75s 起 60/秒。

### 项目卡片页

[项目卡片页](#项目卡片选目录--agent-工具)打开期间**独占整个键盘**——
**X + Y** 窗口和弦、**START+L** 双屏切换、R 的语音起录全部失效，
方向键也不会往 shell 里发箭头。它自己的键很少：

| 键 | 效果 |
|---|---|
| **D-pad ↑↓** | 移动选中项 |
| **D-pad ◀▶** / **L** / **R** | 切换编辑器分段（编辑器内）|
| **A** | 执行选中卡片 / 对表单当前行执行动作 |
| **X** | 编辑选中卡片 |
| **Y** | 删除——要按两次（第一次只是待确认）|
| **B** | 保存草稿并返回；在列表页则关闭本页 |
| **START** | 保存并关闭本页 |

这一页上的所有东西也都能用触屏操作——卡片列表、四个分段 tab、
工具与参数预设、以及 保存 / 取消 按钮。

### 双屏一体终端

按 **START + L** 把两块屏幕拼成**一块 66×40 的连续终端**：SSH 会话被
实时放大到 40 行高，上屏显示 0-19 行（和普通模式完全一样），下屏显示
20-39 行。看长输出（`less`、`git log`、build 日志）或全屏工具
（`vim`/`htop`/`top`）时视野翻倍。再按一次 **START + L** 缩回 66×20、
恢复软键盘。

| 操作 | 效果 |
|---|---|
| **START + L** | 进入/退出双屏一体终端（SSH PTY 实时重协商，shell 自动重绘） |
| Circle Pad ↑↓ | 在双屏一体终端里整体滚动 scrollback |

- 进入/退出时远端 shell 会收到 SIGWINCH 并按新尺寸重绘一次，属正常现象。
- 下屏只有 320 px 宽，放不下 66 列（396 px），所以下屏的 20 行会把 66 列
  **横向压扁到 ~0.81 倍**塞进整块下屏——**一个字符都不丢、不裁剪**，代价是
  下屏文字比上屏窄一点、边缘略柔。上屏保持满宽清晰。
- 当远端进入**全屏 TUI**（opencode、vim、htop 等一切切换到 alternate
  screen 的程序），下屏会自动从"压扁"切换到**居中裁切**：裁掉左右边缘列，
  中间 53 列按完整 6 px 渲染，字体和上屏一样清晰。设置列表（下屏布局）可以
  在四种行为里强制指定（**AUTO / SQUEEZE / CENTER / TRIM**）——
  **TRIM** 只裁左 2 列右 3 列，剩余 61 列按 ~0.87 倍压扁（介于 SQUEEZE
  和 CENTER 之间的折中）。
- 双屏模式下没有软键盘（那是你等下要打字的对象），切回来再输入。
- 断线重连会自动恢复你当前的双屏状态。

### 多窗口终端

一次 SSH 登录，最多**四个交互式 shell**。新建窗口**不是新建连接**：它在已经
认证好的那条 session 上再开一条 channel，所以没有第二次握手、没有第二次
keychain 解锁、也没有第二条 Tailscale 隧道，keepalive 更是四条窗口共用。

| 操作 | 效果 |
|---|---|
| **X + Y + ▲** | 新建窗口并切过去（新 shell 按你当前视图的尺寸打开） |
| **X + Y + ▼** | 关闭你正在看的窗口，焦点退回还活着的窗口 |
| **X + Y + ◀** / **X + Y + L** | 上一个窗口（绕回） |
| **X + Y + ▶** / **X + Y + R** | 下一个窗口（绕回） |
| 在某个窗口里 `exit` | 同样会关闭它——远端 shell 自己退出、channel 结束、窗口自毁 |

- **关闭是拆 channel，不是替你打 `exit`**。DSSH 发 EOF 并释放那条 channel，
  远端 PTY 随之销毁、进程收到 `SIGHUP`。这是有意的：`opencode` 这类全屏 TUI
  占着 stdin，会把 `exit` 当成输入吃掉；卡死的进程也没法礼貌地请它退出。
  代价是那个窗口里还在跑的东西会一起死，包括没保存的东西。
- 和弦在**最后一个窗口上无效**——那条 shell 就是会话本身，要结束请用 `exit`
  （退出后会落到和断线一样的「按 SELECT 重连」提示）。
- X 和 Y 在**所有型号**上都是实体键，所以这套组合键老 3DS 和 2DS
  一样能用——与 **START + L** 双屏触发同样跨机型一致。两个修饰键必须
  同一时刻按住——粘滞锁存的 X 或 Y 点不亮和弦；而且和弦会把第三个键
  一起吃掉，所以 `X+Y+L` 不会触发双屏、`X+Y+R` 也不会起录、
  `X+Y+◀` 更不会变成 `←` 发给远端。方向键的"吃掉"会一直持续到它物理
  松开——方向键连发是看 `held` 的，所以一次按下只翻一个窗口，按住
  不放不会一路翻过去。
- 看不见的窗口**不会被暂停**。每一帧所有窗口都会被读取，后台的 `opencode`
  不会卡在 SSH 流控上继续跑；焦点窗口拿到每帧读取配额的较大头。
- 窗口数大于 1 后，底栏会多出一条 `1·2●·3` 窗口条——焦点数字用主题的正文
  主色，后面带 `●` 表示那个后台窗口产生了你还没看过的输出（切过去即清除）。
  这个点只认**那个窗口自己收到的数据**，本地切视图引发的重绘不算。即使把
  **底栏信息**设成「隐藏」，窗口条照样画。
- **START + L 双屏是全局的**（效果上），但它只重谈**你正在看的那个窗口**
  的尺寸：改尺寸＝SIGWINCH，后台那些 TUI 一旦被叫醒就会整屏重绘，看着就
  像"有未读输出"。看不见的窗口保持你上次看它时的网格，你翻过去的那一帧
  才重谈到当前视图尺寸。双屏模式下没有底栏，窗口条改成浮在下屏右上角的
  小 chip。
- 设置页打开期间整块键盘归它——包括 X 和 Y——所以窗口和弦在页内无效，这是
  设计如此。AI 问答 modal 同理。
- 切窗口会丢弃**打了一半的拼音 buffer**（否则提交时会打进你刚切到的那个
  shell）。中英文模式、键盘页、⇧ 状态是跨窗口共用的——它们属于 UI 状态，
  不是流状态。
- 进行中的语音听写会**钉在开始它的那个窗口**，中途翻标签不会把中文打进
  错误的 shell。
- 服务器侧注意两点：sshd 的 `MaxSessions`（默认 10）要留够额度——四条 shell
  外加语音子系统借用的一条；以及如果你的登录 shell 会自动起 `tmux`，那四个
  "窗口"就变成同一个 tmux 会话的四个客户端，而不是四个独立 shell。

### 软键盘

下屏占满软键盘，三页：

- **字母页**（默认）：QWERTY，另有 ⇧、**拼/EN** 切换、`123` 翻页键、Tab、
  大空格与 ↑ 发送键。**这一页没有任何标点**——连 `.` 和 `,` 都没有，所以
  `。` 才会单独占中文符号页的一个键，而不是某个 Shift 配对。
- **符号页 1**（左下 `123` 切换）：第一行 1234567890，第二行
  `- / : ; ( ) $ & @ "`（EN）或 `- / : ; ( ) ¥ @ “ ”`（CN），第三行
  ``. , ? ! ' ` ``（EN）或 `。， 、 ？ ！ .`（CN）。
- **符号页 2**（在符号页 1 上按 `#+=`）：编程符号集——
  `[ ] { } # % ^ * + =`，接着 `_ \ | ~ < > € £ ¥ •`。shell 测试与重定向要用的
  ASCII `[] <>` 都在这里。

每一页的最底行都保留 Tab、大空格与 ↑。四个符号页变体（页 1 / 页 2 ×
CN / EN）全部**与 Shift 无关**：⇧ 和 CapsLock 在那里不起作用，键面就是
点下去发出的字符。

任何键支持 hold-style 修饰组合，例如：**按住 Y + 点 b** = `Ctrl-B`
（tmux prefix）。

### 状态条（顶部 30 px）

```
┌──────────────────────────────────────────────────────┐
│ [SFT]   候选条 / 拼音 buffer / IME 候选词       [⚙]  │
└──────────────────────────────────────────────────────┘
```

- **左槽 [STA]**：3 字母指示当前修饰键（SFT/CTL/ALT 持续亮，
  ENT/BSP/ESC/`C`/`E` 闪 200ms）
- **中段**：CN 模式拼音 buffer + 候选词；EN 模式空白
- **右槽 [⚙]**：设置按钮，**单击此处** 打开双屏设置页（在其中可进入
  Debug 页面）；按 **B** 或再点一次徽章关闭

---

## 输入法用法

### 全拼

CN 模式下按字母键 → 顶部候选条出现拼音 + 候选：

```
ni       → 年 你 牛奶 娘 念   (page 1/52, total 256)
nihao    → 你好 你好吗 你好啊 拟好 你好呀
shijie   → 世界 世界上 世界杯 世界各地 世界里
```

- **A 键** 或 **空格键** 提交当前高亮候选
- **D-pad ←→** 移动高亮（同一页内）
- **D-pad ↑↓** 翻页
- **触屏** 直接点候选词提交
- **B 键** 删拼音 buffer 一个字母

### 缩写（声母）

每个多音节词都额外有一条声母拼音条目，**输入声母也能召回**（频率打 0.3
折，所以全拼仍排在前）：

```
nh → 你好（在第 8 个候选）
wm → 我们（首个）
sj → 世界
zw → 中文
xx → 谢谢
```

按 D-pad ↓ 翻页或 ←→ 移动游标找到目标后 A 键提交。

### Prefix 回退

输错字母多打了一个？引擎自动用最长有效前缀匹配，红色显示多出的部分：

```
buffer:  niha[oz]    ← 绿色 niha + 红色 oz
候选:    依旧显示 nihao 的结果
```

按 B 键消掉红色尾巴回到正常。

### 修饰键不被 IME 拦截

CN 模式下按住 **Y + c** 仍然发 `Ctrl-C`，按住 **X + f** 仍然发 `Alt-F`
（ESC + `f`）。修饰键优先于 IME 路由，跑 vim/tmux/claude-code 不受影响。
大写已经不是物理修饰键了——由软键盘的 **⇧** 切换。

### 误打英文逃生：A 键直出

CN 模式 + 拼音 buffer 非空时，按 **A** 把 buffer 当英文 ASCII 发到 SSH
并清空。例如不小心在 CN 模式打了 `cd /etc`：候选条会显示一堆奇怪中文，
此时按 **A** 一次，SSH 直接收到 `cd /etc`，不用退格再切模式重打。

> 区别：**Space** 提交高亮候选（中文上屏），**A** 直接把字母原样发出去。

---

## 设置与 Debug 页面

点击右上角 **⚙** 打开（按 **B** 或再点一次徽章关闭）。设置页**占用上下
两个屏幕**，仿 3DS 系统设置：上屏无触摸，放可导航的列表；下屏是唯一触
摸面，放选中项的详情卡：

- **上屏**：**当前分页**的条目列表，每行右侧显示当前值，高亮条标出选中
  行；底部一行暗色文字重复按键模型（根页与分组页的提示不同，因为 B 在
  两处含义不同）。
- **下屏**：选中项的详情卡——标题、当前值、一两行说明（这一项是干什
  么的），以及**每个选项一个可点按的实体按钮**（当前值用强调色高亮）。

| 键 | 作用 |
|---|---|
| **D-pad ↑↓** | 移动选中行（循环；长按自动重复） |
| **D-pad ←→ / A** | 切换选中项的取值（循环）；在「调试页面」上按 A 进入 Debug 屏 |
| **L / R** | 在分组之间步进——外观 → 输入 → 语音，循环。只在分组内有效，根页不算其中一个 |
| **B** | 退回上一层（分组 → 根页）；在根页上则关闭设置 |

设置页打开期间整个键盘归它独占：R 不会开始录音、START 不会触发
AI 问答（R + START）、START + L 不会切双屏、方向键不会发给远端、
X + Y 窗口和弦也无效。（Debug 子页是另一页——它不占键盘，所以
窗口和弦在那儿能用。）选中位置在开关之间保留。

设置项按真实分页排列。根页有五行，其中三行是**分组**——按进去打开子页
面，**B** 退回根页。（调试入口不是分组：它打开的是另一页，不占用键盘。）

- **外观** `[ 进入 ]` —— 主题、界面语言、底栏信息、吉祥物与下屏布局。
- **输入** `[ 进入 ]` —— 修饰键、按键音效与重复速度。
- **语音** `[ 进入 ]` —— 录音上限与录音按键。
- **项目** `[ 进入 ]` —— 打开[项目卡片页](#项目卡片选目录--agent-工具)。从这里进是**纯管理**：**A** 不会执行任何卡片（执行请用窗口），但新增 / 编辑 / 删除都能做，改完立刻写进 `settings.ini`。这一行和连接后弹出的选择器拉起的是**同一个页面**，所以两处按键完全一致。
- **调试页面** `[ 进入 ]` —— 打开下面描述的 Debug 页面。

**外观** —— 本分组五行，按顺序为：

- **主题**：`[ 默认 (Tokyo Night) / 黑客绿 (Matrix) / 黑白 (Mono) /
  深海 (Solarized) / 自定义 (Custom) ]`（高对比度语法高亮与主题；原名
  「终端主题」，现随
  上屏终端一起重绘**整个下屏**——键帽与字符、状态行、IME 候选条、设置
  页、Debug 页、底栏文字与下屏背景色）。设置页本身就用主题调色板绘
  制，**切主题时新配色当即在本页上预览**。有意**不**跟随主题：吉祥物
  像素本体（它们是各物种的识别特征）、AI 问答弹窗、语音徽标的
  REC/转圈/错误状态色。配色表在 `source/kb_theme.c`，host 端
  `make test-theme` 校验（默认主题逐字节回归 + 各角色 alpha 位 + WCAG
  对比度下限）。第 5 项 **自定义 / Custom** 不是内置配色，而是读取 SD
  卡上的 `theme.ini`（见下文 [自定义主题（theme.ini）](#自定义主题themeini)）。
- **界面语言**：`[ 中文 / English / 日本語 / 한국어 / Deutsch /
  Français ]` —— **程序里每一个标签**的语种：设置行、键帽、图例、弹窗
  按钮。六个选项，是所有分页里最宽的一行。与下屏的 **拼 / EN** 键无
  关——那个切换的是你**打**什么，不是程序**说**什么。它排在本分组第二
  位是有意的：看不懂当前语言的人，得能在第一个分组靠上的位置找到出路。
- **底栏信息**：`[ 时钟电量 / 仅时钟 / 隐藏 ]` —— 实时 3DS 电池百分比
  与充电状态监控；只有电量数字会变色（<20% 黄、<10% 红），并在充
  电时改为绿色（"+百分比" 的形式），色调随当前主题偏色，时间用主题的
  暗色文字色（随主题变化，始终弱于电量数字）；选 **隐藏** 时整条底栏
  只保留吉祥物，吉祥物可移动区域始终按左侧信息实际宽度动态收缩，二
  者不会重叠。
- **吉祥物**：`[ 关 / 螃蟹 / 蟹特Crab / 坤坤 / 菌菌 / 大眼 / 牛来 / 刀盾狗 ]` ——
  底栏像素宠物（经典螃蟹 / 蟹特Crab / 坤坤 / 噬菌体 / 外星人 / 牛来 / 刀盾狗 七个物
  种）。每个物种还有各自的按键音效。
- **下屏布局**：`[ 自动 / 压缩 / 居中 / 裁边 ]` —— **START + L** 双屏
  模式下屏的渲染方式；自动＝按远程应用智能切换。四种模式含义：
  **AUTO**（默认）＝全屏 TUI 时居中裁切、普通 shell 时压扁全部 66 列；
  **SQUEEZE**＝始终压扁全部 66 列；**CENTER**＝始终裁到中间列按完整
  6 px 渲染；**TRIM**＝裁掉左 2 列右 3 列，剩余 61 列按 ~0.87 倍压扁。

**输入** —— 三行：

- **修饰键**：`[ 按住 / 粘滞 ]` —— **Y(Ctrl) 和 X(Alt) 的触发方式**：
  按住＝必须与下一个键同时按住；粘滞＝点按一下即锁定，单手触控笔操作
  更轻松。
- **按键音效**：`[ 关闭 / 开启 ]` —— 触屏打字物理按键反馈音效。
- **按键重复速度**：`[ 慢 / 标准 / 快 ]` —— 按住一个键时连续触发的速
  度。这个档位同时管三个地方：物理 D-pad、**B** 退格、以及按住的软键
  盘按键。长按 **⇧** 锁定大写的判定不随此设置改变。

**语音** —— 两行：

- **录音上限**：`[ 15秒 / 30秒 / 60秒 ]` —— 语音/问答录音时长上限；
  60 秒贴合 2 MB 麦克风缓冲并留有余量，~63 秒硬上限自动钳制。录音进行
  时，下屏最底边会浮出一条以本项设置值为起点的倒计时条，随剩余时间缩
  短——绿、最后 20% 转黄、最后 10% 转红；松手或被上限截断即消失。
- **录音按键**：`[ R = 按住说话 ]` —— 单一、跨机型一致的录音入口：
  按住 **R** 起录、松手即发送。按住 **R** 同时点 **START** 把同一段录
  音升级为 AI 问答。旧版本设置里暴露 `[ START / L ]` 让用户在两个方
  案之间切换——这个切换已经在 M11 废弃、改成单一 R 入口，所以这一行
  现在是绑定提示，不再是可调控制：它唯一的按钮点下去不做任何事。

另外两条适用于整页的说明：

- **持久化**：设置改动即时落盘到 `sdmc:/3ds/3dssh/settings.ini`（与
  `config.ini` 同目录）；删掉该文件即恢复默认。host 端可
  `make test-config` 往返校验。自定义配色**不**存在这里——
  `settings_save` 会整体重写该文件，手写内容会被抹掉；配色存在
  `theme.ini`（应用永不写它，见下文）。
- **Debug 页面**：从设置列表选中「调试页面」按 **A**（或点 [ 进入 ]）
  进入，点击右上角 ⚙ 即可退出：
  - 标题 + 退出提示（再单击右上 ⚙ 即退）
  - **recv hex**：最近 32 字节 SSH 接收数据，用来排查 ANSI 协议问题
  - **物理键绑定列表**：按键说明的速查
  - 页面为纯展示：原来的 **MASCOT / BOTTOM** 切换按钮已移入设置列表
    （吉祥物、下屏布局两项）。

### 自定义主题（theme.ini）

**自定义 / Custom** 主题项不是内置配色，而是读取 SD 卡上
`sdmc:/3ds/3dssh/theme.ini` 的内容。可以从仓库里的
`sd_template/3ds/3dssh/theme.ini.example` 开始（和
`config.ini.example` 一样手动复制到 SD 卡）：

```
sdmc:/3ds/3dssh/
├── config.ini
├── theme.ini            # 可选；自定义配色
└── theme.ini.example    # 全量键名参考，一并复制
```

每个键都可省略——缺省键保留默认配色，所以只改一个角色的一行文件
也能用。文件接受全部 23 个下屏角色（`bottom_bg`、`key_body`、
`label` …）以及上屏的 `term_bg`、`term_fg` 与 `ansi0`–`ansi15`；
值写 `RRGGBB` 或 `RRGGBBAA`，大小写不敏感。两条刻意为之的规则：

- **`#` 只在行首才是注释。** 不支持行内注释——正是为了让
  `bottom_bg = #1a1b26` 这种写法成立；若按 config.ini 的规则把任意
  位置的 `#` 当注释起点，色值会被整个吃掉。
- **alpha 字节被忽略。** 应用强制使用内置主题对三个半透明角色
  （`key_special`、`hold_bg`、`cand_sel_bg`）设定的透出程度，其余角色
  一律不透明——这样无论粘贴什么进来，分层观感都不会被破坏。

未知键与无法解析的值会被逐个拒绝：其余内容照常生效，**自定义**
选项下方那一行会报告被拒绝的条数（启动时终端里也有一条同文案的暗色
提示）。文件在**启动时读取一次**，改完文件后把主题从自定义切走再切
回（或重启应用）即可重新读取。和其余主题一样重绘双屏，但同样不会
染色吉祥物像素、AI 问答弹窗与语音徽标的状态色。解析器有 host 测试
（`make test-theme-ini`）。

---

## 已知限制

这几样东西本应用还不提供，摆在明处免得踩坑：

- **完全不校验服务器主机密钥**：对端递来什么主机密钥都照收——不弹提示，没有
  `known_hosts`，也没有能固定指纹的地方。任何能在中间终结这条连接的人
  （开放 Wi-Fi、不由你控制的中继、不由你控制的 Tailscale 出口节点）都能冒充
  你的服务器：会话内容他全看得到、也全写得出来，你敲的字符是发给他的，不是
  发给你以为在用的那台机器。macOS 钥匙串自动解锁走的是同一扇门——它的触发条件
  是「终端里出现解锁提示」，而假服务器可以自己印一个。私钥不会离开 SD 卡，
  所以攻击者拿不到你的登录凭据；代价是这条会话不再保密，而且屏幕上的内容
  由他决定。请只在你能控制的网络上使用，或走 tailnet 连接。
- **无剪贴板 / 复制粘贴**：scrollback 只读——不能选中复制终端里的文字，
  也不能把内容粘贴给远端。需要把整段文字「贴」进终端时，目前的变通办法
  是用语音输入念出来。
- **仅公钥认证**：mbedTLS 后端的限制，只认 RSA（PEM 格式）私钥；没有
  密码登录、没有 ed25519，密钥不可用时也没有降级路径。
- **机器列表只能在电脑上改**：`config.ini` 支持最多 8 台机器（`host_0` … `host_7`，
  启动时会弹机器选择页），但连接信息在机内没有编辑器——加一台机器要改 SD 卡上的
  文件。**名字是例外**：选择页可以直接改 `name_N`，因为那正是你看着列表时想改的
  那一个字段。而且一次只连一台：换机器是重连（或重启后重选），不是并行会话。
- **只有设置页有 6 种界面语言**：设置页是 zh / en / ja / ko / de / fr，机器选择页、
  项目页这些只有中英两栏，其他语言落回英文。
- **无文件传输**：没内置 SFTP/SCP；3DS 与服务器之间传文件得借远端
  `git` / `curl` 之类的路子。
- **项目卡片不跟踪你手打的 `cd`**：「取用当前目录」知道的是
  **DSSH 自己**最后一次把本窗口送到的位置（`cd <卡片目录>`），取不到时
  用新 shell 会登录到的目录。它不监听你敲了什么，所以你自己 `cd` 走之后，
  这一页仍然记着上次卡片把它放在哪。不会执行到错的地方——只是建议会偏。
- **agent 参数预设会过时**：「参数」tab 里那张表是各工具**当时**文档化参数的
  快照（`source/project_presets.c`）。某个工具改了参数名不会有人提醒你——
  「自定义」tab 是逃生出口，`make test-project-presets` 至少把表的形状钉住了。

---

## 从源码构建

### 依赖

- Linux x86\_64（在 Ubuntu 22.04 测过；其他发行版需要相应调整）
- [devkitPro / devkitARM](https://devkitpro.org/wiki/Getting_Started)
  release 65+，GCC 14.2.0。跑 `make` 的那个 shell 只需导出 `DEVKITPRO`
  一个变量（标准安装的 `/etc/profile.d/devkit-env.sh` 就是干这个的）；
  `DEVKITARM` 会由它推导为 `$DEVKITPRO/devkitARM`，也可以显式导出覆盖。
  两个都没设时 `make` 会直接停并给出一行写明修复办法的提示
- Python 3.10+ + Pillow（生成字体 + 词典；`make cia` 也会自己找一个装了
  PIL 的解释器，找不到会直接报出修复办法）

### 步骤

```bash
# 1. 装 devkitPro
wget https://apt.devkitpro.org/install-devkitpro-pacman
bash install-devkitpro-pacman
sudo dkp-pacman -S 3ds-dev 3ds-mbedtls 3ds-libpng 3ds-zlib

# 2. 递归 clone，同时拉取 libts3ds 及其私有 lwIP
git clone --recurse-submodules https://github.com/oli-bot/3DShell.git
cd DSSH

# 仅已有的非递归 clone 需要执行：
git submodule update --init --recursive

# 3. 交叉编译 libssh2（一次性，输出到 $DEVKITPRO/portlibs/3ds/lib/）
bash build-libssh2.sh

# 4. 装系统字体（Terminus 提供 ASCII / box-drawing）
sudo apt install fonts-terminus

# 5. 下载字体源（Zpix）
bash tools/fetch_fonts.sh

# 6. 生成字体 atlas（→ source/font_data.c，~3 MB）
python3 tools/gen_font.py

# 7. 下载 + 生成拼音词典（→ romfs/pinyin_dict.bin，~13 MB）
bash tools/fetch_pinyin_dict.sh
python3 tools/gen_pinyin_dict.py
# `make ime-dict` 就是跑上面这两条命令。

# 8. 编译 .3dsx
make

# 9. （可选）打 .cia
bash tools/install_cia_tools.sh   # 装 bannertool + makerom 到 ~/bin
                                  # `make cia-tools` 跑的是同一个脚本
make cia                           # 输出 DSSH.cia
```

构建命令本身没有变化：`make` 会先生成固定版本的
`libts3ds/build/3ds/libts3ds.a`，再链接 DSSH。若 submodule 未初始化，
Makefile 会直接提示正确命令，而不是稍后报目录或头文件缺失。

### 测试 IME 引擎（host 端，不需 3DS）

```bash
make test-ime
```

会编译 `tools/test_ime.c` 链 `source/ime_pinyin.c`，跑 9 个常用查询用例
（`ni→你`，`nihao→你好`，`nh→你好`，等等）。

### 其余 host 端测试

每个 host 端测试都有一个 `make test-<name>` 目标（`preview-project-page` 和
`preview-machine-page` 是渲染预览图、不做断言）。目标名声明在 `Makefile` 的
`.PHONY` 那一行——
权威清单是那一行，而不是这里复述的任何列表：抄进散文里的清单，下一次
有人加测试就已经过期，而且没有任何东西会报错提醒你。它们全部在 host
上跑，不需要接 3DS，也不需要 ARM 工具链。

---

## 项目结构

```
DSSH/
├── 69633.PNG                  # 源图标 (162×102)
├── icon.png                   # .3dsx / SMDH 图标 (48×48，从源图生成)
├── app.rsf                    # makerom CIA spec
├── Makefile                   # 主构建（make / make cia / make test-*）
├── build-libssh2.sh           # libssh2 + mbedTLS ARM 交叉编译
├── libts3ds/                  # 固定版本的原生 Tailscale 客户端 submodule
├── source/
│   ├── main.c                 # 主循环、SSH receive、多窗口、输入路由
│   ├── ssh_client.{c,h}       # libssh2 封装 —— shell channel + 辅助 exec channel
│   ├── config.{c,h}           # config.ini / settings.ini 解析（含 [projects]）
│   ├── terminal.{c,h}         # ANSI/VT100 解析器（fork skmtrd）
│   ├── renderer.{c,h}         # citro2d 渲染（终端、文本、CJK）
│   ├── keyboard.{c,h}         # 物理按键 + IME 路由
│   ├── keyrepeat.h            # 自动重复时序表
│   ├── softkb.{c,h}           # 软键盘 + 候选条 + 设置页
│   ├── ime_pinyin.{c,h}       # 拼音引擎
│   ├── ai_modal.{c,h}         # R+START AI 问答 modal
│   ├── voice.{c,h}            # 麦克风采集 + 上传给 whisper shim
│   ├── audio.{c,h}            # 按键音效播放
│   ├── key_sound_*.h          # 生成的按键音采样（每个吉祥物物种一个）
│   ├── mascot.{c,h}           # 底栏像素宠物
│   ├── kb_theme.{c,h}         # 按主题的下屏配色
│   ├── theme_ini.{c,h}        # theme.ini 解析（自定义主题）
│   ├── project_ui.{c,h}       # 项目卡片页：列表、编辑器、pwd 探测
│   ├── machine_ui.{c,h}       # SSH 机器选择页（多台机器时的前置一屏）
│   ├── project_presets.{c,h}  # agent CLI 预设表 + 各自的像素标记
│   ├── keychain_protocol.h    # macOS 钥匙串助手 IPC
│   ├── font_atlas.{c,h}       # 字体索引
│   ├── json_find.{c,h}        # 扁平 JSON 读取（语音/AI 回复字段）
│   ├── utf8_fit.h             # 按码点边界截断的辅助函数
│   └── font_data.c            # 字体位图（gen_font.py 生成）
├── tools/
│   ├── stub/                  # host 端桩，让不依赖 citro2d 的逻辑可测
│   ├── test_*.{c,sh}          # host 端测试 —— 每个 `make test-*` 目标一个
│   ├── preview_project_page.* # 项目页各屏 → design/project-page/*.png
│   ├── preview_machine_page.* # 机器选择页各屏 → design/machine-page/*.png
│   ├── fetch_fonts.sh         # 下载 Zpix
│   ├── gen_font.py            # 字体 atlas 生成器
│   ├── fetch_pinyin_dict.sh   # 下载 rime-ice
│   ├── gen_pinyin_dict.py     # 词典 → 二进制
│   ├── bake_key_sound.py      # 按键音采样 → key_sound_*.h
│   ├── gen_mascot_speech.py   # 吉祥物对话气泡字形
│   ├── gen_mascot_dog32.py    # 32×32 刀盾狗母版设计稿
│   ├── bake_mascot_dog20.py   # 母版 → 真实 20×18 画布 + C 代码
│   ├── explore_dog_variants.py# 盾/刀形状变体对比
│   ├── render_dog_before_after.py # 新旧精灵对照图
│   ├── preview_mascot.py      # 吉祥物对照图
│   ├── check_mascot_sync.py   # 守卫 preview_mascot.py 里的帧数据副本
│   ├── audit_mascot_outline.py# 描边缺口审计
│   ├── import_niulai.py       # 牛来精灵导入器
│   ├── whisper_daemon.py      # 本地 whisper.cpp daemon（离线轨）
│   ├── whisper_shim.py        # 服务器端双轨转写 shim
│   ├── test_whisper_gate.py   # whisper shim 闸门检查
│   ├── install_whisper_*.sh   # 服务器端安装器
│   ├── gen_cia_assets.py      # 图标/banner 派生
│   ├── modern_words.txt       # 并入拼音词典的额外词表
│   ├── yazi_icons.txt         # 烘进字体的 yazi 图标码点清单
│   └── install_cia_tools.sh   # bannertool + makerom 安装
├── design/                    # 已提交的预览图：mascot/ + project-page/*.png
├── romfs/                     # gitignored — 装载 pinyin_dict.bin
├── data/                      # gitignored — 字体源 + 词典源
└── sd_template/               # SD 卡部署模板
    ├── README.md
    └── 3ds/3dssh/
        ├── config.ini.example
        └── theme.ini.example
```

## 架构概览

```
SSH server (somewhere on the internet)
     ▲ libssh2 over mbedTLS-RSA-4096
     │
┌────┴──────────────────────────────────────────────────┐
│  main.c poll loop @60fps                              │
│   ├─ ssh_read → softkb_record_recv → utf8 reassemble  │
│   │                ↓                                  │
│   │   terminal_write_n → ANSI parser → cell grid      │
│   ├─ hidScanInput → keyboard_handle_input             │
│   │   ├─ IME mode? → ime_input_letter / page / select │
│   │   ├─ R held → voice_record_start / _stop → upload │
│   │   └─ R + START → ai_modal_open (bottom-screen)    │
│   ├─ hidTouchRead → softkb_touch                      │
│   │   ├─ candidate strip hit → ime_select             │
│   │   ├─ key hit → keyboard_emit_for / ime_input      │
│   │   └─ ⚙ badge tap → settings page (debug row)      │
│   ├─ project_ui_tick — 卡片页打开时它独占键盘         │
│   │   ├─ 列表 / 编辑器状态机                          │
│   │   └─ 「取用当前目录」→ 在第二条 channel 上        │
│   │        ssh_aux_exec 跑 pwd（新 shell 的登录目录， │
│   │        所以本窗口自己的 cd 目标会被优先询问）     │
│   └─ render: top = renderer_draw_terminal, or the     │
│                 settings list while settings are open │
│              bot = softkb_draw + clock + mascot, or   │
│                 the settings detail card              │
│              （卡片页打开时两个屏都画 project_ui_*）  │
└───────────────────────────────────────────────────────┘
     │
   citro2d (3DS 2D rendering)
     │
   GPU (top 400×240 + bottom 320×240, 24-bit color)
```

详细 milestone 进度参见 commit history（M0 → M11 全部提交）。

---

## 致谢

3DShell 的实现离不开以下优秀的开源项目与社区贡献：

### 原上游项目

- **[Fishason/DSSH](https://github.com/Fishason/DSSH)**：由 [Fishason](https://github.com/Fishason) 开源的 DSSH 原项目。奠定了 3DS 终端的核心架构、ANSI 终端集成、拼音输入法引擎及云端语音听写流程基础。

### 核心依赖与开源库

- **[skmtrd/3dssh](https://github.com/skmtrd/3dssh)**：原日文版 3DS 终端仿真器及 citro2d 渲染引擎核心。
- **[rime-ice](https://github.com/iDvel/rime-ice)** — 拼音词典源
  （commit `3f57a6f6` pin）
- **[Zpix Pixel Font](https://github.com/SolidZORO/zpix-pixel-font)** —
  12px CJK 像素字体（OFL 1.1）
- **[Terminus TTF](https://terminus-font.sourceforge.net/)** — ASCII /
  box-drawing 像素字体
- **[libssh2](https://www.libssh2.org/)** + **[mbedTLS](https://www.trustedfirmware.org/projects/mbed-tls/)** —
  SSH/TLS 协议栈
- **[cadl/libts3ds](https://github.com/cadl/libts3ds)** — Nintendo 3DS 原生
  Tailscale 兼容传输，派生自
  **[CamM2325/microlink](https://github.com/CamM2325/microlink)**
- **[devkitPro](https://devkitpro.org/) libctru / citro2d / citro3d** —
  3DS 用户态运行时 + 渲染
- **[carstene1ns/3ds-bannertool](https://github.com/carstene1ns/3ds-bannertool)**
  + **[3DSGuy/Project_CTR makerom](https://github.com/3DSGuy/Project_CTR)** —
  CIA 打包工具

## License

本项目采用 **MIT 许可证** 开源 — 完整条款见 [LICENSE](LICENSE)。

3DShell 属于基于 **[Fishason/DSSH](https://github.com/Fishason/DSSH)** 与 **[skmtrd/3dssh](https://github.com/skmtrd/3dssh)** 的二次开发衍生作品。

项目打包集成的第三方字体、词典以及上游底层通信库各自保留其开源许可证（SIL OFL 1.1 / LGPL-3.0 / BSD-3-Clause / Apache-2.0 / zlib 等），在二次分发二进制文件（`.cia` 或 `.3dsx`）时请遵守对应开源协议。
