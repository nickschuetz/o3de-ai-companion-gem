# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Camera entity templates."""

from typing import List, Optional, Union

from ..builders.entity_builder import EntityBuilder

Number = Union[int, float]

CAMERA_PRESETS = {
    "top_down": {
        "position_offset": [0, 0, 20],
        "rotation": [-90, 0, 0],
    },
    "isometric": {
        "position_offset": [10, -10, 15],
        "rotation": [-45, 0, 45],
    },
    "perspective": {
        "position_offset": [0, -10, 5],
        "rotation": [-20, 0, 0],
    },
    "side_view": {
        "position_offset": [20, 0, 5],
        "rotation": [0, 0, -90],
    },
}


def create_camera_entity(
    name: str = "MainCamera",
    camera_type: str = "perspective",
    follow_target: Optional[str] = None,
    offset: Optional[List[Number]] = None,
) -> str:
    """Create a camera entity.

    Args:
        name: Camera entity name.
        camera_type: Camera preset ("top_down", "isometric", "perspective", "side_view").
        follow_target: Name of entity to follow (requires a follow script).
        offset: Custom position offset [x, y, z]. Overrides preset offset.

    Returns:
        JSON with entity details.
    """
    if camera_type not in CAMERA_PRESETS:
        available = ", ".join(CAMERA_PRESETS.keys())
        raise ValueError(f"Unknown camera type '{camera_type}'. Available: {available}")

    preset = CAMERA_PRESETS[camera_type]
    pos = offset or preset["position_offset"]
    rot = preset["rotation"]

    builder = (
        EntityBuilder(name)
        .at_position(pos[0], pos[1], pos[2])
        .with_rotation(rot[0], rot[1], rot[2])
        .with_component("Camera")
    )

    result = builder.build()

    # Make this entity the active rendering camera, both for the editor
    # viewport (via SetViewFromEntityPerspective) and for game mode (by
    # disabling "Make active camera on activation?" on every other Camera
    # entity so this one wins the activation race).
    try:
        import json as _json
        parsed = _json.loads(result)
        if parsed.get("status") == "ok" and "entity_id" in parsed.get("data", {}):
            _activate_camera(parsed["data"]["entity_id"])
    except Exception:
        pass

    return result


def _activate_camera(keeper_entity_id) -> None:
    """Make ``keeper_entity_id`` the active editor view and force the
    "Make active camera on activation?" property to False on every other
    Camera component so it also wins on game mode entry.

    Uses direct EBus calls via Python. Safe to call repeatedly.
    """
    try:
        import azlmbr.bus as bus
        import azlmbr.editor as editor
        import azlmbr.entity as entity_api
        import azlmbr.camera as camera_api
    except ImportError:
        return

    keeper_eid = _eid_from_jsonable(keeper_entity_id)
    if keeper_eid is None:
        return

    cam_tids = editor.EditorComponentAPIBus(
        bus.Broadcast, "FindComponentTypeIdsByEntityType",
        ["Camera"], entity_api.EntityType().Game,
    )
    if not cam_tids:
        return
    cam_tid = cam_tids[0]

    search_filter = entity_api.SearchFilter()
    search_filter.names = ["*"]
    all_ids = entity_api.SearchBus(bus.Broadcast, "SearchEntities", search_filter)

    for eid in all_ids or []:
        has = editor.EditorComponentAPIBus(
            bus.Broadcast, "HasComponentOfType", eid, cam_tid
        )
        if not has:
            continue
        outcome = editor.EditorComponentAPIBus(
            bus.Broadcast, "GetComponentOfType", eid, cam_tid
        )
        if not (hasattr(outcome, "IsSuccess") and outcome.IsSuccess()):
            continue
        pair = outcome.GetValue()
        if isinstance(pair, list) and pair:
            pair = pair[0]
        keep = _eids_equal(eid, keeper_eid)
        editor.EditorComponentAPIBus(
            bus.Broadcast, "SetComponentProperty",
            pair,
            "Controller|Configuration|Make active camera on activation?",
            bool(keep),
        )

    # Drive the editor viewport directly so the new camera is what we see
    # immediately, not just on game-mode entry.
    camera_api.EditorCameraRequestBus(
        bus.Broadcast, "SetViewFromEntityPerspective", keeper_eid
    )


def _eid_from_jsonable(value):
    """Recover an EntityId proxy from the JSON-safe form ``[12345]``,
    a raw int, or a string. Returns None if it can't be parsed.
    """
    try:
        import azlmbr.entity as entity_api
    except ImportError:
        return None

    if isinstance(value, str):
        digits = "".join(ch for ch in value if ch.isdigit())
        if not digits:
            return None
        return entity_api.EntityId(int(digits))
    if isinstance(value, int):
        return entity_api.EntityId(value)
    return value


def _eids_equal(a, b) -> bool:
    """Compare two EntityId-ish values by their numeric form."""
    def _digits(x):
        return "".join(ch for ch in str(x) if ch.isdigit())
    return _digits(a) == _digits(b)
