# Self-hosted runner for build-test CI

The `lint` workflow (SPDX headers, hygiene, `clang-format` on the C++ with the
pinned version, Python unit tests) runs on standard GitHub-hosted runners and is
the always-on gate. The Python unit tests run twice
there, on `ubuntu-latest` (the required check) and on `windows-latest`
(advisory), so a path or line-ending assumption that only holds on Linux is
caught before it reaches a Windows user of the gem.

The `build-test` workflow compiles the gem's C++ and runs its C++ unit tests
(including the `BusSchema` introspection tests). That needs a full O3DE SDK (26.05 or 26.10),
a host project, and the 3rdParty packages, none of which exist on
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
engine call that crashes instead of failing. The live suite checks those, one class
per concern: `TestProtocol` (the versions the checkout declares, the error
codes), `TestPythonPackage` (the package imports and every advertised function
exists), `TestNativeRequestTypes` (`get_scene_snapshot`, `get_entity_tree`,
`get_entity`, `validate_scene`, `get_bus_schema` answer, agree with each other
and use decimal-string ids), `TestNativeMutations` (`create_entity`,
`set_transform`, `delete_entity` round-trip, undo, the validation refusals, the
level-root guard and the protected-entity guard), `TestTemplatesAndRollback`
(the templates create entities that `rollback_last_batch` removes again, a
failing call answers a rolled-back error), `TestMultiEntityPersistence`
(multi-entity calls keep every entity), `TestPrefabGuard` (`spawn_prefab`
refuses a missing prefab with the editor still alive) and `TestAnimGraphs`
(the anim graph reads against a fixture graph, and the authoring and wiring
types with their refusals).

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

`LIVE_SECURE=1` runs the secure-mode variant: the script starts the Editor with
`AI_COMPANION_SECURE_MODE=1`, so the AgentServer refuses `execute_python` and
serves the native types only: the reads, the validated mutations and the anim
graph reads and writes (see `Docs/safety-model.md`, "Secure Mode"). Because the
Python-ready probe and the usual level open both go through `execute_python`,
the script opens `LIVE_LEVEL` through the editor's own `--runpython` startup
script instead, waits for `get_api_version` to report `"secure_mode": true` and
for `get_entity_tree` to answer with a root (which shows the editor's main loop
is up and the level is open), and runs `pytest Tests/live -k Secure`. That
selects `TestSecureMode`, which asserts that `execute_python` is refused with
the code `secure_mode`; that `ping`, `get_api_version`, `get_scene_snapshot`,
`get_entity_tree`, `validate_scene`, `get_bus_schema` and `list_anim_graphs`
answer with JSON that carries no error; that `create_entity`, `set_transform`
and `delete_entity` create, move and delete an entity with no Python; and that
`create_anim_graph`, `add_anim_graph_node`, `add_anim_graph_parameter`,
`get_anim_graph` and `remove_anim_graph` author and remove a graph the same
way. Every other class in the live file skips itself against a secure-mode
editor, and `TestSecureMode` skips against a normal one, so the two variants
can be run back to back from the same checkout:

```bash
O3DE_ENGINE_PATH=/path/to/o3de \
AICOMPANION_PROJECT=/path/to/project \
LIVE_SECURE=1 bash scripts/ci_live_test.sh
```

## The launcher gameplay check

`scripts/ci_launcher_test.sh` is the only check that runs the gem's Lua in a
GameLauncher, where PhysX contact callbacks and the prefab bus are not
available to scripts (see `Docs/lua-scripts.md`). It brings up AssetProcessor,
Xvfb and the Editor, creates a level and builds a small arena through the gem
API (`Tests/live/build_launcher_arena.py`: a twin-stick player, a health pickup
inside its pickup radius, a chasing enemy, and a contact-damage hazard on the
enemy's start position), saves it, waits for AssetProcessor to produce the
spawnable, stops the Editor, runs the project's GameLauncher with `+LoadLevel`,
waits for the level-load line and then for the scripts' `[AiCompanion] ...`
markers in `user/log/Game.log`, and runs `Tests/live/test_launcher.py` on that
log. It needs no input: the player registers itself, the pickup collects by
distance, the enemy finds and attacks the player, and the hazard damages the
enemy. Each test asserts one of those happened at runtime, plus one that no
Lua runtime error appeared.

```bash
O3DE_ENGINE_PATH=/path/to/o3de \
AICOMPANION_PROJECT=/path/to/project \
bash scripts/ci_launcher_test.sh
```

The project's `<Project>.GameLauncher` target is built if it is missing. The
generated level (`Levels/AiCompanionLauncherArena`) is deleted afterwards;
`LAUNCHER_KEEP=1` keeps it and the processes. The workflow runs this as the
`launcher-test` job after the live suite, on the same `ci:live` label.

To run the tests against an editor you already have open, skip the script:

```bash
O3DE_LIVE_EDITOR_TEST=1 O3DE_EDITOR_PORT=4600 python -m pytest Tests/live -v
```

Every mutation the suite makes is undone through the gem's own rollback, so
the open level is left as it was found. Nothing is saved.
