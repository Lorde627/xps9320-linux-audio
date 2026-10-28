# XPS 13 Plus 9320 Linux Audio

An unofficial Linux speaker DSP port for the Dell XPS 13 Plus **9320**, using the existing SOF / SoundWire kernel drivers, PipeWire, native LV2 plugins and an event-driven volume bridge. This is a user-session processing chain, not a replacement kernel driver or an official Dell/Waves release.

## Tested scope

One XPS 9320, board `0V0059`, with two RT1316 amplifiers; Arch Linux, kernel 7.2.11, SOF firmware 2026.09.2, ALSA UCM 1.2.16.1, PipeWire 1.6.10, WirePlumber 0.5.18, LSP LV2 1.2.35. Only 48 kHz data is shipped. The dynamics plugin targets x86-64-v3, suitable for this machine's Alder Lake CPU.

| Component | Status |
| --- | --- |
| Internal speakers | Integrated and software-validated on one XPS 9320 |
| Processing chain | Input crossover → bass/3D group → HF → PEQ → multiband dynamics/iVolume → LSP True Peak limiter |
| Sample rate | 48 kHz presets and configuration |
| Volume integration | Dynamics curve follows the endpoint's actual gain; final output volume is applied once by the endpoint |
| Other XPS models and speaker assemblies | Not validated; the installer rejects other models |

## Arch Linux installation

First establish a working stock **HiFi Speaker** sink. This project does not fix missing hardware or kernel enumeration failures.

```bash
sudo pacman -Syu --needed git gcc python lv2 lilv pipewire pipewire-audio pipewire-pulse wireplumber sof-firmware alsa-ucm-conf lsp-plugins-lv2
git clone https://github.com/Lorde627/xps9320-linux-audio.git
cd xps9320-linux-audio
python3 scripts/build.py
python3 -m unittest discover -s tests -v
python3 scripts/install.py check
python3 scripts/install.py install --dry-run
python3 scripts/install.py install
```

Run the DSP installer as your desktop user, **without sudo**. It checks dependencies, backs up replaced files, stops user audio services before replacing libraries, installs five LV2 bundles under `~/.lv2`, adds the PipeWire graph, and enables `xps9320-volume.service`. Audio services restart briefly. Default XDG paths and balanced stereo volume are required in this release.

Use the physical internal Speaker output normally. WirePlumber's smart filter matches only this speaker. Avoid stacking another XPS EQ/processing chain on it. The volume bridge changes the dynamics curve from the endpoint's actual gain; final volume/mute/balance is still applied by the endpoint once.

The minimum accepted WirePlumber version is 0.5.10; only the recorded 0.5.18 stack was tested. Python 3.10+, GCC, LV2 development headers, Lilv and LSP LV2 are required. Other distributions are not yet validated.

## Uninstall

```bash
python3 scripts/install.py uninstall
```

Original files and service state are restored from `~/.local/state/xps9320-audio/installation`. Backups remain after successful removal. If a managed file has changed since installation, removal stops instead of overwriting your edits. Installation errors trigger a rollback attempt and preserve the original-file backup.

## Verification

```bash
python3 scripts/install.py status
systemctl --user status xps9320-volume.service
pw-link -l
pw-top
python3 tests/smoke_pipewire.py
```

During playback expect application → `effect_input.xps9320` and `effect_output.xps9320` → Speaker links. The smoke test uses an isolated server, freshly built plugins, system LSP and synthetic input, without touching live services or audio hardware. See [validation scope](docs/validation.md) and the [troubleshooting guide](docs/troubleshooting.md).

This repository contains no original Windows DLL or installer, personal recordings, credentials, or private research logs. No Windows installation is required. See [NOTICE](NOTICE.md) for provenance and licensing scope. Digital peak limiting is not a claim of complete speaker thermal or excursion protection; begin listening at a low volume.

## Repository contents

- `src/`: the five native LV2 research implementations and required headers.
- `bundles/`: 48 kHz preset data, LV2 metadata and SHA256 integrity records.
- `scripts/`: build, prerequisite checks, installation, rollback and volume integration.
- `tests/`: synthetic input fixtures, deployed-plugin reference hashes, chunking checks and rollback tests.

The research input was Dell's `XPS-13-9320-KP6R0_Win11_1.0_A15.exe` package. The repository ships native source and the small preset data needed by this processing path, rather than the original Windows executable or DLL. See [NOTICE](NOTICE.md) for the distinction between original integration tooling and research-derived materials.

## Reporting issues

Include your laptop model and board, distribution, kernel, PipeWire/WirePlumber versions, `scripts/install.py check` output, and `pw-link -l` / `pw-top` observations during playback. Do not attach passwords, tokens, personal recordings or complete private logs.
