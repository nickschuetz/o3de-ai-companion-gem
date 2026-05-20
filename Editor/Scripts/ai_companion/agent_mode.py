# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Agent mode: suppresses human-only interactive UI for AI-driven sessions.

Two layers of effects, each independently usable.

1. Runtime, in-process. The Python toggle writes a small JSON state file
   at ``$XDG_STATE_HOME/o3de-ai-companion/agent_mode.json`` (default
   ``~/.local/state/o3de-ai-companion/agent_mode.json``). The C++ editor
   system component watches that file on tick and, when ``enabled`` is
   true, installs a QApplication event filter that auto-dismisses common
   modal dialogs (welcome screen, level-unsaved prompt during agent-driven
   level switches, missing-component-UUID error logs). The Python toggle
   is the agent-facing surface.

2. Persistent editor preferences. ``configure_editor_prefs(enabled=True)``
   updates ``~/.config/O3DE/O3DE Editor.conf`` (a Qt INI file) to set
   ``LoadLastLevelAtStartup=1`` and ``ShowWelcomeScreenAtStartup=0``, so
   future editor launches skip the welcome dialog and auto-load the last
   level. Only safe to call while the editor is not running; we abort
   with a clear error otherwise.

A JSON sidecar (rather than the settings registry) is used for runtime
state because the settings-registry Python binding shape varies across
O3DE builds; a flat file is stable, easy to inspect, and trivial to read
from both Python and C++.
"""

from __future__ import annotations

import json
import os
import re
import time
from pathlib import Path
from typing import Any, Dict

from .utils.json_output import success, error

STATE_VERSION = 1


# ---------------------------------------------------------------------------
# JSON state file (read by both Python and the gem's C++ component)
# ---------------------------------------------------------------------------


def _state_dir() -> Path:
    base = os.environ.get("XDG_STATE_HOME") or os.path.join(
        os.path.expanduser("~"), ".local", "state"
    )
    return Path(base) / "o3de-ai-companion"


def _state_path() -> Path:
    return _state_dir() / "agent_mode.json"


def _observed_state_path() -> Path:
    return _state_dir() / "agent_mode_observed.json"


def _read_observed_state() -> Dict[str, Any]:
    """Load whatever the C++ side last wrote.

    Returns a dict with keys ``enabled``, ``suppress_dialogs``,
    ``filter_installed``, ``source_updated_at``, ``observed_at`` if available,
    or an ``available: False`` marker if the file does not exist or is
    unreadable. The file is written by the gem's editor system component
    after every poll, and is the canonical signal that the C++ side has
    caught up to a Python state change.
    """
    path = _observed_state_path()
    if not path.is_file():
        return {"available": False, "reason": "observed-state file not present"}
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as exc:
        return {"available": False, "reason": str(exc)}
    return {
        "available": True,
        "enabled": bool(raw.get("enabled", False)),
        "suppress_dialogs": bool(raw.get("suppress_dialogs", False)),
        "filter_installed": bool(raw.get("filter_installed", False)),
        "source_updated_at": raw.get("source_updated_at"),
        "observed_at": raw.get("observed_at"),
        "state_path": str(path),
    }


def _default_state() -> Dict[str, Any]:
    return {
        "version": STATE_VERSION,
        "enabled": False,
        "suppress_dialogs": False,
        "updated_at": None,
    }


def _read_state() -> Dict[str, Any]:
    path = _state_path()
    if not path.is_file():
        return _default_state()
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return _default_state()
    out = _default_state()
    if isinstance(raw, dict):
        for key in out:
            if key in raw:
                out[key] = raw[key]
    return out


def _write_state(state: Dict[str, Any]) -> None:
    path = _state_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    state = dict(state)
    state["updated_at"] = int(time.time())
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(state, indent=2, sort_keys=True), encoding="utf-8")
    tmp.replace(path)


# ---------------------------------------------------------------------------
# Runtime toggle
# ---------------------------------------------------------------------------


def set_agent_mode(enabled: bool = True, suppress_dialogs: bool = True) -> str:
    """Enable or disable agent mode at runtime.

    Persists state to the JSON sidecar so the C++ editor system component
    (and any other reader) sees the latest value. Returns a JSON status
    string.

    Args:
        enabled: master switch.
        suppress_dialogs: subordinate switch for the dialog auto-dismissal
            event filter. Only meaningful when ``enabled`` is True.
    """
    state = _read_state()
    state["enabled"] = bool(enabled)
    state["suppress_dialogs"] = bool(suppress_dialogs)
    try:
        _write_state(state)
    except OSError as exc:
        return error(f"Failed to write agent-mode state file: {exc}")

    return success({
        "enabled": state["enabled"],
        "suppress_dialogs": state["suppress_dialogs"],
        "state_path": str(_state_path()),
        "note": (
            "Runtime enforcement is performed by the AiCompanion C++ editor "
            "system component; until that ships, this toggle is recorded but "
            "has no in-editor side effect. The persistent prefs route works "
            "today; see Docs/agent-mode.md."
        ),
    })


def get_agent_mode() -> str:
    """Return the current agent-mode runtime state."""
    state = _read_state()
    return success({
        "enabled": state["enabled"],
        "suppress_dialogs": state["suppress_dialogs"],
        "updated_at": state.get("updated_at"),
        "state_path": str(_state_path()),
    })


# ---------------------------------------------------------------------------
# Persistent editor preferences (Qt INI file)
# ---------------------------------------------------------------------------


def _editor_conf_path() -> Path:
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(
        os.path.expanduser("~"), ".config"
    )
    return Path(base) / "O3DE" / "O3DE Editor.conf"


def _editor_is_running() -> bool:
    """Best-effort check for a live editor process.

    Looks for any process whose command line contains the Editor binary
    path. Returns False on platforms where /proc is unavailable.
    """
    proc_root = Path("/proc")
    if not proc_root.is_dir():
        return False
    needle = re.compile(r"/bin/.*?/Editor( |$)")
    for pid_dir in proc_root.iterdir():
        if not pid_dir.name.isdigit():
            continue
        cmdline_file = pid_dir / "cmdline"
        try:
            cmdline = cmdline_file.read_bytes().decode("utf-8", errors="ignore")
        except (FileNotFoundError, PermissionError):
            continue
        if needle.search(cmdline):
            return True
    return False


_KEYS = {
    "LoadLastLevelAtStartup": ("1", "0"),
    "ShowWelcomeScreenAtStartup": ("0", "1"),
}


def configure_editor_prefs(enabled: bool = True) -> str:
    """Update the editor's per-user preferences for an agent-driven workflow.

    When ``enabled`` is True, sets:
      - LoadLastLevelAtStartup = 1
      - ShowWelcomeScreenAtStartup = 0

    When ``enabled`` is False, restores defaults:
      - LoadLastLevelAtStartup = 0
      - ShowWelcomeScreenAtStartup = 1

    The file is edited in place; a ``.bak`` copy is created on first edit.
    Aborts if a competing editor process is running.

    Returns a JSON status string.
    """
    conf = _editor_conf_path()
    if not conf.is_file():
        return error(
            f"Editor preferences file not found at {conf}. Launch the "
            "editor at least once so it can create the file, then retry."
        )

    if _editor_is_running():
        return error(
            "An O3DE Editor process is currently running. Close all editors "
            "before changing persistent preferences, otherwise the editor "
            "will overwrite this file on shutdown."
        )

    backup = conf.with_suffix(conf.suffix + ".bak")
    if not backup.exists():
        backup.write_bytes(conf.read_bytes())

    text = conf.read_text(encoding="utf-8")
    changed = []

    for key, (on_value, off_value) in _KEYS.items():
        target = on_value if enabled else off_value
        pattern = re.compile(rf"^{re.escape(key)}=.*$", flags=re.MULTILINE)
        if pattern.search(text):
            new_text, n = pattern.subn(f"{key}={target}", text)
            if n and new_text != text:
                text = new_text
                changed.append({"key": key, "value": target})
        else:
            text = text.rstrip() + f"\n{key}={target}\n"
            changed.append({"key": key, "value": target, "added": True})

    conf.write_text(text, encoding="utf-8")

    return success({
        "applied": True,
        "enabled": bool(enabled),
        "config_path": str(conf),
        "backup_path": str(backup),
        "changes": changed,
    })


def get_status() -> str:
    """Snapshot of both layers, suitable for diagnostics."""
    runtime = _read_state()
    runtime_out: Dict[str, Any] = {
        "enabled": runtime["enabled"],
        "suppress_dialogs": runtime["suppress_dialogs"],
        "updated_at": runtime.get("updated_at"),
        "state_path": str(_state_path()),
    }

    conf = _editor_conf_path()
    if conf.is_file():
        text = conf.read_text(encoding="utf-8", errors="replace")
        values = {}
        for key in _KEYS:
            match = re.search(rf"^{re.escape(key)}=(.*)$", text, flags=re.MULTILINE)
            values[key] = match.group(1) if match else None
        persistent_out = {
            "available": True,
            "config_path": str(conf),
            "values": values,
        }
    else:
        persistent_out = {
            "available": False,
            "reason": f"Editor preferences file not found at {conf}.",
        }

    observed_out = _read_observed_state()

    return success({
        "runtime": runtime_out,
        "observed": observed_out,
        "persistent": persistent_out,
    })
