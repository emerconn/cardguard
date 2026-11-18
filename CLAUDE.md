# cardguard

See README.md for the build, tools, hardware budget and decision log. Record new design or
architecture decisions in the README's Decisions table.

## Git workflow

`main` is tested and validated. A ruleset on `main` blocks direct and force pushes and requires a
pull request whose `phone-page` check (the `Tests` workflow) passes.

1. Branch from `origin/main`, commit, push the branch, open a PR into `main`.
2. Wait for the `Tests` workflow. Merge only when it passes; a failing PR is not merged.
3. Merge with `gh pr merge --squash` (the only method the repo allows). The PR title becomes the
   commit on `main`, the PR description its body. GitHub deletes the branch after the merge.
4. Firmware changes: flash and run `test/hardware/smoke.py` locally first (CI has no board), and
   say so in the PR.

Commit messages and PR titles use [Conventional Commits](https://www.conventionalcommits.org/):
`type(scope): imperative summary`, e.g. `fix(ble): treat brightness as a raw byte only`.

- Types: `feat`, `fix`, `perf`, `refactor`, `test`, `ci`, `build`, `docs`, `style`, `chore`,
  `revert`; `!` after the scope marks a breaking change.
- Scopes: `ble`, `display`, `guards`, `power`, `page` (phone page), `case`, `fonts`, `tools`,
  `tests`. Leave the scope out when none fits.

GitHub Actions job names are lowercase with dashes (`phone-page`): use the job id, no separate
`name:`. A required check in the `main` ruleset refers to the job name, so rename it in the ruleset
before merging a job rename.

## When the user gives a new design (reference image)

1. Save the reference image to `assets/<design_name>.png` (snake_case), as given. It is kept to
   compare against and improve the design later.
2. Add a scene to `src/guards.cpp` using `src/smooth.h`, reuse the shared helpers (suits, colours,
   `duck`, ...), and append it to `GUARDS` and its name to `GUARD_NAMES` (the phone page reads them from the board). Text uses only the shared `TextStyle`s (`BAND`,
   `BAND_LONG`, `HEADLINE`, `LABEL`, `CARD_INDEX`, `CARD_RANK`, `ALERT`, `BIG_NUMBER`), never an ad-hoc font or scale;
   small serif text goes blurry.
3. New characters in text: add them to the lists in `tools/fonts.sh` and run it.
4. Check it with `tools/preview/run.sh` (view `preview/guard_N.png`, scaled up) against the reference.
5. Flash and time it: `pio run -t upload && python3 tools/drawtime.py`. Keep designs well under ~500 ms.

## Case (`case/`)

The STLs are committed so the Windows slicer (Creality Print) can pull them. Whenever
`case/cardguard_case.scad` changes, regenerate both `case/shell.stl` and `case/lid.stl` and commit them in
the same commit, so every print maps to a design version.
