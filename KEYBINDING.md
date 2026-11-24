# DSSH Keybindings / 按键说明

> Current binding model — voice recording on **R**, AI-ask on
> **R + START**, dual-screen toggle on **START + L**, IME mode on the
> on-screen **拼/EN** soft key.  See [README.md](README.md) for the
> feature overview and [README.zh.md](README.zh.md) for the
> Chinese-language counterpart.

---

## English

DSSH aims for a **uniform key surface across every 3DS-family model**
— every key documented below is a physical button that exists on the
original 3DS, 3DS XL, 2DS, New 3DS, New 3DS XL, and New 2DS XL.  No
model detection is needed.

### Terminal mode

| Button | Function | Notes |
|---|---|---|
| **A** | Enter (EN) / **emit pinyin buffer as English** (IME) | Accidentally typed English in CN mode? Press A and the buffer flies to SSH as raw ASCII — no need to backspace and switch modes. |
| **B** | Backspace / consume one pinyin letter | Hold-style auto-repeat (peaks at 60 / sec) |
| **X** | Alt modifier | Hold-style — held when the next key fires |
| **Y** | Ctrl modifier | Hold-style — Y + tap `c` → Ctrl-C |
| **Y + space** / **Y + @** | `Ctrl-Space` / `Ctrl-@` → **NUL (`0x00`)** | readline / emacs-style set-mark. Both spellings emit the same byte — a real NUL, not a space and not a literal `@`. Space and `@` are soft keys (Space on every page, `@` on the symbol pages), so this is "hold **Y** + tap **space**" |
| **SELECT** | Esc | Tap fires immediately |
| **D-pad ↑↓** | Arrow keys / IME page nav | When the IME buffer is active, ↑↓ paginates candidates.  With **X + Y** held they open / close a window instead |
| **D-pad ←→** | Arrow keys / IME selection cursor | When active, ←→ moves the candidate cursor within the page.  With **X + Y** held they switch windows instead |
| **Circle Pad ↑↓** | Scrollback / full-screen app paging | A normal shell scrolls DSSH's **local scrollback**.  When the remote is in a full-screen app (opencode, htop, vim…), DSSH sends **PageUp / PageDown** instead — those apps redraw in place, so nothing ever scrolls off and the local buffer would be empty |

### Voice (push-to-talk)

| Combo | Function | Notes |
|---|---|---|
| **R held** | Voice recording (start sampling on press, send on release) | The **single** entry point for voice IME.  Hold down to record, release to ship — there is no tap-toggle.  Auto-stops at the **Max Voice Limit** setting (15 / 30 / 60 s) |
| **R held + START** | AI-ask voice modal | Hold R, tap START — you are now talking to the AI instead of typing into SSH.  The answer pops up in a bottom-screen modal.  Press **A** to keep history, **B** (or touch anywhere) to clear |

### Dual-screen one-terminal

| Combo | Function | Notes |
|---|---|---|
| **START + L** | Merge both screens into one 66×40 terminal (top 0–19 rows + bottom 20–39 rows); press again to restore the soft keyboard | **The single dual-screen trigger** — works on **every** 3DS model.  The SSH PTY is renegotiated live; the remote shell repaints once.  See the *Dual-screen one-terminal* section in README.md for rendering details |

### Multi-window terminal

| Combo | Function | Notes |
|---|---|---|
| **X + Y + ▲** | **New window** | Opens another shell channel on the *same* SSH login.  Both modifiers must be physically held — a sticky-latched X/Y does not count |
| **X + Y + ▼** | **Close current window** | Tears that channel down — the remote shell gets `SIGHUP`, so it works even when a full-screen TUI has swallowed stdin.  Inert on the only remaining window |
| **X + Y + ◀** or **X + Y + L** | Previous window | Wraps around.  The chord swallows L so no stray `←` reaches the shell and L does not trigger the dual-screen toggle |
| **X + Y + ▶** or **X + Y + R** | Next window | Same, forward; `R` does not start a voice take on that press (the chord steals the held bit) |

### Soft keyboard (bottom screen, touch)

