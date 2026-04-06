# Copyright Contributors to the AI Companion for O3DE Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Operation sandboxing to prevent runaway AI operations."""

import time
from typing import Optional

# Configurable limits
MAX_ENTITIES_PER_CALL = 100
MAX_RECURSION_DEPTH = 10
MAX_OPERATION_TIMEOUT_SECONDS = 30.0


class SandboxLimitError(Exception):
    """Raised when an operation exceeds sandbox limits."""
    pass


class OperationSandbox:
    """Tracks and enforces limits on AI-driven operations within a single API call."""

    def __init__(
        self,
        max_entities: int = MAX_ENTITIES_PER_CALL,
        max_depth: int = MAX_RECURSION_DEPTH,
        timeout: float = MAX_OPERATION_TIMEOUT_SECONDS,
    ):
        self._max_entities = max_entities
        self._max_depth = max_depth
        self._timeout = timeout
        self._entities_created = 0
        self._current_depth = 0
        self._start_time: Optional[float] = None

    def begin(self):
        """Start tracking an operation."""
        self._entities_created = 0
        self._current_depth = 0
        self._start_time = time.monotonic()

    def check_entity_limit(self, count: int = 1):
        """Check if creating `count` more entities would exceed the limit."""
        if self._entities_created + count > self._max_entities:
            raise SandboxLimitError(
                f"Entity creation limit exceeded: would create "
                f"{self._entities_created + count} entities "
                f"(limit: {self._max_entities})"
            )
        self._entities_created += count

    def check_depth(self):
        """Check recursion depth before entering a nested operation."""
        self._current_depth += 1
        if self._current_depth > self._max_depth:
            raise SandboxLimitError(
                f"Recursion depth limit exceeded: depth {self._current_depth} "
                f"(limit: {self._max_depth})"
            )

    def exit_depth(self):
        """Exit a nested operation level."""
        self._current_depth = max(0, self._current_depth - 1)

    def check_timeout(self):
        """Check if the operation has exceeded the timeout."""
        if self._start_time is not None:
            elapsed = time.monotonic() - self._start_time
            if elapsed > self._timeout:
                raise SandboxLimitError(
                    f"Operation timeout exceeded: {elapsed:.1f}s "
                    f"(limit: {self._timeout}s)"
                )

    @property
    def entities_created(self) -> int:
        return self._entities_created

    @property
    def elapsed_time(self) -> float:
        if self._start_time is None:
            return 0.0
        return time.monotonic() - self._start_time


# Global sandbox instance for the current operation
_current_sandbox: Optional[OperationSandbox] = None


def get_sandbox() -> OperationSandbox:
    """Get the current operation sandbox, creating one if needed."""
    global _current_sandbox
    if _current_sandbox is None:
        _current_sandbox = OperationSandbox()
    return _current_sandbox


def reset_sandbox():
    """Reset the global sandbox for a new operation."""
    global _current_sandbox
    _current_sandbox = OperationSandbox()
    _current_sandbox.begin()
