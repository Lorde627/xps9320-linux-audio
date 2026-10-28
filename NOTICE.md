# Provenance and licensing scope

This is independent interoperability research for the Dell XPS 13 Plus 9320. It is not affiliated with or endorsed by Dell, Realtek, Intel, Waves, PipeWire, WirePlumber, or LSP. Product and algorithm names identify the hardware and processing paths being studied.

The original integration tooling, documentation, tests, and configurations are provided under the scoped MIT license in `LICENSE`.

`src/` contains research reconstructions and generated C transliterations of the device's APO processing routines, including multiband and volume-processing mathematics. `bundles/` includes small coefficient banks and initialization/preset data captured from the original device-specific preset path. These materials are not represented as original MIT-licensed vendor code or as carrying a vendor redistribution license. The tooling license does not grant rights to third-party material. No blanket project-wide license is asserted for these two directories.

The research input was the Dell package `XPS-13-9320-KP6R0_Win11_1.0_A15.exe`. No original DLL, executable installer, complete heap dump, Windows runtime, or vendor driver binary is shipped. `docs/source-provenance.json` records the original research source fingerprints; test fixtures are deterministic synthetic samples and output hashes.

LSP's limiter is an external system dependency; it is not vendored here. Existing SOF firmware, ALSA UCM, PipeWire, WirePlumber, LV2 and LSP retain their own licenses. The optional UCM references system ALSA UCM includes rather than copying entire upstream codec configurations.
