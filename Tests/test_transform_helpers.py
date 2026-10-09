# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Unit tests for ai_companion.utils.transform_helpers.set_entity_scale.

A non-uniform scale must go through the gem's C++ ``SetScale`` event, which
adds the Non-uniform Scale component the way the editor does; the older
``EditorComponentAPIBus`` attempt and the largest-axis fallback are only for
gem builds without the event.
"""

import os
import sys
import types
import unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "Editor", "Scripts"))

from ai_companion.utils.transform_helpers import set_entity_scale  # noqa: E402


class _Outcome:
    def __init__(self, ok=True, value=None):
        self._ok = ok
        self._value = value

    def IsSuccess(self):  # noqa: N802
        return self._ok

    def GetValue(self):  # noqa: N802
        return self._value


def _stub_azlmbr(calls, with_gem_bus=True, gem_ok=True, component_found=False):
    """Stub the azlmbr modules set_entity_scale touches.

    Every bus call lands in ``calls`` as ``(bus, event, args)``. With
    ``with_gem_bus`` the editor module carries ``AiCompanionEditorRequestBus``
    whose ``SetScale`` answers ``gem_ok``; without it, attribute access raises
    as it does on a gem build that lacks the event. ``component_found`` makes
    the ``EditorComponentAPIBus`` fallback succeed.
    """

    def transform_bus(call_type, event, *args):
        calls.append(("TransformBus", event, args))
        return None

    def gem_bus(call_type, event, *args):
        calls.append(("AiCompanionEditorRequestBus", event, args))
        return _Outcome(gem_ok)

    def component_bus(call_type, event, *args):
        calls.append(("EditorComponentAPIBus", event, args))
        if event == "FindComponentTypeIdsByEntityType":
            return ["tid"] if component_found else []
        if event == "AddComponentOfType":
            return _Outcome(True, "pair") if component_found else _Outcome(False)
        if event == "SetComponentProperty":
            return _Outcome(True)
        raise AssertionError(f"unexpected EditorComponentAPIBus event {event!r}")

    class _EntityType:
        Game = "Game"

    bus = types.ModuleType("azlmbr.bus")
    bus.Broadcast = object()
    bus.Event = object()
    editor = types.ModuleType("azlmbr.editor")
    editor.EditorComponentAPIBus = component_bus
    if with_gem_bus:
        editor.AiCompanionEditorRequestBus = gem_bus
    entity = types.ModuleType("azlmbr.entity")
    entity.EntityType = _EntityType
    components = types.ModuleType("azlmbr.components")
    components.TransformBus = transform_bus
    math = types.ModuleType("azlmbr.math")
    math.Vector3 = lambda x, y, z: ("Vector3", x, y, z)
    root = types.ModuleType("azlmbr")
    root.__path__ = []
    modules = {
        "azlmbr": root,
        "azlmbr.bus": bus,
        "azlmbr.editor": editor,
        "azlmbr.entity": entity,
        "azlmbr.components": components,
        "azlmbr.math": math,
    }
    for name, mod in modules.items():
        if "." in name:
            setattr(root, name.split(".", 1)[1], mod)
    return modules


class TestSetEntityScale(unittest.TestCase):
    def test_number_sets_the_uniform_scale_only(self):
        calls = []
        with mock.patch.dict(sys.modules, _stub_azlmbr(calls)):
            set_entity_scale("eid", 2)
        self.assertEqual(calls, [("TransformBus", "SetLocalUniformScale", ("eid", 2.0))])

    def test_equal_elements_are_a_uniform_scale(self):
        calls = []
        with mock.patch.dict(sys.modules, _stub_azlmbr(calls)):
            set_entity_scale("eid", [3, 3, 3])
        self.assertEqual(calls, [("TransformBus", "SetLocalUniformScale", ("eid", 3.0))])

    def test_non_uniform_goes_through_the_gem_event(self):
        calls = []
        with mock.patch.dict(sys.modules, _stub_azlmbr(calls)):
            set_entity_scale("eid", [50, 50, 1])
        self.assertEqual(
            calls,
            [("AiCompanionEditorRequestBus", "SetScale", ("eid", ("Vector3", 50.0, 50.0, 1.0)))],
        )

    def test_without_the_gem_event_the_component_api_fallback_runs(self):
        calls = []
        with mock.patch.dict(sys.modules, _stub_azlmbr(calls, with_gem_bus=False, component_found=True)):
            set_entity_scale("eid", [50, 50, 1])
        buses = [bus for bus, _, _ in calls]
        self.assertNotIn("AiCompanionEditorRequestBus", buses)
        self.assertIn(("EditorComponentAPIBus", "AddComponentOfType", ("eid", "tid")), calls)
        self.assertNotIn(("TransformBus", "SetLocalUniformScale", ("eid", 50.0)), calls)

    def test_when_everything_fails_the_largest_axis_is_applied(self):
        calls = []
        with mock.patch.dict(sys.modules, _stub_azlmbr(calls, gem_ok=False, component_found=False)):
            set_entity_scale("eid", [50, 20, 1])
        self.assertEqual(calls[0][:2], ("AiCompanionEditorRequestBus", "SetScale"))
        self.assertEqual(calls[-1], ("TransformBus", "SetLocalUniformScale", ("eid", 50.0)))


if __name__ == "__main__":
    unittest.main()
