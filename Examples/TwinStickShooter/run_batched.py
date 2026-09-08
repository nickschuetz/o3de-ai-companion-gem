# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""
Batched TwinStickShooter driver.

Runs the example as a sequence of separate editor requests, one per step,
so the editor's main thread drains between them. This avoids the SetName
race that affects the all-in-one script on some O3DE builds.

Usage (from outside the editor, with the editor running):

    python Examples/TwinStickShooter/run_batched.py

The steps run inside one persistent editor Python session, opened with
o3de-mcp's ``begin_session`` tool and driven with ``exec_in_session``.
The gem's ``Editor/Scripts`` folder and this example directory are put on
``sys.path`` once, the ``steps`` module is imported once, and every step
then reuses that namespace instead of rebuilding it per request.

Requires o3de-mcp 0.4.0 or later, which pulls in the ``mcp`` 2.x SDK it
needs. Install it from PyPI (``pip install o3de-mcp``), or, to run against a
checkout, ``pip install -e .`` there or add its ``src/`` to PYTHONPATH.
"""

import asyncio
import json
import os
import sys

try:
    from mcp.server import MCPServer
    from o3de_mcp.tools.editor import register_editor_tools
except ImportError as exc:
    sys.stderr.write(
        "Could not import o3de_mcp (needs the mcp 2.x SDK). Install it with "
        "`pip install o3de-mcp` (0.4.0 or later), or `pip install -e .` from an "
        "o3de-mcp checkout, or add its src/ to PYTHONPATH.\n"
        f"  Reason: {exc}\n"
    )
    raise


GEM_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXAMPLE_DIR = os.path.dirname(os.path.abspath(__file__))

# Runs once, in the session namespace. Dropping cached ai_companion modules
# lets edits on disk take effect on the next run without restarting the editor.
SETUP = (
    "import sys, importlib\n"
    f"_scripts = r'{GEM_ROOT}/Editor/Scripts'\n"
    f"_example = r'{EXAMPLE_DIR}'\n"
    "for _p in (_scripts, _example):\n"
    "    if _p not in sys.path:\n"
    "        sys.path.insert(0, _p)\n"
    "_drop = [m for m in list(sys.modules) if m == 'ai_companion' or m.startswith('ai_companion.') or m == 'steps']\n"
    "for _m in _drop:\n"
    "    del sys.modules[_m]\n"
    "import steps\n"
    "print('session ready')\n"
)


STEPS = [
    ("open_level", "print(steps.ensure_level_open())\n"),
    ("arena", "print(steps.build_arena(size=30, wall_height=3))\n"),
    ("player", "print(steps.add_player(position=(0, 0, 1), health=100))\n"),
    ("camera", "print(steps.add_camera(offset=(0, 0, 25)))\n"),
    ("enemies", "for r in steps.add_enemies():\n    print(r)\n"),
    ("pickups", "for r in steps.add_pickups():\n    print(r)\n"),
    ("verify", "print(steps.verify_scene())\n"),
]


def _failed(response: str) -> bool:
    """True when a tool response is a transport error or a session error."""
    lowered = response.lower()
    if "could not connect" in lowered or "timed out" in lowered:
        return True
    stripped = response.strip()
    if stripped.startswith("{"):
        try:
            parsed = json.loads(stripped.splitlines()[0])
        except json.JSONDecodeError:
            return False
        return parsed.get("status") == "error" or "error" in parsed
    return False


async def _call(mcp: MCPServer, tool: str, **kwargs) -> str:
    content = (await mcp.call_tool(tool, kwargs)).content
    return content[0].text if content else ""


async def main() -> int:
    # Each step is short; keep the per-request timeout well under the 600s default.
    os.environ.setdefault("O3DE_EDITOR_TIMEOUT", "120")

    mcp = MCPServer("twin-stick-runner")
    register_editor_tools(mcp)

    opened = await _call(mcp, "begin_session")
    if _failed(opened):
        print(opened)
        print("could not open an editor session", flush=True)
        return 1
    session_id = json.loads(opened.strip().splitlines()[-1])["session_id"]
    print(f"--- session {session_id} ---", flush=True)

    try:
        for step_name, script in (("setup", SETUP), *STEPS):
            print(f"--- step: {step_name} ---", flush=True)
            response = await _call(mcp, "exec_in_session", session_id=session_id, script=script)
            # Truncate huge responses for readability.
            out = response if len(response) < 2000 else response[:2000] + "...<truncated>"
            print(out)
            if _failed(response):
                print(f"step {step_name} reported error/timeout", flush=True)
                return 1
            await asyncio.sleep(0.5)
        return 0
    finally:
        await _call(mcp, "end_session", session_id=session_id)


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
