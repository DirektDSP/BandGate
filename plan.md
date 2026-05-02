# Relay Delay Feature Plan (Plugin-First, Host-Agnostic)

## Goal

Implement a "relay" effect module that sits between delay and reverb by combining:

- Short diffusion in the delay feedback path.
- OTT-style multiband upward/downward dynamics inside feedback.
- Modulation (chorus on diffuser output, flutter on diffuser internals).
- Tonal shaping filters in the loop.

This should be designed as a **per-band effect block** that can be enabled later in the existing multiband architecture, not as a one-off monolithic delay.

## Scope (What We Will Build)

- Core delay line with feedback >100% capability (musical but bounded internally).
- Diffusion network with:
  - User diffusion time control (sets minimum round-trip time).
  - Internal high-order damping LPF (60 dB/oct character target).
  - Flutter modulation on allpass filter parameters.
- Feedback-path dynamics:
  - OTT-style multiband compressor/expander behavior.
  - Mix/amount control to create swells without relying only on feedback gain.
- Additional loop filters:
  - 12 dB/oct HPF.
  - 12 dB/oct LPF.
- Chorus modulation after diffusion stage.
- Input gain staging that changes how quickly new audio replaces loop content.
- Clear feedback action (automatable parameter).
- Time modes:
  - Free ms mode.
  - Sync division mode.
  - Round-trip display model that accounts for diffusion floor.

## Out of Scope (For This Project Plan)

- Any Ableton-specific integration (theme color sync, Live-specific UX assumptions, scripting, device wrappers).
- Host-specific UI behavior outside standard plugin automation and parameter exposure.

## Additional Backlog From `todo`

- Installer persistence:
  - Fix installer behavior so install folder selections persist between installer runs.
- Windows packaging scope:
  - For installer builds, package only required plugin formats on Windows.
  - Do not include VST, CLAP, or Standalone in Windows installers when not needed by release policy.
- DSP feature extensions:
  - Implement per-band sidechaining.
  - Add threshold tilt control(s) for band-aware threshold shaping.
  - Support FFT-size selection per band (instead of one global FFT size).

## High-Level Architecture

Add a new DSP subsystem, then integrate it as an optional per-band processor:

1) New module: `RelayDelayCore`

- Stereo-safe processing, sample-accurate modulation.
- Components:
  - Delay buffer + feedback router.
  - Diffuser (`N` allpass stages, short times).
  - Damping filter (steep LPF in diffusion path).
  - Feedback multiband dynamics (OTT-like).
  - Loop HP/LP (12 dB/oct each).
  - Chorus block (post-diffusion).
  - Parameter smoothing and tempo-aware time mapping.

1) Per-band wrapper: `RelayBandProcessor`

- Thin adapter with:
  - prepare/reset/process.
  - parameter update API.
  - optional enable/bypass.
- Designed so each active band can host its own relay instance later.

1) Integration path in current codebase

- Keep existing splitter and per-band pipeline intact.
- Insert relay stage as optional post-gate processor per band.
- Start with global single instance fallback (feature flag), then expand to true per-band.

## Parameter Spec (Initial)

Global/shared:

- `RELAY_ENABLE`
- `RELAY_CLEAR` (momentary trigger semantics)
- `RELAY_TIME_MODE` (free/sync)
- `RELAY_TIME_MS`
- `RELAY_TIME_SYNC_DIV`
- `RELAY_FEEDBACK`
- `RELAY_INPUT_GAIN`
- `RELAY_MIX`

Diffusion/filter/mod:

- `RELAY_DIFFUSION_TIME`
- `RELAY_DAMPING`
- `RELAY_FLUTTER_RATE`
- `RELAY_FLUTTER_DEPTH`
- `RELAY_CHORUS_RATE`
- `RELAY_CHORUS_DEPTH`
- `RELAY_LOOP_HPF`
- `RELAY_LOOP_LPF`

Dynamics:

- `RELAY_OTT_AMOUNT`
- `RELAY_OTT_BANDS` (or fixed split internally for v1)
- `RELAY_OTT_TIME` (attack/release macro or profile choice)

Per-band extension set (for later phase):

- `BANDx_RELAY_ENABLE`
- `BANDx_RELAY_SEND`
- `BANDx_RELAY_FEEDBACK_TRIM`
- (Optionally) per-band time offset/mod depth

## DSP Design Notes

- Delay round-trip time:
  - Effective cycle time = delay time + diffusion smearing contribution.
  - Clamp minimum cycle to diffusion-dependent floor to avoid zero-time artifacts.
