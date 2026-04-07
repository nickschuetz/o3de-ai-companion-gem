# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Deep entity inspection — detailed component and property information."""

import json
from ..utils.json_output import success, error


def inspect_entity(entity_id: int) -> str:
    """Get detailed information about a specific entity.

    Args:
        entity_id: The entity ID to inspect.

    Returns:
        JSON with entity name, components, properties, transform, and children.
    """
    try:
        import azlmbr.editor as editor
        import azlmbr.bus as bus
        import azlmbr.components as components

        eid = entity_id

        # Get basic info
        name = editor.EditorEntityInfoRequestBus(bus.Event, "GetName", eid)

        # Get transform
        pos = components.TransformBus(bus.Event, "GetWorldTranslation", eid)
        position = [pos.x, pos.y, pos.z] if pos else [0, 0, 0]

        # Get component list
        component_list = editor.EditorComponentAPIBus(
            bus.Broadcast, "GetComponentsOfEntity", eid
        )

        comp_details = []
        if component_list:
            for comp_id in component_list:
                comp_type = editor.EditorComponentAPIBus(
                    bus.Broadcast, "GetComponentName", comp_id
                )
                comp_details.append({
                    "component_id": int(comp_id) if comp_id else 0,
                    "type": comp_type or "Unknown",
                })

        # Get children
        children = editor.EditorEntityInfoRequestBus(
            bus.Event, "GetChildren", eid
        )
        child_ids = [int(c) for c in children] if children else []

        return success({
            "entity_id": entity_id,
            "name": name or "",
            "position": position,
            "components": comp_details,
            "child_count": len(child_ids),
            "child_ids": child_ids,
        })

    except ImportError:
        return error("Cannot inspect entity: not running inside O3DE Editor")
