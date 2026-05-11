# Release workflow

Releases are tag-driven and fully automated. Pushing a `v*` tag triggers
[release.yml](workflows/release.yml), which builds the driver DLL,
publishes a GitHub release, and verifies the published body matches the
input. The release body is generated from `git log` between the previous
tag and the current tag plus the templated evergreen sections in
[release-template/](release-template/) -- there is no hand-written
narrative path. If a release needs content that the auto-generator can't
produce, the only supported path is the [extras file](#extras-file).

This repo is the shared SteamVR driver consumed by sibling consumer
overlays (OpenVR-WKSpaceCalibrator, OpenVR-WKSmoothing, OpenVR-WKInputHealth)
via submodule. End users typically receive this driver as part of one of
those overlays' installers; standalone driver releases here are mainly
for paired-bump coordination across the consumer overlays.

## Tag shapes

| Form | When | Example |
|---|---|---|
| `vYYYY.M.D.N` | Release. `.N` is the release iteration for that calendar day, starting at 0. | `v2026.5.6.0` |
| `vYYYY.M.D.N-XXXX` | Dev. `.N` is local build count; `XXXX` is a 4-hex UID. Rare on the release stream. | `v2026.5.6.0-A1B2` |

[build.ps1](../build.ps1) validates the shape and fails fast on
malformed tags.

## Body composition

The release body is the verbatim output of
[Generate-ReleaseNotes.ps1](scripts/Generate-ReleaseNotes.ps1). Layout:

```
# OpenVR-WKPairDriver <tag>

## What's Changed

### Features
- feat(...): subject by @author in <sha>

### Bug Fixes
- fix(...): subject by @author in <sha>

**Full Changelog**: <compare-url>

## File integrity
<table from build.ps1's manifest.tsv>

## More
<links.md template>

## Install (fresh)
<install.md template>

## Uninstall
<uninstall.md template>

## What you need to do
<what-you-need-to-do.md template>

[--- Additional notes (extras file, if present) ---]
```

CHANGELOG.md keeps accumulating in-repo for browsing; only the public
release body shrinks to just-this-version content.

## Conventional-commit policy

Commits in the tag range get bucketed by the prefix on their subject line:

| Prefix | Section |
|---|---|
| `feat(...)?:` | Features |
| `fix(...)?:` | Bug Fixes |
| `perf(...)?:` | Performance |
| `refactor(...)?:` | Refactors |
| `revert(...)?:` | Reverts |
| `docs(...)?:` | Documentation |
| `style(...)?:` | Style |
| `test(...)?:` | Tests |
| `ci(...)?:` | CI |
| `build(...)?:` | Build |
| `chore(...)?:` | Chores |
| anything else | Other Changes |

Subjects starting with `<prefix>!:` (the breaking-change marker) match
the same section. Trailing version-stamps like ` (2026.5.6.0-A1B2)` are
stripped before grouping. Commits with `[skip changelog]` in the subject
are excluded entirely; merge commits are excluded by `--no-merges`.

## Author handle remap

The generator emits `by @<author>` from the commit's `%an` field. The
local git config uses the brand "WhyKnot"; the GitHub login is
"RealWhyKnot". The generator carries an `$AuthorHandleMap` hashtable
that remaps known git authors to the right login before emit.

## Scrub gates

After the body is composed, three gates run before the workflow proceeds:

1. **ASCII normalisation.** Common typographic patterns (em-dash,
   en-dash, ellipsis, smart quotes, etc.) are substituted to ASCII
   equivalents. One-way, silent.

2. **Non-ASCII fail.** Anything left outside printable ASCII (0x20-0x7E
   plus tab) after normalisation fails with the offending line, column,
   and Unicode code point.

3. **Voice + internal-only-vocabulary check.** Pattern groups match
   case-insensitively against the composed body. Any match fails the
   script. Fix by amending the offending commit subject (or extras
   file) to use plainer language, OR mark the commit `[skip changelog]`.

The scrub list lives in
[Generate-ReleaseNotes.ps1](scripts/Generate-ReleaseNotes.ps1).

## Empty-slice guard

If the tag range yields zero qualifying commits, the script throws.
Escape hatch: `-AllowEmpty` for the very first release.

## Extras file

For content that the auto-generator can't capture -- coordination notes
for paired releases with consumer overlays, migration instructions,
links to a long-form wiki page -- create a markdown file at
`.github/release-extras/<tag>.md` BEFORE pushing the tag.

```
.github/release-extras/v2026.5.6.0.md   <- created before tag push
```

Appended verbatim below the auto section with `---` separator and
`## Additional notes` heading. Same scrub gates apply.

## Post-publish verification

The workflow re-fetches the published body via `gh release view` and
compares it SHA256-to-SHA256 against the input. The verify step uses an
exponential backoff retry loop (2+4+8+16+32 = 62s budget) because
GitHub's release-body read-after-write isn't strictly consistent -- right
after a write the next read can return a stub for several seconds before
the API settles.

If the body still differs after the retry budget: auto-correct via `gh
release edit`, re-run the retry loop, fail loud if it still differs.

## Promote-back to main

The workflow pushes the promoted `CHANGELOG.md` and `wiki/Changelog.md`
back to `main` via the GraphQL `createCommitOnBranch` mutation. The
resulting commit is signed server-side with GitHub's bot key, so it
lands as verified=true.

## Coordination with consumer overlays

When this driver releases, the consumer overlays (SpaceCalibrator,
Smoothing, InputHealth) typically need a paired bump:

1. Tag this driver: `git tag -a v2026.M.D.N origin/main && git push origin v2026.M.D.N`.
2. In each consumer's checkout: `git submodule update --remote OpenVR-WKPairDriver`,
   commit the new submodule SHA with a `chore(deps): bump OpenVR-WKPairDriver to <sha>`
   subject, push.
3. Tag and release each consumer at its own version when ready.

The shared driver's per-build version stamp + version-gate in each
consumer's installer means a user with multiple consumers installed
never accidentally downgrades the shared driver by updating one of
them.

## Failure modes + remediations

| Symptom | Fix |
|---|---|
| `No commits found in range` | Check the tag's parent reachability. Either prev-tag detection failed or every commit is `[skip changelog]`. |
| `Non-ASCII characters in release body after normalisation` | Amend the offending commit subject, force-push the tag at the new SHA. Or add the char to `$asciiSubs`. |
| `Voice or internal-only-vocabulary patterns in release body` | Amend the offending commit subject. Or `[skip changelog]` it if unavoidable. |
| `Release body still differs after auto-correct + retries` | A GitHub-side issue. Compare the input file in runner artifacts against what `gh release view` returns. |
| `createCommitOnBranch returned GraphQL errors` | Usually a stale `expectedHeadOid`. Release itself is fine; re-run the promote step or re-run the workflow. |

## Updating the workflow

The workflow + scripts are versioned alongside the code. Workflow
plumbing fixes MUST ride along with a genuine customer release; never
tag a release purely to test workflow plumbing. Use `workflow_dispatch`
against a draft release for plumbing tests instead.
