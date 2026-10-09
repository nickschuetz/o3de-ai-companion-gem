# Self-hosted runner for build-test CI

The `lint` workflow (SPDX headers, hygiene, Python unit tests) runs on standard
GitHub-hosted runners and is the always-on gate.

The `build-test` workflow compiles the gem's C++ and runs its C++ unit tests
(including the `BusSchema` introspection tests). That needs a full O3DE SDK (26.05 or 26.10)5
SDK, a host project, and the 3rdParty packages, none of which exist on
GitHub-hosted runners. It therefore runs only on a **self-hosted** runner and is
**opt-in**.

## When it runs

- On `workflow_dispatch` (manual trigger), or
- On a pull request that a maintainer has labelled `ci:build` (build and C++
  unit tests) or `ci:live` (the same, followed by the live editor suite).

Ordinary contributors are never blocked by missing hardware: without the label
and without the runner, the job simply does not run.

## Runner requirements

Register a self-hosted runner with the labels `self-hosted` and `o3de`, on a
machine that has:

- An O3DE SDK (26.05 and 26.10 are what the gem is built and tested against) with `scripts/o3de.sh` and `bin/Linux/profile/Default/AzTestRunner`.
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

The default test filter runs the full suite (`*`).

## The live editor suite

`Tests/live/` is the only part of the test surface that touches a running
editor. The unit suites stub `azlmbr` and the C++ tests run without an
application context, so neither can catch a reflection attribute that keeps
a bus out of Python, a template that no longer produces a real entity, or an
engine call that crashes instead of failing. The live suite checks those: the
`ai_companion` package imports inside the editor and reports the versions the
checkout declares, every advertised API function exists, the native request
types (`get_scene_snapshot`, `get_entity_tree`, `get_entity`, `validate_scene`,
`get_bus_schema`) answer and agree with each other, the templates create
entities that `rollback_last_batch` removes again, and `spawn_prefab` refuses
a missing prefab with the editor still alive.

It is opt-in (`O3DE_LIVE_EDITOR_TEST=1`) and talks to the AgentServer with a
standard-library client (`Tests/live/agent_client.py`), so it needs no extra
Python packages. `scripts/ci_live_test.sh` runs it end to end:

```bash
O3DE_ENGINE_PATH=/path/to/o3de \
AICOMPANION_PROJECT=/path/to/project \
bash scripts/ci_live_test.sh
```

The script starts AssetProcessor and waits for its idle line in the project's
`user/log/AP_GUI.log`, starts a cursor-less Xvfb, launches the Editor on it
with the AgentServer on port 4610 (so a developer's own editor on 4600 is left
alone), opens `LIVE_LEVEL` (default `DefaultLevel`), runs `pytest Tests/live`,
and stops everything it started. `LIVE_KEEP=1` leaves the processes up for
debugging. Linux only; the runner needs Xvfb and a GPU with a working Vulkan
driver in addition to the build requirements above.

To run the tests against an editor you already have open, skip the script:

```bash
O3DE_LIVE_EDITOR_TEST=1 O3DE_EDITOR_PORT=4600 python -m pytest Tests/live -v
```

Every mutation the suite makes is undone through the gem's own rollback, so
the open level is left as it was found. Nothing is saved.
