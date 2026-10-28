# Troubleshooting and optional UCM

## Check the baseline sound card first

```bash
cat /proc/asound/cards
aplay -l
for p in /sys/bus/soundwire/devices/sdw:*/status; do printf '%s: ' "$p"; cat "$p"; done
wpctl status
```

The test machine's amplifiers are `sdw:0:1:025d:1316:01` and `sdw:0:2:025d:1316:01`; both should report `Attached`. Its microphone device is `sdw:0:0:025d:0714:01`. The `rt715-sdca` driver also supports device 0x714, so the different driver name alone does not indicate an incorrect binding.

The test machine has no camera module, and its microphone belongs to that module. The absent hardware therefore explains `UNATTACHED` on this particular machine. **Do not assume that every XPS 9320 with the same status has the same cause.** Machines with the module installed need separate investigation; the speaker-only workaround below is not a general microphone fix.

## Physically absent module only: speaker-only UCM

On the test machine, ACP probing could not establish the HiFi profile because the microphone was absent. The optional UCM keeps only the dual-RT1316 Speaker and HDMI devices.

This configuration was validated only on board `0V0059`. The tool checks the model, board and microphone status, and backs up existing files:

```bash
sudo python3 -I scripts/ucm.py install --no-microphone
systemctl --user restart wireplumber.service
python3 scripts/install.py check
```

The files are `/usr/share/alsa/ucm2/conf.d/sof-soundwire/DellInc.-XPS9320-0V0059.conf` and `/usr/share/alsa/ucm2/sof-soundwire/HiFi-xps9320.conf`. Original files are backed up under `/var/lib/xps9320-audio-ucm`. Check that the custom configuration remains effective after system updates. The tool does not overwrite the package's generic UCM files.

Do not manually unbind/rebind `rt715-sdca` to recover a physically absent microphone. During research, that operation removed the machine-card `Speaker Switch` control and prevented the previously working speaker HiFi profile from loading. Normal installation in this repository neither rebinds devices nor resets the PCI controller.

## Failed prerequisite checks

- `Missing command`: install the dependencies listed in the README.
- `Missing LSP stereo limiter`: check `lsp-plugins-lv2` and Lilv's search path. Do not substitute a different limiter or change oversampling enumeration values without validation.
- `stock HiFi speaker sink is missing`: resolve the kernel/SOF/UCM issue first. The DSP installer does not repair the baseline sound card.
- Model/CPU mismatch: this preset and build target were not validated on your hardware; the installer does not relax those checks automatically.
- Nondefault XDG directories: this release supports standard user directories only. Do not use sudo to bypass the check.
- `Another XPS DSP config exists`: back up and disable the duplicate configuration first, so two processing chains do not stack.
- Existing installation backup: uninstall using the original installation or inspect the backup before proceeding. Do not overwrite the rollback record.

## Keep diagnostic information private

The installer does not ask for system passwords or GitHub tokens. Only the optional system UCM operation uses local sudo authentication; do not put passwords in scripts or issues. Test fixtures are deterministic synthetic signals, not microphone recordings.
