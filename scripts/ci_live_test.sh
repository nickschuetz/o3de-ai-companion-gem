#!/usr/bin/env bash
# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
# Run the gem's live editor suite (Tests/live) against a real O3DE Editor.
#
# Brings up AssetProcessor, a cursor-less Xvfb, and the Editor with the
# AgentServer on its own port, opens a level, runs the tests, and tears it all
# down. Needs a host project that already has the gem enabled and built
# (scripts/ci_build_test.sh does the build). Linux only: the editor is run on
# a virtual X display so the job works on a monitor-less runner.
#
# Required environment:
#   O3DE_ENGINE_PATH     Engine/SDK root (has bin/Linux/<config>/Default/Editor).
#   AICOMPANION_PROJECT  Host project with the AiCompanion gem enabled and built.
#
# Optional:
#   BUILD_CONFIG         profile (default) | debug | release.
#   LIVE_LEVEL           Level to open (default: DefaultLevel).
#   LIVE_PORT            AgentServer port (default: 4610, so a developer's own
#                        editor on 4600 is left alone).
#   LIVE_AP_PORT         AssetProcessor port (default: 45644).
#   LIVE_DISPLAY         Xvfb display (default: :99).
#   LIVE_KEEP            1 to leave everything running on exit (debugging).
#
# Exit status is non-zero if the editor never answers, the level does not
# open, or any test fails.

set -euo pipefail

GEM_PATH="${GEM_PATH:-$(cd "$(dirname "$0")/.." && pwd)}"
BUILD_CONFIG="${BUILD_CONFIG:-profile}"
LIVE_LEVEL="${LIVE_LEVEL:-DefaultLevel}"
LIVE_PORT="${LIVE_PORT:-4610}"
LIVE_AP_PORT="${LIVE_AP_PORT:-45644}"
LIVE_DISPLAY="${LIVE_DISPLAY:-:99}"
LIVE_KEEP="${LIVE_KEEP:-0}"

fail() {
    echo "ci_live_test: $1" >&2
    exit 1
}

[ -n "${O3DE_ENGINE_PATH:-}" ] || fail "O3DE_ENGINE_PATH is not set"
[ -n "${AICOMPANION_PROJECT:-}" ] || fail "AICOMPANION_PROJECT is not set"
BIN="$O3DE_ENGINE_PATH/bin/Linux/$BUILD_CONFIG/Default"
[ -x "$BIN/Editor" ] || fail "no Editor under $BIN"
[ -x "$BIN/AssetProcessor" ] || fail "no AssetProcessor under $BIN"
[ -f "$AICOMPANION_PROJECT/project.json" ] || fail "AICOMPANION_PROJECT has no project.json"
command -v Xvfb >/dev/null || fail "Xvfb is not installed"

STATE="$(mktemp -d)"
echo "== AiCompanion live test =="
echo "  engine : $O3DE_ENGINE_PATH"
echo "  project: $AICOMPANION_PROJECT"
echo "  level  : $LIVE_LEVEL   port: $LIVE_PORT   display: $LIVE_DISPLAY"
echo "  logs   : $STATE"

