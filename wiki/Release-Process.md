# Release process

Releases are produced by `.github/workflows/release.yml` on a `v*` tag push. The workflow:

1. Promotes the `## Unreleased` heading in `CHANGELOG.md` (and the wiki mirror) to `## [vTAG] -- DATE` via `.github/scripts/Update-Changelog.ps1`.
2. Configures + builds the umbrella exe, driver DLL, and C# face-tracking host.
3. Runs every `*_tests.exe` under `build/artifacts/Release/`; any non-zero exit fails the release.
4. Stages a driver tree under `release/_stage_<version>/` with the loader-prefixed `driver_01openvrpair.dll`, the face-tracking host artifacts, the umbrella exe, and `openvr_api.dll`.
5. Builds five zip artifacts: one umbrella + four per-feature mirrors. Each mirror is identical to the umbrella plus one `enable_<feature>.flag` pre-dropped under `driver_openvrpair/resources/`.
6. Builds the NSIS installer (`OpenVR-Pair-v<version>-Setup.exe`).
7. Generates the release body via `.github/scripts/Generate-ReleaseNotes.ps1` from the promoted changelog plus per-file SHA256 manifest.
8. Publishes the umbrella release on this repo (zip + installer).
9. Mirrors the release to each sibling repo using `MIRROR_RELEASE_TOKEN`, attaching that feature's mirror zip.
10. Pushes the promoted CHANGELOG.md back to `main` via GraphQL `createCommitOnBranch` so the commit is server-side signed by GitHub's bot key.

## Mirror token

The mirror step needs a fine-grained PAT with `contents: read and write` scoped to all four mirror repos:

- `RealWhyKnot/OpenVR-WKSpaceCalibrator`
- `RealWhyKnot/OpenVR-WKSmoothing`
- `RealWhyKnot/OpenVR-WKInputHealth`
- `RealWhyKnot/OpenVR-WKVRCFT`

Store as the `MIRROR_RELEASE_TOKEN` secret on this repo (Settings -> Secrets and variables -> Actions). Without it, the umbrella release publishes but the four mirror steps fail -- acceptable for a smoke test, fix before promoting any real version. Widening the token to add a new mirror repo is an in-place edit on the PAT under `github.com/settings/tokens?type=beta`; no need to regenerate.

## Changelog discipline

The `## Unreleased` section in `CHANGELOG.md` is auto-appended by `.github/workflows/changelog-append.yml` from conventional-commit subjects (`feat`, `fix`, `perf`, `refactor`, etc.) pushed to `main`. The release workflow renames `## Unreleased` to `## [v<tag>] -- <date>` at promotion time. The promoted file is the same one that lands in `main` after publish, so `git log -- CHANGELOG.md` shows the canonical history.

Hand-editing the release body is not part of the workflow. Extra prose for a tag goes in `release/<tag>/extra-details.md` if needed; the body generator picks that up.

## Commit-message hygiene

Two hooks enforce the version-stamp convention on `.githooks/`:

- `prepare-commit-msg` appends the current `version.txt` stamp to the subject if (and only if) the subject is clean. It rejects fresh commits whose subjects already carry a stamp -- the hook is the single source of truth; pre-stamps by humans or agents go stale on the next build.
- `commit-msg` rejects subjects with more than one stamp.

`.github/workflows/commit-msg-check.yml` mirrors the rule on the server so a push that bypassed the local hooks (a fresh clone where `build.ps1` hasn't activated `core.hooksPath` yet, `--no-verify`, an API commit) still surfaces as a failed check in the Actions tab.

## Test-tag rule

Don't push test tags to the live repo. The workflow does five public releases per tag (umbrella + four mirrors) and pushes a server-signed changelog-promotion commit at the end; a "test" tag would leak all of that publicly. Validate workflow changes locally with `js-yaml` for parse, mirror the packaging step by extracting the PowerShell from the YAML and running it against the existing build artifacts, or use a private fork for end-to-end runs.
