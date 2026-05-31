# Self-hosted runner for build-test CI

The `lint` workflow (SPDX headers, hygiene, Python unit tests) runs on standard
GitHub-hosted runners and is the always-on gate.

The `build-test` workflow compiles the gem's C++ and runs its C++ unit tests
(including the `BusSchema` introspection tests). That needs the full O3DE 26.05
SDK, a host project, and the 3rdParty packages, none of which exist on
GitHub-hosted runners. It therefore runs only on a **self-hosted** runner and is
**opt-in**.

## When it runs

- On `workflow_dispatch` (manual trigger), or
- On a pull request that a maintainer has labelled `ci:build`.

Ordinary contributors are never blocked by missing hardware: without the label
and without the runner, the job simply does not run.

## Runner requirements

Register a self-hosted runner with the labels `self-hosted` and `o3de`, on a
machine that has:

- The O3DE 26.05 SDK (with `scripts/o3de.sh` and `bin/Linux/profile/Default/AzTestRunner`).
- A host O3DE project to build the gem through.
- A C++ toolchain and CMake/Ninja matching the engine's requirements.

Provide these as repository (or organization) variables, or export them in the
runner's environment:

| Variable | Meaning |
| --- | --- |
| `O3DE_ENGINE_PATH` | Path to the O3DE engine/SDK. |
| `AICOMPANION_PROJECT` | Path to the host project to build through. |

## What it does

The job runs `scripts/ci_build_test.sh`, which registers and enables the gem in
the host project, configures with Ninja Multi-Config, builds
`AiCompanion`/`AiCompanion.Editor`/`AiCompanion.Tests`, and runs the tests
through `AzTestRunner`.

You can run the same script locally:

```bash
O3DE_ENGINE_PATH=/path/to/o3de \
AICOMPANION_PROJECT=/path/to/project \
bash scripts/ci_build_test.sh
```

The default test filter excludes `AgentServerProtocolTest` while issue #2 (a
Base64 decode memory leak) is open; add it back once that is fixed.
