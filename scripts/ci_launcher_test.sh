#!/usr/bin/env bash
# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
# Build a small arena with the gem's Python API, save it as a level, run it in
# the project's GameLauncher, and check the gem's Lua gameplay scripts actually
# fire at runtime (Tests/live/test_launcher.py reads the launcher's log).
#
# This is the only check that runs the Lua in a launcher, where PhysX contact
# callbacks and the prefab bus are not available to scripts (see
# Docs/lua-scripts.md). It proves, with no input and no human: the shared body
# registry (player registers, pickup finds it), distance-based pickup
# collection, enemy chase and attack, and distance-based contact damage.
#
# Steps: AssetProcessor (idle wait on AP_GUI.log), Xvfb, Editor with the
# AgentServer, build and save the level through the gem API, wait for the Asset
# Processor to produce the spawnable, stop the Editor, run the GameLauncher on
# the same display with +LoadLevel, wait for the level-load line and then for
# the gem's markers in Game.log, run pytest on that log, tear everything down
# and delete the generated level.
#
# Required environment:
#   O3DE_ENGINE_PATH     Engine/SDK root (bin/Linux/<config>/Default/Editor).
#   AICOMPANION_PROJECT  Host project with the gem enabled, built, and its
#                        <Project>.GameLauncher built in BUILD_DIR.
#
# Optional:
#   BUILD_CONFIG         profile (default) | debug | release.
#   BUILD_DIR            Project build tree (default: $AICOMPANION_PROJECT/build/linux).
#   LAUNCHER_LEVEL       Level name to create (default: AiCompanionLauncherArena).
#   LAUNCHER_PORT        AgentServer port for the editor phase (default: 4611).
#   LAUNCHER_AP_PORT     AssetProcessor port (default: 45645).
#   LAUNCHER_DISPLAY     Xvfb display (default: :98).
#   LAUNCHER_WAIT        Seconds to wait for the gameplay markers (default: 90).
#   LAUNCHER_KEEP        1 to leave processes and the level in place (debugging).

set -euo pipefail

GEM_PATH="${GEM_PATH:-$(cd "$(dirname "$0")/.." && pwd)}"
BUILD_CONFIG="${BUILD_CONFIG:-profile}"
LAUNCHER_LEVEL="${LAUNCHER_LEVEL:-AiCompanionLauncherArena}"
LAUNCHER_PORT="${LAUNCHER_PORT:-4611}"
LAUNCHER_AP_PORT="${LAUNCHER_AP_PORT:-45645}"
LAUNCHER_DISPLAY="${LAUNCHER_DISPLAY:-:98}"
LAUNCHER_WAIT="${LAUNCHER_WAIT:-90}"
LAUNCHER_KEEP="${LAUNCHER_KEEP:-0}"

fail() { echo "ci_launcher_test: $1" >&2; exit 1; }

[ -n "${O3DE_ENGINE_PATH:-}" ] || fail "O3DE_ENGINE_PATH is not set"
[ -n "${AICOMPANION_PROJECT:-}" ] || fail "AICOMPANION_PROJECT is not set"
BUILD_DIR="${BUILD_DIR:-$AICOMPANION_PROJECT/build/linux}"
BIN="$O3DE_ENGINE_PATH/bin/Linux/$BUILD_CONFIG/Default"
PROJECT_NAME="$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['project_name'])" "$AICOMPANION_PROJECT/project.json")"
GAME="$BUILD_DIR/bin/$BUILD_CONFIG/$PROJECT_NAME.GameLauncher"
[ -x "$BIN/Editor" ] || fail "no Editor under $BIN"
[ -x "$BIN/AssetProcessor" ] || fail "no AssetProcessor under $BIN"
if [ ! -x "$GAME" ]; then
    echo "== building $PROJECT_NAME.GameLauncher (not found at $GAME) =="
    cmake --build "$BUILD_DIR" --config "$BUILD_CONFIG" --target "$PROJECT_NAME.GameLauncher" -j \
        || fail "could not build $PROJECT_NAME.GameLauncher"
    [ -x "$GAME" ] || fail "GameLauncher still missing after build: $GAME"
fi
command -v Xvfb >/dev/null || fail "Xvfb is not installed"

STATE="$(mktemp -d)"
LEVEL_DIR="$AICOMPANION_PROJECT/Levels/$LAUNCHER_LEVEL"
[ -e "$LEVEL_DIR" ] && fail "level $LEVEL_DIR already exists; remove it or choose another LAUNCHER_LEVEL"
echo "== AiCompanion launcher test =="
echo "  engine : $O3DE_ENGINE_PATH"
echo "  project: $AICOMPANION_PROJECT ($PROJECT_NAME)"
echo "  level  : $LAUNCHER_LEVEL   port: $LAUNCHER_PORT   display: $LAUNCHER_DISPLAY"
echo "  logs   : $STATE"

AP_PID=""; XVFB_PID=""; EDITOR_PID=""; GAME_PID=""
stop() { local pid=$1; [ -n "$pid" ] || return 0; kill "$pid" 2>/dev/null || true; for _ in 1 2 3 4 5 6; do kill -0 "$pid" 2>/dev/null || return 0; sleep 1; done; kill -9 "$pid" 2>/dev/null || true; }
cleanup() {
    [ "$LAUNCHER_KEEP" = "1" ] && { echo "LAUNCHER_KEEP=1: leaving processes and $LEVEL_DIR in place"; return 0; }
    stop "$GAME_PID"; stop "$EDITOR_PID"; stop "$AP_PID"; stop "$XVFB_PID"
    pkill -f "[A]ssetBuilder .*project-path=\"?$AICOMPANION_PROJECT" 2>/dev/null || true
    rm -rf "$LEVEL_DIR"
}
trap cleanup EXIT

