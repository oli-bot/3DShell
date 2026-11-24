# 发布清单

上一个 release 是 v1.3.0（2026-08-13），它的 assets 少了 `demo.mp4`，README 里
`releases/latest/download/demo.mp4` 那条链接从那天起就是 404 —— 这份清单存在的
唯一理由就是下一发别再漏。逐项走完。

## 1. 门禁

```bash
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM

# 19 个宿主机测试目标（全部 test-* 目标是 20 个，少列的 test-whisper-gate 见下），
# 逐个跑，任何一个红就停
for t in ime json-find json-build keyboard font-atlas utf8-next config \
         config-projects project-presets project-ui machine-ui ai-modal \
         softkb-i18n terminal theme theme-ini keyrepeat mascot mascot-outline; do
  # 输出留在 /tmp：红的时候只有 "FAIL test-x" 一句话等于没跑
  if make test-$t >/tmp/gate_$t.log 2>&1; then
    echo "PASS test-$t"
  else
    echo "FAIL test-$t — /tmp/gate_$t.log"; tail -20 "/tmp/gate_$t.log"; break
  fi
done

make -j4 all      # 只允许 main.c 时钟行那一条既有 -Wformat-truncation
```

`make test-whisper-gate` 只在动过 `tools/whisper_daemon.py` 时才需要。

## 2. 产物

```bash
make cia          # DSSH.cia 与 3dssh.3dsx 都要出，不能只出 3dsx
```

版本号从 `git describe` 派生、编译进 `build/version.h`（`main.o` 依赖它），所以
**先打 tag 再出包**：tag 之前出的包，开机画面会写 `v1.3.0-45-g…` 这种"距上个 tag
多少个提交"。tag 之后重跑 `make cia`，然后核对两处：

- 装机后开机画面有一行 `DSSH v…`，与要发的 tag 一致；
- `build/dssh.smdh` 的长描述（UTF-16）里同样带着它。

第一条不必等真机：`build/.verify/install.sh` 装本轮 CIA，再
`build/.verify/run.sh <outdir> 6:boot` 截图。但 SD 的 `config.ini` 要临时指向
一个连不上的主机（`10.255.255.1`）——真连上时 6 s 内就翻进项目页，抓不到开机
画面；跑完记得把 SD 还原（连上之后 `config.ini` 会被回写一行 `config_version`）。

在 Mac 上就地核对（`build/version.h` 与 ELF 里的串必须一致）：

```bash
v=$(sed -n 's/.*"\(.*\)".*/\1/p' build/version.h); echo "$v"
strings 3dssh.elf | grep -c "$v"        # 期望 1
```

**`strings DSSH.cia` 查不到它是正常的**：makerom 把 content 段加密了，CIA 里只有
SMDH 那段是明文。

## 3. Release assets —— 少一个就不算发完

| Asset | 来源 | 备注 |
|---|---|---|
| `DSSH.cia` | `make cia` | 主交付物 |
| `3dssh.3dsx` | `make all` | HBL 用户 |
| `demo.mp4` | 本地 `docs/media/demo.mp4`（gitignore，不进仓库） | README 封面图链到 `releases/latest/download/demo.mp4`；**不传就是 404**，v1.3.0 漏在这里 |

## 4. 文档

- `README.md` 与 `README.zh.md` 一起改，逐词 `grep -c` 核双语计数。
- `sd_template/3ds/3dssh/config.ini.example` 里出现的键，README 字段表必须有，
  反之亦然（08c11c9 就是修这里的漂移）。
- 「Known limitations / 已知限制」是最容易被跳过的一节：新的行为边界要写进去，
  不能只写进 `docs/audit-*.md`。主机密钥不验证这条就是这么漏的。

## 5. 推送

```bash
git push origin main
git push origin <tag>
gh release create <tag> DSSH.cia 3dssh.3dsx docs/media/demo.mp4 \
  --title "…" --notes-file …
gh release view <tag> --json assets --jq '.assets[].name'   # 三个都在才算完
curl -I https://github.com/oli-bot/3DShell/releases/latest/download/demo.mp4  # 期望 200
```

## 6. 发出去就得在 README 里认的未决项

- **不校验服务器主机密钥**（中间人可以冒充服务器）—— README 已披露，代码未修。
- `docs/audit-2026-09-14.md` 第二十七节末尾"没能在模拟器上验的"三条：
  `settings_save()` 的设备侧写入、T67 会话内重试、T74 转写控制字符。