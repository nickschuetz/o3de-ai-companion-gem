# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Undo-batch / rollback support for AI-driven operations.

Every high-level API function should be wrapped with @with_undo_batch
to ensure that failed operations are automatically rolled back."""

import functools

from .sandbox import reset_sandbox, get_sandbox

_batch_depth = 0


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


def _undo() -> None:
    """Undo the last operation (rolls back the most recent batch)."""
    try:
        import azlmbr.editor as editor
        import azlmbr.bus as bus
        editor.ToolsApplicationRequestBus(bus.Broadcast, "Undo")
        return
    except (ImportError, AttributeError):
        pass

    try:
        import azlmbr.legacy.general as general
        general.undo()
    except ImportError:
        pass


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
            global _batch_depth

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
                return result
            except Exception:
                _batch_depth -= 1
                if is_outermost:
                    _end_undo()
                    _undo()
                raise
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


def rollback_last_batch():
    """Undo the last completed undo batch."""
    _undo()
