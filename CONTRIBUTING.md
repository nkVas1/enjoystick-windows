# Contributing to Enjoystick Windows

Thank you for your interest in contributing! This project holds itself to a high standard of engineering and design quality. Please read this guide before opening a PR.

## Code Quality Bar

- **Language standards**: C++20 (core), C# 12 / .NET 8 (shell/logic)
- **No warnings policy**: all warnings treated as errors in CI
- **RAII everywhere** in C++; no raw owning pointers
- **Async-first** in C# — `async/await` throughout, no `.Result` / `.Wait()`
- **Input path** (daemon → router → UI) must sustain <5ms round-trip latency

## Pull Request Checklist

- [ ] New logic has unit tests (`/tests`)
- [ ] UI changes include a screenshot or recording in the PR description
- [ ] Performance-critical paths are profiled (attach ETW trace or benchmark)
- [ ] Design tokens used — no hardcoded colors or spacing values
- [ ] Localization strings added to `/src/shared/Strings.resw`
- [ ] `CHANGELOG.md` updated under `[Unreleased]`

## Commit Convention

We use [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(shell): add radial quick menu animation
fix(inputrouter): correct deadzone calculation for PS5 triggers
perf(daemon): reduce polling loop overhead by 40%
docs: update architecture diagram
```

Types: `feat`, `fix`, `perf`, `refactor`, `test`, `docs`, `chore`, `ci`

## Design Contributions

- All visual work should align with the **Enjoystick Design System** (`/docs/design/`)
- Icon proposals should be submitted as SVG + source file
- Motion spec: always define duration, easing, and trigger conditions

## Branch Strategy

```
main          → stable, release-ready
develop       → integration branch
feature/*     → feature work
fix/*         → bug fixes
release/*     → release prep
```
