# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Undo-batch / rollback support for AI-driven operations.

Every high-level API function should be wrapped with @with_undo_batch
to ensure that failed operations are automatically rolled back."""

import functools

from .sandbox import reset_sandbox, get_sandbox, SandboxLimitError
from ..utils.json_output import error

_batch_depth = 0

# Entity ids created by the last completed outermost batch, kept so
# rollback_last_batch() can delete whatever the editor's Undo leaves behind.
_last_batch_entities: list = []


def _begin_undo(label: str) -> None:
    """Begin an O3DE undo batch.

    Tries the modern ToolsApplicationRequestBus.BeginUndoBatch path first
    (O3DE 2310+), then falls back to the legacy azlmbr.legacy.general API
    if it exists. Silently no-ops outside the editor.
    """
    try:
        import azlmbr.editor as editor
        import azlmbr.bus as bus
        editor.ToolsApplicationRequestBus(bus.Broadcast, "BeginUndoBatch", label)
        return
    except (ImportError, AttributeError):
        pass

    try:
        import azlmbr.legacy.general as general
        if hasattr(general, "begin_undo_batch"):
            general.begin_undo_batch(label)
    except ImportError:
        pass


def _end_undo() -> None:
    """End the current O3DE undo batch."""
    try:
        import azlmbr.editor as editor
        import azlmbr.bus as bus
        editor.ToolsApplicationRequestBus(bus.Broadcast, "EndUndoBatch")
        return
    except (ImportError, AttributeError):
        pass

    try:
        import azlmbr.legacy.general as general
        if hasattr(general, "end_undo_batch"):
            general.end_undo_batch()
    except ImportError:
        pass


def _entity_exists(entity_id) -> bool:
    try:
        import azlmbr.editor as editor
        import azlmbr.bus as bus
        return bool(editor.ToolsApplicationRequestBus(bus.Broadcast, "EntityExists", entity_id))
    except (ImportError, AttributeError):
        return False


def _delete_survivors(entity_ids) -> int:
    """Delete any of ``entity_ids`` that still exist. Returns how many were deleted.

    On O3DE 26.10 an entity carrying a Lua Script can survive Undo: the
    editor's ScriptEditorComponent::LoadScript opens an undo batch from
    inside the undo's prefab re-instantiation and ToolsApplication rejects
    it. The gem knows every entity it created, so rollback finishes the job.
    """
    deleted = 0
    try:
        import azlmbr.editor as editor
        import azlmbr.bus as bus
    except ImportError:
        return 0
    for entity_id in entity_ids:
        if _entity_exists(entity_id):
            editor.ToolsApplicationRequestBus(bus.Broadcast, "DeleteEntityById", entity_id)
            if not _entity_exists(entity_id):
                deleted += 1
    return deleted


def _undo(entity_ids=None) -> dict:
    """Undo the last operation (rolls back the most recent batch), then delete
    any recorded entity the undo left behind.

    ``ToolsApplicationRequestBus`` reflects ``BeginUndoBatch`` and
    ``EndUndoBatch`` to Python but no ``Undo`` event: a call with that name
    returns ``None`` without raising, so it must not be tried first. The
    editor's undo itself is reachable through ``azlmbr.legacy.general.undo``.
    """
    undone = False
    try:
        import azlmbr.legacy.general as general
        general.undo()
        undone = True
    except (ImportError, AttributeError):
        pass
    deleted = _delete_survivors(entity_ids or [])
    return {"rolled_back": undone, "leftover_entities_deleted": deleted}


def _error_code_for(exc: BaseException) -> str:
    if isinstance(exc, SandboxLimitError):
        return "limit_exceeded"
    if isinstance(exc, (ValueError, TypeError)):
        return "validation_failed"
    if isinstance(exc, ImportError):
        return "not_in_editor"
    return "engine_error"


def with_undo_batch(label: str):
    """Decorator that wraps an API function in an undo batch.

    If the function raises an exception, the batch is ended and undone,
    effectively rolling back all operations performed within the function.

    Args:
        label: Human-readable label for the undo batch (shown in Edit > Undo).
    """
    def decorator(func):
        @functools.wraps(func)
        def wrapper(*args, **kwargs):
            global _batch_depth, _last_batch_entities

            is_outermost = _batch_depth == 0
            if is_outermost:
                reset_sandbox()
                _begin_undo(label)
            _batch_depth += 1

            try:
                result = func(*args, **kwargs)
                _batch_depth -= 1
                if is_outermost:
                    _end_undo()
                    _last_batch_entities = get_sandbox().created_entity_ids
                return result
            except Exception as exc:
                _batch_depth -= 1
                if not is_outermost:
                    raise
                # Outermost call: close the batch, undo it, delete anything the
                # undo left behind, and hand the agent JSON instead of a traceback.
                _end_undo()
                outcome = _undo(get_sandbox().created_entity_ids)
                _last_batch_entities = []
                return error(
                    str(exc) or exc.__class__.__name__,
                    code=_error_code_for(exc),
                    details={"exception": exc.__class__.__name__, "operation": label, **outcome},
                    rolled_back=True,
                )
        return wrapper
    return decorator


def begin_undo_batch(label: str = "AI Operation"):
    """Manually begin an undo batch. Must be paired with end_undo_batch()."""
    global _batch_depth
    _batch_depth += 1
    if _batch_depth == 1:
        reset_sandbox()
    _begin_undo(label)


def end_undo_batch():
    """Manually end the current undo batch."""
    global _batch_depth
    _end_undo()
    _batch_depth = max(0, _batch_depth - 1)


def rollback_last_batch() -> dict:
    """Undo the last completed undo batch and delete any entity it created
    that the editor's Undo left behind. Returns the outcome as a dict."""
    global _last_batch_entities
    outcome = _undo(_last_batch_entities)
    _last_batch_entities = []
    return outcome
