# XPS 13 Plus 9320 Linux Audio

Dell XPS 13 Plus **9320** 的 Linux 扬声器 DSP 移植与安装工具。中文说明如下；[English guide](docs/README.en.md)。

本项目在内核已有的 SOF / SoundWire 驱动之上，通过 PipeWire + LV2 接入研究复刻的扬声器处理链，并让动态处理曲线随 Linux 输出音量变化。**这不是内核驱动替换包，也不是 Dell / Waves 官方发布。**

## 当前状态与适用范围

| 项目 | 状态 |
| --- | --- |
| 内置扬声器 | 已在一台 XPS 9320 上完成日常接入与软件验证 |
| 处理链 | 输入分频 → MaxxBass / 3D 组 → 高频处理 → PEQ → 多段动态 / iVolume → LSP True Peak 限幅 |
| 采样率 | 本仓库仅提供 **48 kHz** 预设与配置 |
| 音量联动 | 根据端点实际线性增益更新动态曲线，最终输出音量由端点应用一次 |
| 麦克风 | 测试机器未安装摄像头/麦克风模组；未验证录音 |
| 睡眠唤醒、长期实机听音、大音量声学表现 | 尚未完成全面验收 |
| 其他 XPS 型号、其他主板与扬声器组合 | 未验证，安装器不接受其他机型 |

测试机器：XPS 9320 / 主板 `0V0059`，双 RT1316 扬声器放大器，Arch Linux，内核 `7.2.11-arch1-1`，SOF firmware `2026.09.2`，ALSA UCM `1.2.16.1`，PipeWire `1.6.10`，WirePlumber `0.5.18`，LSP LV2 `1.2.35`。这些是测试记录，不要求版本必须完全相同。

动态插件使用 `x86-64-v3` 编译，适用于本机 Alder Lake CPU。本版需要 Python 3.10+、GCC、LV2 开发头文件、PipeWire、WirePlumber 0.5.10+、Lilv 和 LSP LV2；更早的软件栈没有在本仓库中验证。测试使用 Python 3.14 / GCC 16。

## 安装：Arch Linux

先确认内核已有声卡、两个 RT1316 已附着，系统的 **HiFi Speaker** 输出能够正常出现。本项目的 DSP 安装器不会修复硬件枚举或刷写固件。

安装依赖：

```bash
sudo pacman -Syu --needed git gcc python lv2 lilv pipewire pipewire-audio pipewire-pulse wireplumber sof-firmware alsa-ucm-conf lsp-plugins-lv2
```

下载、构建并验证：

```bash
git clone https://github.com/Lorde627/xps9320-linux-audio.git
cd xps9320-linux-audio
python3 scripts/build.py
python3 -m unittest discover -s tests -v
python3 scripts/install.py check
python3 scripts/install.py install --dry-run
```

确认检查通过后，**以当前桌面用户运行，不加 sudo**：

```bash
python3 scripts/install.py install
wpctl status
```

安装会短暂重启用户音频服务。它备份原文件，将五个 LV2 插件装入 `~/.lv2`，添加 PipeWire 配置，并启用 `xps9320-volume.service`。**不要再叠加另一套 XPS EQ / 音效配置。**

安装器保留当前正常的左右平衡与输出音量；请先使用左右相等的立体声音量。PipeWire 里的智能滤镜只匹配内置 Speaker，HDMI 和其他输出不使用这套扬声器预设。安装后，在音量界面继续使用内置 Speaker；智能滤镜会插入播放路径。

如果 `check` 提示没有 HiFi Speaker，先看 [排查说明](docs/troubleshooting.md)。**只有确实未安装摄像头/麦克风模组时**，才使用可选的扬声器专用 UCM；正常装有麦克风的机器不要安装该配置。

## 卸载与回滚

在仓库目录，以安装时的桌面用户运行：

```bash
python3 scripts/install.py uninstall
```

原文件备份位于 `~/.local/state/xps9320-audio/installation`；成功卸载后备份以 `removed-<时间>` 保留。如果用户在安装后修改了受管理文件，卸载器会停止，避免覆盖后续修改。安装失败会尝试恢复原文件和原服务状态，并保留备份与错误信息。

可选 UCM 是独立的系统配置；只在安装过它时另行恢复：

```bash
sudo python3 -I scripts/ucm.py remove
systemctl --user restart wireplumber.service
```

## 验证与已知限制

```bash
python3 scripts/install.py status
systemctl --user status xps9320-volume.service
pw-link -l
pw-top
```

播放时应看到应用 → `effect_input.xps9320`，以及 `effect_output.xps9320` → 内置 Speaker 的连接。音量联动状态写入 `$XDG_RUNTIME_DIR/xps9320-volume-state.json`。这不是独立音量控制器，不要同时手动调节滤镜节点的音量。

仅测试重建后的插件与完整处理图，不连接声卡、不改变现有音频服务：

```bash
python3 tests/smoke_pipewire.py
```

[验证范围](docs/validation.md)区分了核心数值一致、宿主录音一致和实机验收。限幅器限制数字信号峰值，不能据此声称已经复刻原厂全部热保护、振膜位移保护或 Windows 策略。首次使用从较低音量开始。

## 仓库内容与来源

- `src/`：五个原生 LV2 插件的研究实现及所需头文件。
- `bundles/`：48 kHz 参数数据、LV2 元数据及 SHA256 清单。
- `scripts/`：独立构建、检查、安装、回滚和音量联动工具。
- `ucm/`：仅供摄像头/麦克风模组物理缺失机器使用的可选配置。
- `tests/`：合成测试信号、已验证本机插件的输出指纹、分块与回滚测试。

本仓库不包含原厂 Windows DLL、驱动安装包、录音、账号凭据或私人研究日志。原厂算法研究参考来自 Dell XPS 9320 驱动包 `XPS-13-9320-KP6R0_Win11_1.0_A15.exe`。本项目不要求安装或启动 Windows。

有关源码与参数数据的区别，见 [NOTICE](NOTICE.md)。

## 反馈

提交 issue 时，请提供机型/主板、发行版、内核、PipeWire/WirePlumber 版本、`install.py check` 输出，以及播放期间的 `pw-link -l` / `pw-top`。不要提交密码、令牌、整段个人日志或录音。
