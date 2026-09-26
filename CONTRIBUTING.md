# Contributing to Kickarse

Thanks for wanting to help improve Kickarse! This project is licensed under
**GPL-3.0** (see [LICENSE](LICENSE)) specifically so anyone can use it, study
it, and build on it — and so improvements flow back to the project instead of
disappearing into a closed fork.

## Ground rules

- **License**: by submitting a contribution (pull request, patch, or commit),
  you agree it is licensed under GPL-3.0, same as the rest of the project.
  You keep copyright on your own contribution; you're granting the project
  (and everyone downstream) the same GPL-3.0 rights you received.
- **Attribution**: your contribution is credited to you via git history and
  GitHub's contributor graph. Please keep the top-of-file copyright/license
  header intact in files you modify, and add yourself to a `Co-authored-by:`
  trailer or the commit author field rather than editing existing headers.
- **No silent forks going closed-source**: that's the whole point of GPL-3.0
  — if you distribute a modified build, your changes must stay open under the
  same license.

## Before you start

For anything beyond a small fix (typo, obvious bug), please open an issue
first to discuss the approach — especially for DSP changes, new modes, or
anything touching the editor UI. This avoids wasted work on both sides.

## Building

See the [README](README.md#building-from-source) for build requirements
(Visual Studio 2022 Build Tools, CMake ≥ 3.22, git) and build steps.

## Submitting changes

1. Fork the repo and create a branch off `main`.
2. Keep commits focused — one logical change per commit, clear message.
3. Test your change locally (build all three formats if the change touches
   shared code; VST3 alone is fine for editor-only tweaks).
4. Open a pull request describing what changed and why. Reference any
   related issue.

## Reporting bugs

Open a GitHub issue with: your DAW + version, plugin format (VST3/VST2/CLAP),
steps to reproduce, and what you expected vs. what happened. A screenshot or
short clip of the editor helps a lot for UI issues.

## Code of conduct

Be respectful, keep feedback constructive, and assume good faith. Disruptive
or abusive behavior toward other contributors will get you blocked from the
repo.
