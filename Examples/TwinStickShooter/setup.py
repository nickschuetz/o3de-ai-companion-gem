# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Twin-Stick Shooter Example, full game setup (all-in-one).

This is the quick-demo entry point. It calls every step function from
`steps.py` in order, in a single editor Python invocation.

    run_editor_python(open("path/to/setup.py").read())

Known limitation: on builds where `EditorEntityAPIBus.SetName` races with
the prefab system finalizing newly-created entities, the first batch of
entities created in `build_arena()` (Ground, walls, KeyLight, FillLight)
may end up with default `EntityN` names in the Outliner. The script logs
report the intended names correctly, only the in-editor names are lost.

To avoid the race entirely, drive setup as multiple `run_editor_python`
calls. See `run_batched.py` for the recommended invocation pattern.
"""

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import steps


def setup():
    """Build the complete twin-stick shooter example."""
    print("=== Twin-Stick Shooter Setup ===")
    print()

    print("[0/6] Preparing level...")
    print(steps.ensure_level_open())
    print()

    print("[1/6] Creating arena...")
    print(steps.build_arena(size=30, wall_height=3))
    print()

    print("[2/6] Creating player...")
    print(steps.add_player(position=(0, 0, 1), health=100))
    print()

    print("[3/6] Creating camera...")
    print(steps.add_camera(offset=(0, 0, 25)))
    print()

    print("[4/6] Creating enemies...")
    for e in steps.add_enemies():
        print(e)
    print()

    print("[5/6] Placing pickups...")
    for p in steps.add_pickups():
        print(p)
    print()

    print("[6/6] Verifying scene...")
    print(steps.verify_scene())
    print()

    print("=== Twin-Stick Shooter Ready! ===")
    print("Enter game mode to play.")


# Auto-run on load so both supported entry points work:
#   1. run_editor_python(open(...).read()), which execs the body
#   2. import setup, from the editor Python console
setup()
