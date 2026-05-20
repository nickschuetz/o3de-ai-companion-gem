# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Drive the TwinStickShooter example via discrete o3de-mcp requests.

This is the recommended invocation pattern for the example. Each step is
its own `run_editor_python` call, so the editor's main thread drains and
the prefab system finalizes newly-created entities between batches. That
sidesteps the `SetName` race that affects the single-call setup.py path.

Usage (from outside the editor, with the editor running):

    python Examples/TwinStickShooter/run_batched.py

The script imports o3de-mcp's transport layer for the AgentServer protocol.
Make sure o3de-mcp is installed and that the editor is running with the
AiCompanion and EditorPythonBindings gems enabled.

You can also use this file as a reference for building your own driver:
each entry in STEPS is a small Python snippet that gets sent to the editor
through one AgentServer request.
"""

import asyncio
import os
import sys

# Pull o3de-mcp's transport. We assume the user has it on the Python path or
# installed in editable mode in a sibling repo.
try:
    from o3de_mcp.tools.editor import _pool  # type: ignore
except ImportError as exc:
    sys.stderr.write(
        "Could not import o3de_mcp. Install it with `pip install -e .` from "
        "the o3de-mcp checkout, or add its src/ to PYTHONPATH.\n"
        f"  Reason: {exc}\n"
    )
    raise


GEM_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


PRELUDE = (
    "import sys, importlib\n"
    f"_scripts = r'{GEM_ROOT}/Editor/Scripts'\n"
    f"_example = r'{os.path.dirname(os.path.abspath(__file__))}'\n"
    "for _p in (_scripts, _example):\n"
    "    if _p not in sys.path:\n"
    "        sys.path.insert(0, _p)\n"
    "# Drop cached ai_companion modules so edits on disk take effect.\n"
    "_drop = [m for m in list(sys.modules) if m == 'ai_companion' or m.startswith('ai_companion.') or m == 'steps']\n"
    "for _m in _drop:\n"
    "    del sys.modules[_m]\n"
)


STEPS = [
    ("open_level", PRELUDE + "import steps; print(steps.ensure_level_open())\n"),
    ("arena", PRELUDE + "import steps; print(steps.build_arena(size=30, wall_height=3))\n"),
    ("player", PRELUDE + "import steps; print(steps.add_player(position=(0, 0, 1), health=100))\n"),
    ("camera", PRELUDE + "import steps; print(steps.add_camera(offset=(0, 0, 25)))\n"),
    (
        "enemies",
        PRELUDE
        + "import steps\n"
        + "for r in steps.add_enemies():\n"
        + "    print(r)\n",
    ),
    (
        "pickups",
        PRELUDE
        + "import steps\n"
        + "for r in steps.add_pickups():\n"
        + "    print(r)\n",
    ),
    ("verify", PRELUDE + "import steps; print(steps.verify_scene())\n"),
]


async def main() -> int:
    for step_name, script in STEPS:
        print(f"--- step: {step_name} ---", flush=True)
        response = await _pool.send_script(script, timeout=60.0)
        # Truncate huge responses for readability.
        out = response if len(response) < 2000 else response[:2000] + "...<truncated>"
        print(out)
        if '"status": "error"' in response or "Could not connect" in response or "timed out" in response.lower():
            print(f"step {step_name} reported error/timeout", flush=True)
            return 1
        await asyncio.sleep(0.5)
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
