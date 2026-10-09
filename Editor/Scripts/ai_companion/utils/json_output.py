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


# Error codes every API error carries in its top-level ``code`` field, so an
# agent can branch on them instead of parsing the message:
#   validation_failed  an argument failed the safety validators
#   limit_exceeded     a sandbox limit (entity count, depth, timeout) was hit
#   not_in_editor      the call needs the O3DE Editor's azlmbr and it is absent
#   not_found          the named prefab, file or entity does not exist
#   editor_running     the operation needs the editor closed
#   io_error           a file could not be read or written
#   engine_error       an azlmbr call failed or raised
#   instantiate_failed the prefab system returned a failed outcome
ERROR_CODES = (
    "validation_failed",
    "limit_exceeded",
    "not_in_editor",
    "not_found",
    "editor_running",
    "io_error",
    "engine_error",
    "instantiate_failed",
)


def error(message: str, details: Any = None, rolled_back: bool = False, code: str = "engine_error") -> str:
    """Return a JSON error response.

    ``code`` is one of ``ERROR_CODES`` (or a function-specific code such as
    ``prefab_not_found``) and is also mirrored into ``details.code`` for
    callers written against 0.4.0.
    """
    result: Dict[str, Any] = {
        "status": "error",
        "code": code,
        "message": message,
    }
    if details is None:
        details = {}
    if isinstance(details, dict):
        details = dict(details)
        details.setdefault("code", code)
    result["details"] = details
    if rolled_back:
        result["rolled_back"] = True
    return json.dumps(result)


def entity_result(
    entity_id: int,
    name: str,
    component_ids: Optional[Dict[str, int]] = None,
    position: Optional[List[float]] = None,
    extra: Optional[Dict[str, Any]] = None,
) -> str:
    """Return a JSON response for a created entity.

    ``extra`` holds further per-entity fields (for example the component
    properties a build applied and any it could not), merged into ``data``.
    """
    data: Dict[str, Any] = {
        "entity_id": entity_id,
        "name": name,
    }
    if component_ids is not None:
        data["component_ids"] = component_ids
    if position is not None:
        data["position"] = position
    if extra:
        data.update(extra)
    return success(data)


def batch_result(entities: List[Dict[str, Any]], total: int) -> str:
    """Return a JSON response for a batch operation."""
    return success({
        "entities": entities,
        "total_created": total,
    })
