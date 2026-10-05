# House style

Agent-facing. This is the canonical spec for how the five repos of the stack look: Wolfram, MetalBear, Cobalt, Indigo and Platinum. It is checked mechanically by `tools/style-check.sh`, which runs as `flow / style` from the shared reusable workflow. Change the spec here by PR to Wolfram, then update the others; the check fails them until they match.

## README

Order, top to bottom:

1. A centred logo: `<p align="center"><img src="docs/logo.svg" alt="<Name>" width="420"></p>`. `<Name>` is the product name exactly as it is written in the heading in step 3.
2. A centred badge row, in this order: CI workflow badge (`actions/workflows/ci.yml/badge.svg`), latest release (`img.shields.io/github/v/release/<owner>/<repo>?sort=semver`), licence (`img.shields.io/github/license/<owner>/<repo>`, label `licence`), GitHub Sponsors (`img.shields.io/github/sponsors/<owner>`). Repo-specific badges (MetalBear's container badge) come after those.
3. `# <Name>`.
4. A one-sentence description.

There is no second version badge further down the page: the release badge in the header row is the version badge.

Sections after the intro, in this order, where they apply: what it is and status, install, use, build, contributing (link `CONTRIBUTING.md`), licence. Other sections may sit between them but must not reorder them. The check compares the second-level headings it recognises (install, use/quick start, build, contributing, licence) and fails if they are out of order or contributing/licence are missing.

Prose follows the voice rules in each README: first person, plain British English, dry, specific, no hype, no emoji. Status claims must match the code.

## Logo

`docs/logo.svg`, in the existing Wolfram/MetalBear style:

- A pixel-art silhouette built only from `<rect>` elements on a 3 by 5 cell grid: every `x` and `width` is a multiple of 3, every `y` and `height` a multiple of 5.
- `viewBox` is `0 0 294 <height>`.
- `shape-rendering="crispEdges"` on the group.
- One fill class, `.logo`, and nothing else sets a fill. `#15803d` in light, `#4ade80` in dark, selected by `prefers-color-scheme`.
- `role="img"` and an `aria-label` on the root `<svg>`.

All five use the same two colours so they read as one family. The shapes differ; the palette does not.

## Icons

App icons are per platform and not covered by the checker. Derive them from the logo silhouette, in `#15803d` on a transparent or white ground, never a new colour.
