# Contributing to AI Companion for O3DE

Thank you for your interest in contributing. This document covers the guidelines
and processes for both human and AI-assisted contributions.

## Code of Conduct

Be respectful, constructive, and inclusive. We are building tools that make game
development more accessible — contributions should reflect that spirit.

## Getting Started

1. Fork the repository and clone your fork
2. Register the gem with O3DE and enable EditorPythonBindings
3. Build and verify the editor loads without errors
4. Create a feature branch from `main`

```bash
git checkout -b feature/my-change main
```

## What to Contribute

We welcome:

- Bug fixes and correctness improvements
- New Python API functions, Lua scripts, or prefabs
- Documentation improvements
- Test coverage improvements
- Performance optimizations
- Safety and security hardening

If your change is non-trivial, open an issue first to discuss the approach.

## Contribution Requirements

### License Headers

Every new source file must include the appropriate copyright and SPDX header as
the very first content. See [AGENTS.md](AGENTS.md#license-headers) for the exact
format per file type.

The copyright line must be:
```
Copyright (c) Contributors to the Open 3D Engine Project.
```

The SPDX identifier must be:
```
SPDX-License-Identifier: Apache-2.0 OR MIT
```

### Licensing

By submitting a contribution, you agree that your work will be dual-licensed
under Apache-2.0 OR MIT, consistent with the project's existing licensing. Do not
introduce dependencies with incompatible licenses.

### Coding Style

Follow the conventions documented in [AGENTS.md](AGENTS.md#coding-conventions):

- **Python**: `snake_case` functions/variables, `PascalCase` classes, type hints
  on public signatures, JSON string returns from API functions
- **C++**: O3DE/AZ Framework conventions, `AZStd::` over `std::`, RapidJSON for
  JSON serialization
- **Lua**: Self-contained scripts with `Properties = { ... }` configuration

Match the existing style of surrounding code. When in doubt, read the file you
are modifying and follow its patterns.

### Safety Rules

These are non-negotiable:

- All user-supplied inputs must be validated through `safety/validators.py`
  (Python) or `InputValidator` (C++) before reaching O3DE APIs
- All mutating operations must be wrapped in undo batches
- Protected entities (EditorGlobal, SystemEntity, AZ::*) must never be modified
- Asset paths must be validated against traversal attacks
- No third-party Python dependencies — use only the standard library and `azlmbr`

### Tests

- Every new Python API function needs a test in `Tests/test_*.py`
- Every new C++ component or validator needs a test in `Code/Source/Tests/`
- Run Python tests before submitting:
  ```bash
  python -m pytest Tests/
  ```
- Existing tests must continue to pass

### Documentation

- Update `Docs/api-reference.md` for new or changed API functions
- Update `Docs/lua-scripts.md` for new Lua scripts
- Update `Docs/prefab-catalog.md` for new prefabs
- Update `CHANGELOG.md` under `[Unreleased]` for any user-facing change

### SBOM

If your change adds or removes a dependency, update `sbom.cdx.json`.

### Version Bumps

Do not bump the version in your PR unless asked. Version changes are coordinated
at release time and require updates to four files (see
[AGENTS.md](AGENTS.md#versioning)).

## Pull Request Process

1. **One concern per PR** — Keep PRs focused. A bug fix and a new feature should
   be separate PRs.
2. **Descriptive title** — Use a clear, concise title (under 70 characters).
3. **Description** — Explain what the change does and why. Include any relevant
   issue numbers.
4. **Tests pass** — Verify all existing and new tests pass locally.
5. **CHANGELOG updated** — Add an entry under `[Unreleased]` describing the change.
6. **Clean history** — Squash fixup commits. Each commit should be a coherent,
   buildable unit.

### PR Template

```markdown
## Summary
<!-- What does this PR do and why? -->

## Changes
<!-- Bulleted list of specific changes -->

## Test Plan
<!-- How was this tested? What commands were run? -->

## Checklist
- [ ] License headers on all new files
- [ ] Input validation for any new user-facing parameters
- [ ] Undo batch wrapping for mutating operations
- [ ] Tests added or updated
- [ ] Documentation updated
- [ ] CHANGELOG.md updated
- [ ] SBOM updated (if dependencies changed)
```

## AI-Assisted Contributions

AI-generated code is welcome, but held to the same quality bar as human-written
code. The following additional guidelines apply.

### Before Submitting AI-Generated Code

- **Review every line.** AI tools can produce plausible but incorrect code.
  Verify logic, edge cases, and error handling before submitting.
- **Test thoroughly.** Do not assume AI-generated tests are sufficient. Run them
  and verify they actually exercise the intended behavior, not just pass.
- **Check for hallucinated APIs.** AI models may reference O3DE functions,
  components, or EBus interfaces that do not exist. Verify all `azlmbr` calls,
  component type names, and EBus event names against the actual O3DE API.
- **Verify security properties.** Confirm that input validation and undo batching
  are present and correct. AI tools may omit safety wrappers or generate
  validators with incorrect regex patterns.

### AI-Specific Pitfalls to Watch For

| Risk | What to check |
|------|---------------|
| Hallucinated imports | Does `azlmbr.some_module` actually exist in O3DE? |
| Incorrect component names | Verify against `get_component_catalog()` output |
| Missing validation | Every user-supplied string must go through validators |
| Missing undo batch | Every mutating function needs `@with_undo_batch` or manual batching |
| Invented O3DE APIs | O3DE's Python API surface is limited; verify against engine source |
| Over-abstraction | AI tends to add unnecessary layers. Prefer direct, simple implementations |
| Stale patterns | AI may generate code using patterns from older O3DE versions |

### Disclosure

You are not required to disclose AI tool usage, but if you do, a simple note in
the PR description is sufficient (e.g., "AI-assisted"). Do not add AI attribution
to commit messages or copyright headers.

### AI Tools Working on This Codebase

If you are an AI agent or LLM-based tool, read [AGENTS.md](AGENTS.md) for
project-specific instructions including repository layout, coding conventions,
key design decisions, and performance best practices.

## Reporting Issues

Use [GitHub Issues](https://github.com/nickschuetz/o3de-ai-companion-gem/issues)
to report bugs or request features. Include:

- Steps to reproduce (for bugs)
- Expected vs actual behavior
- O3DE version and platform
- Relevant log output

## Questions

Open a discussion in GitHub Issues if you have questions about the codebase,
architecture, or contribution process. For architecture context, start with
[Docs/architecture.md](Docs/architecture.md).
