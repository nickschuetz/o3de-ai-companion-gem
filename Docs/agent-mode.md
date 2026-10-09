# Agent Mode

Agent mode tells the AI Companion gem that the editor is being driven by an
AI agent (or any unattended automation), and that interactive UI meant for
a human operator should be suppressed.

The feature has two independent layers. You can use either in isolation or
both together.

## 1. Persistent editor preferences

The O3DE Editor keeps two per-user preferences that control whether it
shows the "Welcome to O3DE" dialog and whether it auto-loads the last
level. By default both are tuned for first-time human users: the welcome
dialog is shown and the level is not auto-loaded.

`configure_editor_prefs_for_agent()` flips them so the editor goes
straight into the last-open level on every launch.

```python
from ai_companion.api import configure_editor_prefs_for_agent
print(configure_editor_prefs_for_agent(enabled=True))   # apply
print(configure_editor_prefs_for_agent(enabled=False))  # restore
```

Behind the scenes this edits `~/.config/O3DE/O3DE Editor.conf` (Linux
path; equivalent location on macOS and Windows). A `.bak` is written on
first edit. The function refuses to run while any editor process is
active because the editor overwrites this file on shutdown.

This is a **per-user, machine-wide** change. It affects every O3DE
project on the machine, not just the one with the AiCompanion gem.

## 2. Runtime dialog suppression

The runtime layer asks the gem's C++ editor system component to install a
QApplication event filter (`Code/Source/AgentMode/AgentModeFilter.cpp`) that
keeps modal dialogs from blocking an unattended editor. Its policy, by window
title: "Welcome to O3DE" is closed; "Unsaved files detected", "Error Log" and
"Startup Errors" are logged and passed through untouched; and any other
`QMessageBox` is rejected (its title and text are written to the editor log
first), because a message box the editor raises during an agent request, such
as the "Entity Creation Error" box from the prefab integration layer, would
otherwise block the main thread until a human clicks it and time out every
AgentServer request from then on.

The toggle is exposed in Python:

```python
from ai_companion.api import set_agent_mode, get_agent_mode_status
print(set_agent_mode(enabled=True, suppress_dialogs=True))
print(get_agent_mode_status())
```

State is persisted to a JSON sidecar at
`$XDG_STATE_HOME/o3de-ai-companion/agent_mode.json` (default
`~/.local/state/o3de-ai-companion/agent_mode.json`). Both Python and the
gem's C++ side read this file; Python writes it. The choice of a plain
file over the settings registry is intentional: the Python binding for
the registry is shaped differently across O3DE builds, while a JSON file
is stable and easy to inspect.

Example input file:

```json
{
  "enabled": true,
  "suppress_dialogs": true,
  "updated_at": 1779218374,
  "version": 1
}
```

The C++ editor system component polls this file once per second on
`OnSystemTick`. When the file flips to enabled (with `suppress_dialogs`
true), it installs a QObject event filter on `qApp` that acts on
`QEvent::Show` by window title: it closes the "Welcome to O3DE" dialog,
leaves "Unsaved files detected", "Error Log" and "Startup Errors" alone
after logging them (their dismissal policy waits for real-world traces of
when they fire), and rejects every other `QMessageBox` after logging its
title and text. When the file flips back, the filter is removed.

### Observed-state file (introspection)

Every poll the C++ side writes a companion file at
`~/.local/state/o3de-ai-companion/agent_mode_observed.json` describing
what state it has actually applied:

```json
{
  "enabled": true,
  "suppress_dialogs": true,
  "filter_installed": true,
  "source_updated_at": 1779220425,
  "observed_at": 1779220425,
  "version": 1
}
```

- `enabled` and `suppress_dialogs` mirror the input file fields the C++
  side has accepted.
- `filter_installed` reflects whether the QApplication event filter is
  actually live on `qApp` (true) or not (false).
- `source_updated_at` echoes `updated_at` from the input file at read
  time, so you can tell whether the C++ has caught up to the latest
  Python write.
- `observed_at` is when the C++ side last polled.

`get_agent_mode_status()` exposes all three layers (`runtime`, the input
file; `observed`, this file; `persistent`, the editor preferences) in a
single payload. The most reliable way to confirm a toggle has taken effect
is to check that `observed.source_updated_at` equals `runtime.updated_at`
and `observed.filter_installed` is true.

The post-init editor log does not capture `AZ_Printf` output, so the
observed-state file is the canonical way for tools and tests to verify
the runtime layer is doing its job; do not rely on log scraping.

## Status snapshot

```python
from ai_companion.api import get_agent_mode_status
print(get_agent_mode_status())
```

Returns all three layers in one payload:

```json
{
  "status": "ok",
  "data": {
    "runtime": {
      "enabled": true,
      "suppress_dialogs": true,
      "updated_at": 1779218374,
      "state_path": "/home/you/.local/state/o3de-ai-companion/agent_mode.json"
    },
    "observed": {
      "available": true,
      "enabled": true,
      "suppress_dialogs": true,
      "filter_installed": true,
      "source_updated_at": 1779218374,
      "observed_at": 1779218375,
      "state_path": "/home/you/.local/state/o3de-ai-companion/agent_mode_observed.json"
    },
    "persistent": {
      "available": true,
      "config_path": "/home/you/.config/O3DE/O3DE Editor.conf",
      "values": {
        "LoadLastLevelAtStartup": "1",
        "ShowWelcomeScreenAtStartup": "0"
      }
    }
  }
}
```

`observed` is `{"available": false, "reason": "..."}` until the editor has
polled the input file once (or when it is not running), so a caller can tell
"not yet applied" from "applied".
