# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Unit tests for ai_companion.agent_mode."""

import json
import os
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "Editor", "Scripts"))

from ai_companion import agent_mode


SAMPLE_CONF = textwrap.dedent("""\
    [General]
    UseDoubleClickedRowSavedView=true
    [Welcome%20Screen]
    LoadLastLevelAtStartup=0
    ShowWelcomeScreenAtStartup=1
    UndoLevels=50
""")


class TestRuntimeStateRoundTrip(unittest.TestCase):
    """The JSON sidecar must round-trip values cleanly."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.state_path = Path(self.tmp.name) / "agent_mode.json"
        self._patch = mock.patch.object(
            agent_mode, "_state_path", return_value=self.state_path
        )
        self._patch.start()
        self.addCleanup(self._patch.stop)

    def test_default_state_when_file_missing(self):
        result = json.loads(agent_mode.get_agent_mode())
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["data"]["enabled"], False)
        self.assertEqual(result["data"]["suppress_dialogs"], False)

    def test_set_then_get(self):
        json.loads(agent_mode.set_agent_mode(enabled=True, suppress_dialogs=True))
        result = json.loads(agent_mode.get_agent_mode())
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["data"]["enabled"], True)
        self.assertEqual(result["data"]["suppress_dialogs"], True)
        self.assertIsNotNone(result["data"]["updated_at"])

    def test_disable_persists(self):
        agent_mode.set_agent_mode(enabled=True, suppress_dialogs=True)
        agent_mode.set_agent_mode(enabled=False, suppress_dialogs=False)
        result = json.loads(agent_mode.get_agent_mode())
        self.assertEqual(result["data"]["enabled"], False)
        self.assertEqual(result["data"]["suppress_dialogs"], False)

    def test_corrupted_state_reads_as_default(self):
        self.state_path.parent.mkdir(parents=True, exist_ok=True)
        self.state_path.write_text("{not valid json", encoding="utf-8")
        result = json.loads(agent_mode.get_agent_mode())
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["data"]["enabled"], False)


class TestEditorPrefsConfigure(unittest.TestCase):
    """Persistent preference editing must be safe and idempotent."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.conf_path = Path(self.tmp.name) / "O3DE" / "O3DE Editor.conf"
        self.conf_path.parent.mkdir(parents=True)
        self.conf_path.write_text(SAMPLE_CONF, encoding="utf-8")

    def _patches(self):
        return [
            mock.patch.object(agent_mode, "_editor_conf_path",
                              return_value=self.conf_path),
            mock.patch.object(agent_mode, "_editor_is_running",
                              return_value=False),
        ]

    def _enter_patches(self):
        ps = self._patches()
        for p in ps:
            p.start()
            self.addCleanup(p.stop)

    def test_enable_writes_expected_values(self):
        self._enter_patches()
        result = json.loads(agent_mode.configure_editor_prefs(enabled=True))
        self.assertEqual(result["status"], "ok")
        text = self.conf_path.read_text(encoding="utf-8")
        self.assertIn("LoadLastLevelAtStartup=1", text)
        self.assertIn("ShowWelcomeScreenAtStartup=0", text)
        backup = self.conf_path.with_suffix(self.conf_path.suffix + ".bak")
        self.assertTrue(backup.exists())
        self.assertEqual(backup.read_text(encoding="utf-8"), SAMPLE_CONF)

    def test_disable_restores_defaults(self):
        self._enter_patches()
        agent_mode.configure_editor_prefs(enabled=True)
        agent_mode.configure_editor_prefs(enabled=False)
        text = self.conf_path.read_text(encoding="utf-8")
        self.assertIn("LoadLastLevelAtStartup=0", text)
        self.assertIn("ShowWelcomeScreenAtStartup=1", text)

    def test_aborts_when_editor_running(self):
        with mock.patch.object(agent_mode, "_editor_conf_path",
                               return_value=self.conf_path), \
             mock.patch.object(agent_mode, "_editor_is_running",
                               return_value=True):
            result = json.loads(agent_mode.configure_editor_prefs(enabled=True))
            self.assertEqual(result["status"], "error")
            self.assertIn("running", result["message"].lower())

    def test_missing_conf_returns_error(self):
        missing = Path(self.tmp.name) / "nope.conf"
        with mock.patch.object(agent_mode, "_editor_conf_path",
                               return_value=missing), \
             mock.patch.object(agent_mode, "_editor_is_running",
                               return_value=False):
            result = json.loads(agent_mode.configure_editor_prefs(enabled=True))
            self.assertEqual(result["status"], "error")
            self.assertIn(str(missing), result["message"])

    def test_idempotent_repeated_enable(self):
        self._enter_patches()
        agent_mode.configure_editor_prefs(enabled=True)
        text_a = self.conf_path.read_text(encoding="utf-8")
        agent_mode.configure_editor_prefs(enabled=True)
        text_b = self.conf_path.read_text(encoding="utf-8")
        self.assertEqual(text_a, text_b)


class TestStatusSnapshot(unittest.TestCase):
    """get_status combines both layers in one payload."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.state_path = Path(self.tmp.name) / "agent_mode.json"
        self.conf_path = Path(self.tmp.name) / "O3DE Editor.conf"
        self.conf_path.write_text(SAMPLE_CONF, encoding="utf-8")
        for patch in [
            mock.patch.object(agent_mode, "_state_path", return_value=self.state_path),
            mock.patch.object(agent_mode, "_editor_conf_path",
                              return_value=self.conf_path),
        ]:
            patch.start()
            self.addCleanup(patch.stop)

    def test_includes_runtime_and_persistent_layers(self):
        agent_mode.set_agent_mode(enabled=True, suppress_dialogs=True)
        status = json.loads(agent_mode.get_status())
        self.assertEqual(status["status"], "ok")
        self.assertEqual(status["data"]["runtime"]["enabled"], True)
        self.assertEqual(status["data"]["runtime"]["suppress_dialogs"], True)
        persistent = status["data"]["persistent"]
        self.assertTrue(persistent["available"])
        self.assertEqual(persistent["values"]["LoadLastLevelAtStartup"], "0")
        self.assertEqual(persistent["values"]["ShowWelcomeScreenAtStartup"], "1")


if __name__ == "__main__":
    unittest.main()
