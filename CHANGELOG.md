# Changelog

All notable user-visible changes to OpenVR-PairDriver. The "Unreleased" section is auto-appended by `.github/workflows/changelog-append.yml` from conventional-commit subjects on `main`; tagged sections are promoted by `.github/workflows/release.yml` on a `v*` tag push.

The `release.yml` body for each tag is composed mechanically from the slice between the prior tag and the new tag plus the templated sections under `.github/release-template/`. Hand-writing release bodies is not part of the workflow.

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

### Changed
- Rename export macro to OPENVRPAIRDRIVER_EXPORTS (c7df7e6)
- Rename driver entry files to OpenVR-PairDriver (0.1.0.0) (52e05f7)

### Fixed
- **inputhealth:** Make observation fail open (0.1.0.0) (a01318b)
- **build:** Wrap cmake calls with EAP='Continue' to dodge PS 5.1 ErrorRecord wrap (0.1.0.0) (bded14d)
