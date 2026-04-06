# Copyright Contributors to the AI Companion for O3DE Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Scene validation — checks for common issues in AI-created scenes."""

from ..utils.json_output import success, error


def validate_scene() -> str:
    """Validate the current scene for common issues.

    Checks for:
    - Unnamed entities
    - Entities stacked at the origin
    - Entities with no components beyond Transform
    - Overlapping entity positions

    Returns JSON validation report.
    """
    try:
        import azlmbr.bus as bus

        # Try C++ EBus first for performance
        try:
            import azlmbr.ai_companion as ai_companion_bus
            result = ai_companion_bus.AiCompanionRequestBus(
                bus.Broadcast, "ValidateScene"
            )
            if result:
                return result
        except (ImportError, AttributeError):
            pass

        # Python fallback
        return _python_validate()

    except ImportError:
        return error("Cannot validate scene: not running inside O3DE Editor")


def _python_validate() -> str:
    """Python-based scene validation fallback."""
    try:
        import azlmbr.editor as editor
        import azlmbr.bus as bus
        import azlmbr.components as components

        entity_ids = editor.ToolsApplicationRequestBus(
            bus.Broadcast, "GetAllEntities"
        )

        warnings = []

        for eid in entity_ids:
            name = editor.EditorEntityInfoRequestBus(bus.Event, "GetName", eid)

            if not name:
                warnings.append({
                    "type": "unnamed_entity",
                    "entity_id": int(eid),
                    "message": "Entity has no name",
                })

            pos = components.TransformBus(bus.Event, "GetWorldTranslation", eid)
            if pos and abs(pos.x) < 0.001 and abs(pos.y) < 0.001 and abs(pos.z) < 0.001:
                warnings.append({
                    "type": "at_origin",
                    "entity_id": int(eid),
                    "entity_name": name or "",
                    "message": "Entity is positioned at the origin",
                })

        return success({
            "entity_count": len(entity_ids) if entity_ids else 0,
            "warnings": warnings,
            "status": "ok",
        })

    except ImportError:
        return error("Cannot validate scene: not running inside O3DE Editor")
