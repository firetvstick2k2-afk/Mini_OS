# SI4735 DSP Patch Profiles

ATS Mini OS contains three selectable DSP patch profiles:

| Profile | AM | SSB |
|---|---|---|
| Standard | SI4735 stock AM | Existing embedded SSB patch |
| V1 | `v1patch_am_continuous.bin` | `v1patch_ssb_continuous.bin` |
| V2 | `v2patch_am_continuous.bin` | `v2patch_ssb_continuous.bin` |

The four supplied binary files are retained in the repository root as reference/source assets. The firmware embeds their Base64 representations in `src/tuner/PatchBinaryData.h` and decodes the selected patch into a bounded RAM buffer before transfer. Patch framing is checked before the SI4735 is touched.

Patch loading stays inside `TunerDriver` / `TunerSI4735`:
- SSB uses the PU2CLR `loadPatch()` sequence.
- AM uses the AM-specific power-up and `downloadPatch()` sequence ported from ATS Mini Dev. It must not use `loadPatch()`, because that helper applies SSB-specific configuration.
- Switching back from an experimental AM patch to Standard explicitly restores stock AM.
- The selected profile is stored in the dedicated `atsmini_os` NVS namespace under `dsp_patch`.

The selection is available under **Radio → DSP-Patch**. Standard remains the default. If an experimental patch fails validation or transfer setup, the manager falls back to Standard.

These supplied patches are experimental. Structural validation confirms the framing and sizes only; it does not certify operation on every SI4735/SI4732 hardware revision. Hardware testing is still required.
