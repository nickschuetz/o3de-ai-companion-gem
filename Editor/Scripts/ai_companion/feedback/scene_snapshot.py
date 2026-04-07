# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Scene snapshot feedback — calls the C++ SceneSnapshotProvider via EBus."""

from ..utils.json_output import success, error


def get_scene_snapshot() -> str:
    """Get a full snapshot of the current scene via the C++ EBus.

    Returns JSON with all entities, their transforms, and component lists.
    Falls back to Python-based traversal if the EBus is unavailable.
    """
    try:
        import azlmbr.bus as bus

        result = None

        def on_result(args):
            nonlocal result
            result = args

        # Call the C++ SceneSnapshotProvider via AiCompanionRequestBus
        import azlmbr.ai_companion as ai_companion_bus
        result = ai_companion_bus.AiCompanionRequestBus(
            bus.Broadcast, "GetSceneSnapshot"
        )

        if result:
            return result  # Already JSON from C++

    except (ImportError, AttributeError):
        pass

    # Fallback: Python-based entity enumeration
    return _python_fallback_snapshot()


def get_entity_tree() -> str:
    """Get the entity hierarchy tree via the C++ EBus."""
    try:
        import azlmbr.bus as bus
        import azlmbr.ai_companion as ai_companion_bus

        result = ai_companion_bus.AiCompanionRequestBus(
            bus.Broadcast, "GetEntityTree"
        )
        if result:
            return result

    except (ImportError, AttributeError):
        pass

    return _python_fallback_tree()


def _python_fallback_snapshot() -> str:
    """Fallback snapshot using pure Python azlmbr calls."""
    try:
        import azlmbr.editor as editor
        import azlmbr.bus as bus
        import azlmbr.components as components
        import json

        # Get all entity IDs
        entity_ids = editor.ToolsApplicationRequestBus(
            bus.Broadcast, "GetAllEntities"
        )

        entities = []
        for eid in entity_ids:
            name = editor.EditorEntityInfoRequestBus(
                bus.Event, "GetName", eid
            )

            pos = components.TransformBus(
                bus.Event, "GetWorldTranslation", eid
            )

            entity_data = {
                "id": int(eid),
                "name": name or "",
                "position": [pos.x, pos.y, pos.z] if pos else [0, 0, 0],
            }
            entities.append(entity_data)

        return success({
            "entity_count": len(entities),
            "entities": entities,
            "source": "python_fallback",
        })

    except ImportError:
        return error("Cannot capture snapshot: not running inside O3DE Editor")


def _python_fallback_tree() -> str:
    """Fallback entity tree using pure Python azlmbr calls."""
    try:
        import azlmbr.editor as editor
        import azlmbr.bus as bus
        import json

        entity_ids = editor.ToolsApplicationRequestBus(
            bus.Broadcast, "GetAllEntities"
        )

        nodes = []
        for eid in entity_ids:
            name = editor.EditorEntityInfoRequestBus(
                bus.Event, "GetName", eid
            )
            parent = editor.EditorEntityInfoRequestBus(
                bus.Event, "GetParent", eid
            )
            nodes.append({
                "id": int(eid),
                "name": name or "",
                "parent_id": int(parent) if parent and parent.IsValid() else None,
            })

        return success({"roots": nodes, "source": "python_fallback"})

    except ImportError:
        return error("Cannot capture entity tree: not running inside O3DE Editor")
