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
| `flow scripts self-test` | `ci.yml` | The two scripts above reject known-bad input ([`tools/flow-selftest.sh`](../tools/flow-selftest.sh)). |

The repository should allow rebase merging only. Branch protection on `main` should require `CI gate` and `flow / conventions`, require branches to be up to date, and forbid force pushes. GitHub only lets the repository owner set that, so until it is set the checks report but do not block. That is tracked in the `needs-owner` issue for it.

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

## Releases

Releases are cut only by [`tools/release.sh`](../tools/release.sh), and only once the consumers have been built against the change. The script refuses to run without `--consumers-verified`, which is a claim it cannot check, so it is on whoever passes it. It does not push to `main`: the version bump goes through a `release/vX.Y.Z` pull request, is merged when green, and the tag lands on the merge commit. See the [README](../README.md#releases) for usage.

## Things only the owner can supply

Credentials, hardware results, money and irreversible actions are not guessed at. They get an issue labelled `needs-owner`, and the work carries on around them.