PIDS=()
cleanup() {
    [ "$LIVE_KEEP" = "1" ] && { echo "LIVE_KEEP=1: leaving processes running"; return 0; }
    for pid in "${PIDS[@]:-}"; do
        [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
    done
    sleep 3
    for pid in "${PIDS[@]:-}"; do
        [ -n "$pid" ] && kill -9 "$pid" 2>/dev/null || true
    done
    # AssetProcessor leaves resident AssetBuilder workers behind.
    pkill -f "[A]ssetBuilder .*project-path=\"?$AICOMPANION_PROJECT" 2>/dev/null || true
}
trap cleanup EXIT

# 1. AssetProcessor, then wait for a fresh idle line. AP never prints "idle" on
#    stdout; it appends to the project's user/log/AP_GUI.log, which persists
#    across runs (rotating at 4 MB), so only lines written after launch count.
ap_log="$AICOMPANION_PROJECT/user/log/AP_GUI.log"
before=0
[ -f "$ap_log" ] && before="$(wc -l <"$ap_log")"
"$BIN/AssetProcessor" --project-path="$AICOMPANION_PROJECT" \
    --regset="/Amazon/AzCore/Bootstrap/remote_port=$LIVE_AP_PORT" >"$STATE/ap.log" 2>&1 &
PIDS+=("$!")
idle=0
for _ in $(seq 1 600); do
    if [ -f "$ap_log" ]; then
        now="$(wc -l <"$ap_log")"
        (( now < before )) && before=0
        if tail -n +"$((before + 1))" "$ap_log" | grep -q "Asset Processor is currently idle"; then
            idle=1; break
        fi
    fi
    sleep 1
done
[ "$idle" = "1" ] && echo "== AssetProcessor idle ==" || echo "== AssetProcessor idle not seen in 600s; continuing =="

# 2. Virtual display, no cursor.
Xvfb "$LIVE_DISPLAY" -screen 0 1600x900x24 -nocursor >"$STATE/xvfb.log" 2>&1 &
PIDS+=("$!")
sleep 2

# 3. Editor with the AgentServer on LIVE_PORT. The gem reads O3DE_EDITOR_PORT.
DISPLAY="$LIVE_DISPLAY" O3DE_EDITOR_PORT="$LIVE_PORT" "$BIN/Editor" --project-path="$AICOMPANION_PROJECT" \
    --skipWelcomeScreenDialog --rhi=vulkan --rhi-device-validation=disable \
    --regset="/Amazon/AzCore/Bootstrap/remote_port=$LIVE_AP_PORT" >"$STATE/editor.log" 2>&1 &
PIDS+=("$!")

export O3DE_EDITOR_PORT="$LIVE_PORT"
export PYTHONPATH="$GEM_PATH/Tests/live${PYTHONPATH:+:$PYTHONPATH}"
answered=0
for _ in $(seq 1 300); do
    if python3 - <<'PY' 2>/dev/null; then answered=1; break; fi
from agent_client import AgentClient
import sys
sys.exit(0 if AgentClient(timeout=3).request("ping").get("status") == "ok" else 1)
PY
    sleep 1
done
[ "$answered" = "1" ] || fail "AgentServer never answered ping on port $LIVE_PORT (see $STATE/editor.log)"
echo "== AgentServer up =="

# The AgentServer answers ping as soon as the gem's system component activates,
# which is before the renderer and editor Python exist; an execute_python sent
# then fails with "Failed to retrieve script execution result". Wait until a
# trivial script actually runs.
ready=0
for _ in $(seq 1 300); do
    if python3 - <<'PY' 2>/dev/null; then ready=1; break; fi
from agent_client import AgentClient
import sys
try:
    out = AgentClient(timeout=10).run("print('python-ready')")
except Exception:
    sys.exit(1)
sys.exit(0 if "python-ready" in out else 1)
PY
    sleep 2
done
[ "$ready" = "1" ] || fail "editor Python never became ready on port $LIVE_PORT (see $STATE/editor.log)"
echo "== editor Python ready =="

# 4. Open the level; the editor starts with none when the welcome screen is skipped.
python3 - "$LIVE_LEVEL" <<'PY' || fail "could not open level $LIVE_LEVEL"
import sys
from agent_client import AgentClient
level = sys.argv[1]
out = AgentClient().run(
    "import azlmbr.legacy.general as general\n"
    f"general.open_level_no_prompt({level!r})\n"
    "print('LEVEL=' + str(general.get_current_level_name()))\n"
)
print(out.strip())
sys.exit(0 if f"LEVEL={level}" in out else 1)
PY
# Let the level's entities activate before the first snapshot.
sleep 5

# 5. Tests.
echo "== run live suite =="
cd "$GEM_PATH"
O3DE_LIVE_EDITOR_TEST=1 python3 -m pytest Tests/live -v
echo "== live suite passed =="