- Feedback stability:
  - Soft saturation and/or gain guard in feedback path.
  - OTT amount should be able to "inflate" tail without unstable runaway.
- Diffusion:
  - Prefer modulated allpass chain with bounded coefficient ranges.
  - Flutter should modulate allpass parameters slowly/randomly to avoid metallic ringing.
- Chorus:
  - Apply after diffusion output before re-entry/output tap.
  - Keep modulation shallow by default to preserve pitch center.
- Filters:
  - 12 dB/oct loop HP/LP for broad tone shaping.
  - Separate steep damping LPF integrated in diffusion stage for texture control.

## Implementation Phases

### Phase 1 - Foundations

- Add relay parameter IDs and APVTS entries.
- Build `RelayDelayCore` skeleton with no-op pass-through and tests.
- Add clear trigger handling and state reset behavior.

### Phase 2 - Delay + Diffusion

- Implement circular delay and feedback router.
- Add short allpass diffuser and diffusion-time mapping.
- Add damping LPF (steep topology target).

### Phase 3 - Feedback Tone + Dynamics

- Add 12 dB/oct loop HP/LP filters.
- Implement OTT-like multiband dynamics in feedback path.
- Add guardrails (saturation/ceiling/anti-denormal).

### Phase 4 - Modulation and Time Modes

- Add chorus stage and flutter modulation.
- Implement free vs sync timing and diffusion-compensated mapping.
- Surface round-trip computed time metric for UI display.

### Phase 5 - Integration Into Existing Multiband Chain

- Integrate as global processor first (feature flag).
- Add per-band relay wrapper and routing/sends.
- Enable per-band instances with CPU-budget limits and bypass optimization.
- Implement per-band sidechain routing/controls.

### Phase 6 - UX and Preset Readiness

- Expose parameter groups cleanly in GUI.
- Add sensible defaults and musical macro ranges.
- Add preset migration/versioning support for new parameters.
- Add threshold tilt UX and automation mapping.
- Add per-band FFT controls (or constrained options by quality mode).

### Phase 7 - Packaging and Installer Policy

- Ensure installer install-directory persistence works correctly.
- Align Windows installer output with format policy (exclude VST/CLAP/Standalone when not intended).
- Add CI/package checks that validate expected artifact lists per platform.

## Testing Strategy

- Unit tests:
  - Delay time mapping and sync conversion.
  - Diffusion minimum round-trip behavior.
  - Clear action fully drains/reset feedback buffers.
  - Feedback guardrail prevents NaN/Inf/runaway.
  - Per-band FFT parameter mapping and state restore.
  - Threshold tilt mapping curve behavior.
- Audio behavior tests:
  - Impulse response becomes progressively smeared each cycle.
  - OTT amount increases tail density without hard clipping.
  - Flutter/chorus produce controlled modulation, no zippering.
  - Per-band sidechain engages targeted bands as expected.
- Performance tests:
  - CPU at 44.1/48/96 kHz, max block size, max bands.
  - Validate per-band scaling and bypass savings.
  - Validate per-band FFT overhead stays within budget.

## Risks and Mitigations

- Instability at high feedback:
  - Add internal limiter/saturator and hard gain cap in loop.
- Metallic diffusion artifacts:
  - Randomized/modulated allpass tuning and damping defaults.
- CPU growth with per-band instances:
  - Start global, then per-band opt-in; add quality modes.
- CPU spikes from per-band FFT:
  - Restrict FFT options by band count/quality mode and smooth transitions.
- Parameter explosion:
  - Use macro controls first; expose advanced controls gradually.
- Packaging drift across platforms:
  - Add artifact allowlists and CI assertions for installer contents.

## Deliverables

- `RelayDelayCore` DSP class + tests.
- APVTS parameter expansion and serialization support.
- Initial GUI controls for relay section.
- Global relay mode (v1) and per-band relay mode (v2).
- Presets demonstrating short-space, blurred delay, and feedback bloom textures.
- Per-band sidechain + threshold tilt + per-band FFT feature set.
- Installer behavior fixes and Windows packaging policy enforcement.

## Suggested Build Order (Pragmatic)

1. Global relay (single instance) with delay+diffusion+damping.
2. Add loop filters + OTT amount macro.
3. Add chorus/flutter modulation.
4. Add sync mode + round-trip display math.
5. Split to per-band architecture and optimize CPU.
6. Add per-band sidechain, threshold tilt, and per-band FFT.
7. Finalize installer persistence and Windows artifact policy.
