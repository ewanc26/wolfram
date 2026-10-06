# Working flow

This is how changes get into Wolfram, and into the four repos that depend on it ([metalbear](https://github.com/ewanc26/metalbear), [cobalt](https://github.com/ewanc26/cobalt), [indigo](https://github.com/ewanc26/indigo), [platinum](https://github.com/ewanc26/platinum)). Wolfram holds the canonical copy; the others copy it and a check tells them when they have drifted.

The short, agent-facing version is the `flow` block in [AGENTS.md](../AGENTS.md). If the two disagree, fix this page.

## The path of a change

1. Branch from `main`, named `type/slug`: `feat/muted-words`, `fix/relay-macos`. Types are `feat fix docs ci chore refactor test perf build ui release`.
2. Commit in small, focused steps. Subjects are [Conventional Commits](https://www.conventionalcommits.org/): `fix(sync): close the socket on a short read`. I do not push empty commits.
3. Open a pull request against `main` using the template. The title is a Conventional Commit too.
4. Wait for CI. Nothing merges red.
5. Rebase-merge the pull request. Never squash, never a merge commit. Nothing is pushed to `main` directly, releases included.

Because a rebase merge puts every commit on `main` exactly as I wrote it, each one has to stand alone: a conventional subject, a tree that builds, tests that pass. Review fixes are real commits (`fix(sync): ...`), not "address review". I do not force-push, so I cannot rebase a PR branch, and I do not merge `main` into one, because a merge commit breaks the rebase merge (the flow check rejects it). If a PR falls behind and GitHub can still rebase it cleanly, it merges once CI is green on its current head. If not, I cut a fresh branch from `main`, cherry-pick the commits, open a new PR that links the old one, and close the old one with a comment.

If `main` goes red, that is fixed before anything else is merged.

## What is checked, and where

| Check | Where it lives | What it enforces |
| --- | --- | --- |
| `CI gate` | `ci.yml` | Every build and test job passed. One name to require, so adding a job does not mean editing branch protection. |
| `flow / conventions` | `flow.yml`, [`flow-reusable.yml`](../.github/workflows/flow-reusable.yml) | Branch name, PR title, PR body sections, commit subjects, no empty commits, no merge commits ([`tools/flow-check.sh`](../tools/flow-check.sh)). |
| `flow / drift` | same | The PR template and the AGENTS.md flow block match Wolfram's ([`tools/flow-drift.sh`](../tools/flow-drift.sh)). Skipped in Wolfram itself. |
| `flow / style` | same | README header, section order and logo match [house-style.md](house-style.md) ([`tools/style-check.sh`](../tools/style-check.sh)). |
| `flow scripts self-test` | `ci.yml` | The scripts above reject known-bad input ([`flow-selftest.sh`](../tools/flow-selftest.sh), [`style-selftest.sh`](../tools/style-selftest.sh)). |

The repository should allow rebase merging only. Branch protection on `main` should require `CI gate`, `flow / conventions` and `flow / style`, require branches to be up to date, and forbid force pushes. GitHub only lets the repository owner set that, so until it is set the checks report but do not block. That is tracked in the `needs-owner` issue for it.

## Using it from another repo

Add `.github/workflows/flow.yml`:

```yaml
name: flow
on:
  pull_request:
    types: [opened, edited, synchronize, reopened]
permissions:
  contents: read
jobs:
  flow:
    uses: ewanc26/wolfram/.github/workflows/flow-reusable.yml@main
```

Then copy `.github/PULL_REQUEST_TEMPLATE.md` and the `<!-- flow:begin -->` block of `AGENTS.md` from Wolfram, byte for byte. To change the flow, change it here first, then copy it out; the drift check in the other repos fails until they have.

## PR body

The template has three sections that must not be left empty: what changed, what was verified, and which docs moved. "Verified" means what was actually run, and on what: host, emulator or hardware. If I did not run it on a Wii U, the PR does not say I did.

## Changelog

`CHANGELOG.md` is in the [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) shape, with `## [Unreleased]` at the top grouped into Added, Changed, Fixed, Removed and Security. Every PR that changes anything outside `docs/`, `.github/`, `test/`, `tests/` and Markdown files adds or edits a line there, written as me and linking the PR or issue. A change nobody using the code could notice can say so instead, with a line in the PR body: `Changelog: none — <reason>`, and the check prints the reason. `flow / changelog` ([`tools/changelog-check.py`](../tools/changelog-check.py)) enforces that, and also that every version tag has a section and every section has its tag.

## Releases

A release happens whenever a merged, green set of changes is something a consumer needs: a feature, a fix, anything a pin bump is waiting on. Docs-only and CI-only changes don't count unless a consumer needs them. The version follows semver; while the major version is 0, a breaking change bumps the minor.

Releases are cut only by [`tools/release.sh`](../tools/release.sh), in two steps. `prepare` runs the builds and tests, bumps the version, moves Unreleased into a dated section and pushes a `release/vX.Y.Z` branch; that goes through a pull request like any change. `publish`, once the PR is merged, creates the GitHub release (GitHub makes the tag) at the merge commit only if its `CI gate` is green, with the changelog section as notes. The `release` workflow runs it for me when CI finishes green on a merged release commit, because an agent's sandbox may not be allowed to create releases or push tags; it can also be dispatched by hand with a version. The changelog check allows 30 minutes between a release PR merging and its tag appearing, then fails, so a release workflow that broke is noticed. `prepare` refuses to run without `--consumers-verified`, which is a claim the script cannot check, so it is on whoever passes it: consumer bump branches are built and tested against the change first. Tags are never moved and a version is never re-tagged; a wrong release gets a new patch. See the [README](../README.md#releases) for usage.

## Labels and repository metadata

Every issue carries exactly one kind (`bug`, `enhancement`, `documentation`, `refactor`, `test`, `chore`, `question`) and at least one `area: <x>`. The rest of the taxonomy is workflow (`needs-owner`, `parity`, `duplication`), optional `impact: <x>`, and the usual triage labels. [`.github/labels.yml`](../.github/labels.yml) is the only definition; the `core` block is the same in all five repos and a repo adds labels only under `local`, as `area: <x>`. [`tools/labels-sync.sh`](../tools/labels-sync.sh) checks or applies it, renames included (renaming keeps a label on existing issues), and `flow / labels and metadata` fails when a repo's live labels differ from its file.

Description, homepage, topics and the wiki, discussions and projects switches are [`.github/repo-metadata.yml`](../.github/repo-metadata.yml), with a shared `core` and a per-repo block. The agents' sandbox cannot write repository metadata, so I apply it myself with [`tools/apply-repo-metadata.sh`](../tools/apply-repo-metadata.sh). Until `enforce_live` is set in a repo's file the live comparison only warns.

## Things only the owner can supply

Credentials, hardware results, money and irreversible actions are not guessed at. They get an issue labelled `needs-owner`, and the work carries on around them.
