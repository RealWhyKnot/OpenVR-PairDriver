# Changelog

Mirror of the root `CHANGELOG.md`, kept in lock-step by `.github/scripts/Update-Changelog.ps1`. The GitHub Wiki picks this file up via `.github/workflows/wiki-sync.yml` if a wiki is wired up later.

## Unreleased

### Added
- **inputhealth:** Publish scalar observed range (0.1.0.0) (3f6f42e)
- **driver:** InputHealth Stage 1E reset wiring and Stage 1F shmem publisher (0.1.0.0) (96a2829)
- **driver:** InputHealth Stage 1D -- per-tick stats wired into detour bodies (0.1.0.0) (ceb1416)
- **driver:** InputHealth Stage 1C -- pure-function math primitives (0.1.0.0) (050bc5c)
- **driver:** InputHealth Stage 1B -- install boolean/scalar hooks (pass-through) (0.1.0.0) (16c17ae)
- **driver:** Seed InputHealth subsystem (Stage 1A scaffold) (0.1.0.0) (26c42a4)
- **driver:** Split per-feature pipes and gate subsystems on enable_*.flag (d731767)
- **inputhealth:** Share snapshot diagnostics helpers (0.1.0.0) (5f2d770)
- **inputhealth:** Per-component learned compensation push (0.1.0.0) (a32e1c4)
- **overlay:** Full UI overhaul -- sidebar, cards, theme, typography (0.1.0.0) (e6a8c8e)

### Changed
- Rename export macro to OPENVRPAIRDRIVER_EXPORTS (c7df7e6)
- Rename driver entry files to OpenVR-PairDriver (0.1.0.0) (52e05f7)
- **inputhealth:** Split hook injector by concern (0.1.0.0) (102a28c)
- **repo:** Become the glue that composes feature modules into one binary (0.1.0.0) (9d493f7)
- **deps:** Bump SpaceCalibrator submodule for BuildStamp.h fix (0.1.0.0) (e3ef50a)
- Revert "feat(overlay): full UI overhaul -- sidebar, cards, theme, typography" (0.1.0.0) (4582786)

### Fixed
- **inputhealth:** Make observation fail open (0.1.0.0) (a01318b)
- **build:** Wrap cmake calls with EAP='Continue' to dodge PS 5.1 ErrorRecord wrap (0.1.0.0) (bded14d)