export PYTHONPATH="$GEM_PATH/Tests/live${PYTHONPATH:+:$PYTHONPATH}"
export O3DE_EDITOR_PORT="$LAUNCHER_PORT"

# AP, with a real idle check on the project's AP_GUI.log (never on stdout).
ap_log="$AICOMPANION_PROJECT/user/log/AP_GUI.log"
wait_ap_idle() {
    local before=$1 now
    for _ in $(seq 1 600); do
        if [ -f "$ap_log" ]; then
            now="$(wc -l <"$ap_log")"; (( now < before )) && before=0
            tail -n +"$((before + 1))" "$ap_log" | grep -q "Asset Processor is currently idle" && return 0
        fi
        sleep 1
    done
    return 1
}
before=0; [ -f "$ap_log" ] && before="$(wc -l <"$ap_log")"
"$BIN/AssetProcessor" --project-path="$AICOMPANION_PROJECT" \
    --regset="/Amazon/AzCore/Bootstrap/remote_port=$LAUNCHER_AP_PORT" >"$STATE/ap.log" 2>&1 &
AP_PID=$!
wait_ap_idle "$before" && echo "== AssetProcessor idle ==" || echo "== AssetProcessor idle not seen in 600s; continuing =="

Xvfb "$LAUNCHER_DISPLAY" -screen 0 1600x900x24 -nocursor >"$STATE/xvfb.log" 2>&1 &
XVFB_PID=$!
sleep 2

# Editor phase: build the arena through the gem API and save it as a level.
DISPLAY="$LAUNCHER_DISPLAY" O3DE_EDITOR_PORT="$LAUNCHER_PORT" "$BIN/Editor" --project-path="$AICOMPANION_PROJECT" \
    --skipWelcomeScreenDialog --rhi=vulkan --rhi-device-validation=disable \
    --regset="/Amazon/AzCore/Bootstrap/remote_port=$LAUNCHER_AP_PORT" >"$STATE/editor.log" 2>&1 &
EDITOR_PID=$!
ready=0
for _ in $(seq 1 300); do
    if python3 - <<'PY' 2>/dev/null; then ready=1; break; fi
import sys
from agent_client import AgentClient
try:
    out = AgentClient(timeout=10).run("print('python-ready')")
except Exception:
    sys.exit(1)
sys.exit(0 if "python-ready" in out else 1)
PY
    sleep 2
done
[ "$ready" = "1" ] || fail "editor Python never became ready (see $STATE/editor.log)"
echo "== editor Python ready =="

before=0; [ -f "$ap_log" ] && before="$(wc -l <"$ap_log")"
python3 "$GEM_PATH/Tests/live/build_launcher_arena.py" "$LAUNCHER_LEVEL" || fail "building the arena level failed (see $STATE/editor.log)"
echo "== arena built and saved =="
wait_ap_idle "$before" && echo "== level processed by AssetProcessor ==" || echo "== AssetProcessor idle not seen after save; continuing =="
stop "$EDITOR_PID"; EDITOR_PID=""

# Launcher phase. Game.log is recreated per run; its first line holds the launch time.
game_log="$AICOMPANION_PROJECT/user/log/Game.log"
head_before="$( [ -f "$game_log" ] && head -1 "$game_log" || true )"
DISPLAY="$LAUNCHER_DISPLAY" "$GAME" --rhi=vulkan --rhi-device-validation=disable \
    --project-path="$AICOMPANION_PROJECT" \
    --regset="/Amazon/AzCore/Bootstrap/remote_port=$LAUNCHER_AP_PORT" \
    +LoadLevel "$LAUNCHER_LEVEL" >"$STATE/launcher.log" 2>&1 &
GAME_PID=$!
new_log() { [ -f "$game_log" ] && [ "$(head -1 "$game_log")" != "$head_before" ]; }
loaded=0
for _ in $(seq 1 300); do
    if new_log && grep -qi "Level load complete: .*$(echo "$LAUNCHER_LEVEL" | tr '[:upper:]' '[:lower:]')" "$game_log"; then loaded=1; break; fi
    kill -0 "$GAME_PID" 2>/dev/null || fail "GameLauncher exited before loading the level (see $STATE/launcher.log)"
    sleep 1
done
[ "$loaded" = "1" ] || fail "launcher never reported '$LAUNCHER_LEVEL' loaded (see $game_log)"
echo "== level loaded in the launcher; waiting up to ${LAUNCHER_WAIT}s for gameplay markers =="
for _ in $(seq 1 "$LAUNCHER_WAIT"); do
    if grep -q "\[AiCompanion\] pickup collected" "$game_log" && grep -q "\[AiCompanion\] enemy attack" "$game_log" \
        && grep -q "\[AiCompanion\] contact damage" "$game_log"; then break; fi
    sleep 1
done
cp "$game_log" "$STATE/game.log"
stop "$GAME_PID"; GAME_PID=""

echo "== run launcher checks =="
cd "$GEM_PATH"
AICOMPANION_GAME_LOG="$STATE/game.log" AICOMPANION_LAUNCHER_LEVEL="$LAUNCHER_LEVEL" python3 -m pytest Tests/live/test_launcher.py -v
echo "== launcher checks passed =="
