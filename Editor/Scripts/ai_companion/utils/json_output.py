# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Structured JSON output helpers for consistent API responses."""

import json
from typing import Any, Dict, List, Optional


def success(data: Any = None, message: str = "") -> str:
    """Return a JSON success response."""
    result: Dict[str, Any] = {"status": "ok"}
    if message:
        result["message"] = message
    if data is not None:
        result["data"] = data
    return json.dumps(result)


def error(message: str, details: Any = None, rolled_back: bool = False) -> str:
    """Return a JSON error response."""
    result: Dict[str, Any] = {
        "status": "error",
        "message": message,
    }
    if details is not None:
        result["details"] = details
    if rolled_back:
        result["rolled_back"] = True
    return json.dumps(result)


def entity_result(
    entity_id: int,
    name: str,
    component_ids: Optional[Dict[str, int]] = None,
    position: Optional[List[float]] = None,
) -> str:
    """Return a JSON response for a created entity."""
    data: Dict[str, Any] = {
        "entity_id": entity_id,
        "name": name,
    }
    if component_ids is not None:
        data["component_ids"] = component_ids
    if position is not None:
        data["position"] = position
    return success(data)


def batch_result(entities: List[Dict[str, Any]], total: int) -> str:
    """Return a JSON response for a batch operation."""
    return success({
        "entities": entities,
        "total_created": total,
    })
