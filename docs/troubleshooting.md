# 排查与可选 UCM

## 先确认基础声卡

```bash
cat /proc/asound/cards
aplay -l
for p in /sys/bus/soundwire/devices/sdw:*/status; do printf '%s: ' "$p"; cat "$p"; done
wpctl status
```

测试机器的放大器是 `sdw:0:1:025d:1316:01` 和 `sdw:0:2:025d:1316:01`，应为 `Attached`。麦克风设备是 `sdw:0:0:025d:0714:01`。驱动 `rt715-sdca` 同时支持 0x714；驱动名字不同不等于绑定错误。

测试机器未安装摄像头模组，麦克风也位于该模组内，因此 `UNATTACHED` 在这台机器上由硬件缺失解释。**不要据此推断所有 XPS 9320 的同类现象原因都相同。** 有模组的机器应另行排查，不能直接套用下面的隐藏麦克风方案。

## 只有模组物理缺失时：扬声器专用 UCM

这台机器上的原生 ACP 探测因不存在的麦克风而无法建立 HiFi 配置。可选 UCM 只保留双 RT1316 Speaker 与 HDMI。

此配置只验证过主板 `0V0059`。工具会检查机型、主板和麦克风状态，并备份现有文件：

```bash
sudo python3 -I scripts/ucm.py install --no-microphone
systemctl --user restart wireplumber.service
python3 scripts/install.py check
```

文件位于 `/usr/share/alsa/ucm2/conf.d/sof-soundwire/DellInc.-XPS9320-0V0059.conf` 和 `/usr/share/alsa/ucm2/sof-soundwire/HiFi-xps9320.conf`。原件备份在 `/var/lib/xps9320-audio-ucm`。系统更新后检查自定义配置是否仍有效；工具不改写系统包的通用 UCM 文件。

不要手动解除/重新绑定 `rt715-sdca` 来尝试修复缺失的麦克风。研究中观察到：这样操作会使机器声卡 `Speaker Switch` 控件消失，导致原本工作的扬声器 HiFi 配置加载失败。本仓库的正常安装不重绑任何设备，也不重置 PCI 控制器。

## 安装检查失败

- `Missing command`：安装 README 列出的依赖。
- `Missing LSP stereo limiter`：检查 `lsp-plugins-lv2` 及 Lilv 搜索路径。不要随意替换限幅插件或 oversampling 枚举值。
- `stock HiFi speaker sink is missing`：先解决内核/SOF/UCM 层的问题；DSP 安装不是基础声卡修复工具。
- 机型/CPU 不匹配：这份预设及二进制目标没有验证过你的硬件，安装器不会自动放宽检查。
- 非默认 XDG 目录：当前安装器仅支持标准用户目录；不要用 sudo 绕过检查。
- `Another XPS DSP config exists`：先备份并停用重复配置，避免两套处理链叠加。
- 安装备份已存在：使用原来的安装目录卸载或检查备份，避免覆盖回滚依据。

## 不发布个人诊断信息

安装器不索取系统密码或 GitHub 令牌。只有可选 UCM 操作使用本地 sudo；不要把密码填进脚本或 issue。测试夹具是确定性合成信号，不是麦克风录音。