| Element | Function |
|---|---|
| a–z | Letters (CN mode routes through the IME) |
| **⇧** | One-shot shift; long-press = CapsLock |
| **拼/EN** | **Toggle IME mode** (CN ↔ EN).  Replaces the old BSP soft key.  The status chip in the bottom-bar shows **C** / **E** as a transient event label |
| **123 / 拼音 / ABC** | Page switch (letters ↔ symbols 1 ↔ symbols 2) |
| tab | Send `\t` |
| space | Plain space (EN) / **commit highlighted candidate** (IME) |
| **↑** | Enter (IME: commit candidate + Enter) |
| Touch bottom-row (y ≥ 214) | Mascot pet (no-op in dual-screen mode, where the bottom screen is terminal space) |

### AI-ask modal

| Button | Effect |
|---|---|
| **A** | Close, **keep** the Q&A in history; next R+START continues the conversation |
| **B** or any **touch** on the bottom screen | Close, **clear** history; next R+START is fresh |

### Settings page

The settings page **owns the whole keypad** while open — R does not
start a voice take, START does not trigger AI-ask, the D-pad does not
emit arrows, **START+L does not flip the dual-screen view**, and the
**X + Y** window chords do nothing.  (The debug sub-page is a
different page — it does not hold the keypad, so window chords work
there.)

| Button | Effect |
|---|---|
| **D-pad ↑↓** | Move the selection (wraps; holding auto-repeats) |
| **D-pad ←→ / A** | Step the selected item's value (wraps); on *Debug page*, A enters the debug screen |
| **B** | Back out one level (group → root); from the root it closes settings |
| **L / R** | Step between the groups — 外观 → 输入 → 语音, wrapping.  Only inside a group; the root is not one of them |

### Machine picker

Shown at launch — and again on **SELECT** after a disconnect — **only when
`config.ini` holds more than one machine**.  It comes before the projects
page: nothing is connected yet, so there is no shell behind it, and each
machine has its own settings file (`settings.<N>.ini`) which is loaded on
confirm.

The bottom screen lists each machine as **its name over its address** (`host`,
or `host:port` when the port is not 22); a machine with no `name_N` keeps the
single centred `1. host` line.  The top screen carries the highlighted
machine's name / host / user / port / key.  Both halves take the live keyboard
palette, so the page follows the theme like every other screen.

| Button | Effect |
|---|---|
| **D-pad ↑↓** | Move the selection (wraps over every machine; the five-row list scrolls to follow it) |
| **A** | Connect to the highlighted machine, then raise the projects page |
| **B** | Cancel.  Still disconnected — **SELECT** reopens this page |
| **X** | Rename the highlighted machine |
| Tap a row | Select it and connect.  The gaps between rows and the hint line at the bottom are not hits |
| Tap the ✎ strip | Rename that row's machine — the rightmost 30 px of any row, marked with a hairline and a ✎.  The strip never connects, so this is the touch-only route to renaming |

### Rename field

A text field on the top screen, typed with the soft keyboard (the keyboard owns
the bottom screen while it is up).  Pre-filled with the current name, so a
rename can be an edit.

| Button | Effect |
|---|---|
| **A** or the keyboard's **✓** | Save.  An empty field saves too — that clears the name |
| **B** / **START** / **SELECT** | Discard |
| keyboard **⌫** | Drop one character (a whole CJK glyph, not one byte) |

Saving writes `name_<N>` into `config.ini` and nothing else; every other byte of
the file is preserved.  The connection details are still computer-only.

The chosen machine is written back to `selected_machine` and pre-highlighted
next launch, but the page still asks every time.

### Projects page

Raised by three things: the picker shown at connect time (after the
machine picker, when more than one machine is configured), **X + Y + ▲**
(so a brand-new window picks its *own* card), and the **项目** row on the
settings page.  All three raise the same object, so the keys below are
identical no matter how you got here.

While the page is up it **owns the whole keypad** — the X + Y window
chords, the START + L dual-screen toggle and R's voice take are all
suppressed, and the D-pad does not emit arrows into the shell.

**Card list** (bottom screen; the top screen describes the highlighted card)

| Button | Effect |
|---|---|
| **D-pad ↑↓** | Move the selection (wraps over the cards *and* the trailing **+ 新增项目** row) |
| **A** | Run the selected card in the focused window.  On the **+ 新增项目** row it opens the editor instead.  When the page came from the settings page, A never runs anything |
| **X** | Edit the selected card |
| **Y** | Delete the selected card — **press twice**; the first press only arms it (the 删除 button changes label) so a stray Y cannot wipe a card |
| **B** or **START** | Close the page |
| Tap a card | Select it; tapping the card that is *already* selected runs it |
| Tap 进入 / 编辑 / 新增 / 删除 | The same four actions by touch |

