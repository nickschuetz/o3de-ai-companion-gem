# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Scene snapshot feedback. Calls the C++ SceneSnapshotProvider via EBus."""

from ..utils.id_helpers import id_to_jsonable
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


def _enumerate_all_entities():
    """Return a list of entity ids in the current level.

    Tries `ToolsApplicationRequestBus.GetAllEntities` first (older builds),
    falls back to `entity.SearchBus.SearchEntities` with a wildcard filter
    (O3DE 2310+). Returns an empty list if both paths fail.
    """
    import azlmbr.editor as editor
    import azlmbr.bus as bus

    result = editor.ToolsApplicationRequestBus(bus.Broadcast, "GetAllEntities")
    if result:
        return result

    try:
        import azlmbr.entity as entity_api
        search_filter = entity_api.SearchFilter()
        search_filter.names = ["*"]
        ids = entity_api.SearchBus(bus.Broadcast, "SearchEntities", search_filter)
        return ids or []
    except (ImportError, AttributeError):
        return []


def _python_fallback_snapshot() -> str:
    """Fallback snapshot using pure Python azlmbr calls."""
    try:
        import azlmbr.editor as editor
        import azlmbr.bus as bus
        import azlmbr.components as components
        import json

        entity_ids = _enumerate_all_entities()

        entities = []
        for eid in entity_ids:
            name = editor.EditorEntityInfoRequestBus(
                bus.Event, "GetName", eid
            )

            pos = components.TransformBus(
                bus.Event, "GetWorldTranslation", eid
            )

            entity_data = {
                "id": id_to_jsonable(eid),
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

        entity_ids = _enumerate_all_entities()

        nodes = []
        for eid in entity_ids:
            name = editor.EditorEntityInfoRequestBus(
                bus.Event, "GetName", eid
            )
            parent = editor.EditorEntityInfoRequestBus(
                bus.Event, "GetParent", eid
            )
            parent_valid = False
            if parent is not None:
                try:
                    parent_valid = parent.IsValid()
                except AttributeError:
                    parent_valid = bool(parent)
            nodes.append({
                "id": id_to_jsonable(eid),
                "name": name or "",
                "parent_id": id_to_jsonable(parent) if parent_valid else None,
            })

        return success({"roots": nodes, "source": "python_fallback"})

    except ImportError:
        return error("Cannot capture entity tree: not running inside O3DE Editor")
