# Validation scope

## Reproducible checks in this repository

1. `scripts/build.py` verifies SHA256 hashes of the preset data and LV2 metadata, then builds five plugins.
2. `tests/test_package.py` uses a fixed synthetic 4,096-frame stereo input and compares output hashes against the previously validated deployed plugins. Dynamics controls cover 0, −3.93007922, −6, −20 and −40 dB.
3. The same input produces identical results with chunks of 1, 31, 256, 1,024 and 4,096 frames. Unsupported 32 kHz requests are rejected, and plugin descriptor URIs must match their metadata.
4. File transaction tests cover restoration of original files, removal of newly installed files, detection of later user edits, rejection of path traversal and symbolic links, and installation rollback after an injected graph startup failure.
5. `tests/smoke_pipewire.py` runs the complete graph with freshly built plugins and the system LSP limiter in an isolated PipeWire instance. It excludes the user's `~/.lv2`, does not access audio hardware and does not restart live audio services.

Before publication, all nine tests passed locally. The isolated graph captured 118,784 frames, all finite, with a sample peak of 0.8891814. This short test establishes that the packaged graph works; it is not a hardware playback or long-term stability result.

## Evidence from the original research

The following checks were performed during development. This public repository retains the necessary synthetic regression fixtures and source fingerprints, rather than the entire private experiment directory:

- After aligning the actual host's additional startup silence, 315,904 frames of core output matched the original APO entry point byte for byte. The drain tail after input completion in a standard recording still had small differences. This does not establish full-recording bit equality in every scenario.
- The Linux control model uses endpoint `volume * max(channelVolumes)`, clamped to −40…0 dB. It does not equate Linux UI percentages with Windows UI percentages.
- A live digital-silence test verified application → DSP → Speaker routing, ERR=0, volume/curve tracking through 86% → 85% → 86%, and restoration of the original volume.
- A five-minute isolated run completed 603 curve-control writes successfully. All node error counters remained zero, no internal all-zero run exceeded 50 ms, and all samples were finite. No sound-card hardware participated in that test.
- True peak in the five-minute test was measured only in three 20-second excerpts. The maximum was 0.900781, approximately −0.908 dBFS; this was not a whole-recording true-peak measurement.

## What has not been established

- Complete hardware playback validation after suspend/resume.
- Long-term listening, high-volume acoustic behavior, or equivalent thermal/excursion protection.
- Power consumption or battery-life improvements. Reduced CPU cycles are not measured watts.
- Microphone recording: the test machine's camera/microphone module is physically absent.
- Equivalence across all Windows runtime presets, communication paths, scene changes and audio policies.
- Compatibility with other models, acoustic assemblies or software versions.

## Implementation details

The current graph is `0.5 → input crossover → bass/3D → HF → flat PEQ → 2 → dynamics-linux → LSP True Peak/24`. The 3D and PEQ stages use FTZ/DAZ locally and restore the caller's floating-point environment. The dynamics plugin is compiled with `-O3 -march=x86-64-v3 -ffp-contract=off -fno-fast-math`.

`docs/source-provenance.json` records fingerprints of the original research source files. The public version removes private paths and outdated comments, and makes the HF plugin's default URI match its published metadata. Git history tracks subsequent changes.