**Editor** (bottom screen; the top screen previews the command it will send)

| Button | Effect |
|---|---|
| **D-pad ◀▶** or **L / R** | Switch section: 目录 / 工具 / 参数 / 自定义 |
| **D-pad ▲▼** | Move within the current section |
| **A** | Act on the row — pick the tool, tick a flag, take the current directory, or start typing |
| **B** | **Save** and go back to the list |
| **START** | **Save** and close the page |
| Tap 保存 / 取消 | Save / throw the draft away |

While a text field is being typed the soft keyboard owns the bottom
screen.  **A**, **B**, **START**, **SELECT** — or tapping elsewhere —
commit the field and hand the keypad back.

### Idle keys

| Button | Why it's idle |
|---|---|
| **L** (alone, with no modifier) | Idle; reserved for the **START+L** dual-screen modifier and the **X + Y + ◀ / ▶** window chords |
| **START** (alone, with no modifier) | Idle, except for the **R+START** AI-ask chord and the **START+L** dual-screen modifier.  Use **HOME** to exit DSSH |
| **R** (alone, without holding) | Idle when not held.  Tapping R (without holding) doesn't toggle IME — that lives on the on-screen 拼/EN key |

> These are listed because they sound like something a returning user
> might try.  When a feature needs them in the future, the right move
> is to re-purpose them deliberately (e.g. L as a "quick command
> palette" key) — never re-bind them ad-hoc.

---

## 中文

DSSH 追求**跨所有 3DS 型号统一**的按键模型——下方所有键在原版 3DS、
3DS XL、2DS、New 3DS、New 3DS XL、New 2DS XL 上都是物理按键。
无需任何机型检测。

### 终端模式

| 键 | 功能 | 备注 |
|---|---|---|
| **A** | Enter（EN）/ **拼音 buffer 当英文直出**（IME） | CN 模式下不小心打了英文？按 A 直接把 buffer 作为 ASCII 发出去并清空，不用切换模式重打 |
| **B** | Backspace / 消拼音 buffer | hold-style 自动重复（最快 60/秒） |
| **X** | Alt 修饰 | hold-style，按住时下次按键加 Alt |
| **Y** | Ctrl 修饰 | hold-style，按住 Y + 点 c → Ctrl-C |
| **Y + 空格** / **Y + @** | `Ctrl-Space` / `Ctrl-@` → **NUL（`0x00`）** | readline / emacs 风格的 set-mark。两种写法发的是同一个字节——真的 NUL，不是空格也不是字面的 `@`。空格与 `@` 都是下屏软键（空格每页都有，`@` 在符号页），所以操作是「按住 **Y** + 点**空格**」|
| **SELECT** | Esc | 单点立即发 |
| **D-pad ↑↓** | 方向键 / IME 翻页 | IME buffer 非空时翻候选页；按住 **X + Y** 时改成新建/关闭窗口 |
| **D-pad ←→** | 方向键 / IME 选词 | IME buffer 非空时移动候选游标；按住 **X + Y** 时改成切换窗口 |
| **Circle Pad ↑↓** | 滚动 scrollback / 全屏应用翻页 | 普通 shell 滚动 DSSH 的**本地 scrollback**；远端在全屏应用（opencode、htop、vim…）里时改发 **PageUp / PageDown**——那类应用原地重绘，内容从不滚出屏幕，本地缓冲是空的 |

### 语音（对讲机式）

| 组合 | 功能 | 备注 |
|---|---|---|
| **R 按住** | 录音（按下即起录、松手即发送）| **唯一的语音 IME 入口**。按住说话、松开发送——无短按语义。自动停止于设置里的「录音上限」（15 / 30 / 60 秒）|
| **R 按住 + START** | AI 问答 modal | 按住 R、点 START——你在跟 AI 说话而不是给 SSH 输入。回答弹在底屏 modal 里。**A** 保留历史，**B**（或触屏任意处）清空 |

### 双屏一体终端

| 组合 | 功能 | 备注 |
|---|---|---|
| **START + L** | 上下屏拼成一块 66×40 大终端（上屏 0-19 行 + 下屏 20-39 行），再按恢复软键盘 | **唯一的双屏触发**——**所有 3DS 型号**通用。SSH PTY 实时重协商，shell 自动重绘一次。详见 README.zh.md「双屏一体终端」段 |

### 多窗口终端

| 组合 | 功能 | 备注 |
|---|---|---|
| **X + Y + ▲** | **新建窗口** | 在**同一次 SSH 登录**上开第 2（3、4）条 shell channel。两个修饰键必须同时物理按住——粘滞锁存的 X/Y 不算 |
| **X + Y + ▼** | **关闭当前窗口** | 拆掉那条 channel——远端进程收到 `SIGHUP`，所以全屏 TUI 吞掉 stdin 也照样关得掉。只剩一个窗口时无效 |
| **X + Y + ◀** 或 **X + Y + L** | 上一个窗口 | 绕回；这次按键会被吞——`←` 不会发给远端，L 也不会触发双屏 |
| **X + Y + ▶** 或 **X + Y + R** | 下一个窗口 | 同上、方向相反；R 在这次按下不会起录（和弦把 held 位偷走了）|

### 软键盘（下屏，触摸）

| 元素 | 功能 |
|---|---|
| a–z | 字母（CN 模式走 IME）|
| **⇧** | 一次性 Shift；长按 = CapsLock |
| **拼/EN** | **切换 IME 模式**（CN ↔ EN）。取代原 BSP 软键。底栏状态条会闪一个 **C** / **E** 提示标签 |
| **123 / 拼音 / ABC** | 切页（字母 ↔ 符号 1 ↔ 符号 2）|
| tab | 发 `\t` |
| space | 普通空格（EN）/ **提交当前高亮候选**（IME）|
| **↑** | Enter（IME 激活时：提交候选 + Enter）|
| 触屏最底 26 px（y ≥ 214）| 吉祥物宠物（双屏模式下无效，那时下屏是终端空间）|

### AI 问答 Modal

| 键 | 效果 |
|---|---|
| **A** | 关闭，**保留**这条 Q&A 进 history；下次 R+START 接续上下文 |
| **B** 或下屏**任意触屏** | 关闭，**清空** history；下次 R+START 是全新对话 |

### 设置页

设置页**占用整块键盘**——R 不会起录、START 不会触发 AI 问答、方向键
不会发给远端、**START+L 不会切双屏**、**X + Y** 窗口和弦也无效。
（Debug 子页是另一页——它不占键盘，所以窗口和弦在那儿能用。）

| 键 | 效果 |
|---|---|
| **D-pad ↑↓** | 移动选中行（循环；长按自动重复）|
| **D-pad ←→ / A** | 切换选中项的取值（循环）；在「调试页面」上按 A 进入 Debug 屏 |
| **B** | 退回上一层（分组 → 根页）；在根页上则关闭设置 |
| **L / R** | 在分组之间步进——外观 → 输入 → 语音，循环。只在分组内有效，根页不算其中一个 |

### 机器选择页

启动时弹出——断线后按 **SELECT** 也会重新弹出——但**只在 `config.ini` 里
配了不止一台机器时**。它排在项目页之前：这时还没有建立连接，背后没有
shell；每台机器有自己的设置文件（`settings.<N>.ini`），确认时才加载。

下屏每行是这台机器的**名字在上、地址在下**（`host`，端口不是 22 时是
`host:port`）；没写 `name_N` 的机器保持居中单行 `1. host` 的样子。上屏是
高亮那台的名称 / 主机 / 用户 / 端口 / 密钥。上下两屏都取当前软键盘调色板，
所以这一页和其他页面一样跟随主题。

| 键 | 效果 |
|---|---|
| **D-pad ↑↓** | 移动选中项（在所有机器间循环；一屏 5 行，列表会跟着滚）|
| **A** | 连接高亮的这台机器，连上后弹出项目页 |
| **B** | 取消。此时仍未连接——按 **SELECT** 重新打开这一页 |
| **X** | 给高亮的这台机器改名 |
| 点按某一行 | 选中并直接连接。行与行之间的缝隙、底部那行提示文字都不算命中 |
| 点按 ✎ 条 | 给这一行的机器改名——每行最右边 30 px，有一条细线和一个 ✎ 作标记。这条永远不触发连接，是纯触屏用户的改名入口 |

### 改名输入框

上屏的一个文本框，用软键盘输入（打开期间软键盘占下屏）。预填当前名字，
所以改名可以是「改」而不是「重打」。

| 键 | 效果 |
|---|---|
| **A** 或键盘的 **✓** | 保存。空框也保存——那就是把名字清掉 |
| **B** / **START** / **SELECT** | 放弃 |
| 键盘 **⌫** | 删掉一个字符（一个完整的中文方块字，不是一个字节）|

保存只往 `config.ini` 写 `name_<N>` 这一个键，文件其余每个字节都保留。
连接信息仍然只能在电脑上改。

选中的机器会写回 `selected_machine`、下次启动默认高亮，但**每次启动仍会问**。

### 项目页

三个入口都会拉起这一页：连接成功后弹出的选择器（配了多台机器时，它在
**机器选择页**之后）、**X + Y + ▲**（让每个新窗口自己挑一张卡片）、以及
设置页里的**「项目」**行。三者拉起的是同一个对象，所以不管从哪儿进来，
下面这些键都一样。

页面打开期间**独占整个键盘**——**X + Y** 窗口和弦、**START+L** 双屏
切换、R 的语音起录全部失效，方向键也不会往 shell 里发箭头。

**卡片列表**（下屏；上屏显示当前选中卡片的介绍）

| 键 | 效果 |
|---|---|
| **D-pad ↑↓** | 移动选中项（循环，卡片和末尾的 **+ 新增项目** 行一起转）|
| **A** | 在当前窗口执行选中的卡片。停在 **+ 新增项目** 行时改为打开编辑器。从设置页进来时 A 不会执行任何东西 |
| **X** | 编辑选中的卡片 |
| **Y** | 删除选中的卡片——**要按两次**；第一次只是"待确认"（「删除」按钮文字会变），防止误按一下就把卡片抹掉 |
| **B** 或 **START** | 关闭本页 |
| 点按卡片 | 选中它；点按**已经选中**的那张则直接执行 |
| 点按 进入 / 编辑 / 新增 / 删除 | 与上面四个动作等价 |

**编辑器**（下屏；上屏实时预览这张卡片将要发出的命令）

| 键 | 效果 |
|---|---|
| **D-pad ◀▶** 或 **L / R** | 切换分段：目录 / 工具 / 参数 / 自定义 |
| **D-pad ▲▼** | 在当前分段内移动 |
| **A** | 对当前行执行动作——选工具、勾参数、取用当前目录，或开始输入 |
| **B** | **保存**并回到列表 |
| **START** | **保存**并关闭本页 |
| 点按 保存 / 取消 | 保存 / 丢弃这份草稿 |

正在输入某个文本字段时，下屏交给软键盘。按 **A**、**B**、**START**、
**SELECT**，或点按别处，即提交该字段并把键盘交还。

### 空闲键

| 键 | 为什么空闲 |
|---|---|
| **L**（单按，无修饰）| 空闲，预留给 **START+L** 双屏修饰与 **X + Y + ◀ / ▶** 窗口和弦 |
| **START**（单按，无修饰）| 空闲，仅参与 **R+START** AI 问答和弦与 **START+L** 双屏修饰。退出 DSSH 用 **HOME** 键 |
| **R**（单按，不按住）| 非按住状态空闲。点按 R（不按住）不会切换 IME——切换在下屏软键盘的「拼/EN」键上 |

> 列这几个"空闲键"是因为老用户可能会按它们。将来若有功能需要，按键
> 复用应该是有意识的（例如 L 可以做"快速命令面板"），绝不是临时起意。

---

## See also

- [README.md](README.md) — feature overview (English)
- [README.zh.md](README.zh.md) — feature overview (中文)
- `source/keyboard.h`, `source/keyboard.c` — IME / physical-key dispatch
- `source/softkb.c`, `source/softkb.h` — soft keyboard + settings page
- `source/project_ui.c`, `source/project_ui.h` — the projects page
  (card list, three-part editor, custom-command mode, `pwd` probe)
- `source/machine_ui.c`, `source/machine_ui.h` — the machine picker shown
  before connecting when more than one machine is configured (five-row
  scrolling list, host/user/port/key detail panel)
- `source/project_presets.c`, `source/project_presets.h` — the agent-CLI
  preset table (claude / codex / opencode / agy / gemini / aider) and each
  tool's 16×16 pixel mark
- `source/main.c` — physical-key routing (look for the voice block and
  the dual-screen toggle)
- `source/voice.h` — voice state machine API