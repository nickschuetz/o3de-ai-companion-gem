# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Input validation for AI-driven operations. All validation happens before
any azlmbr calls to prevent injection and invalid state."""

import math
import re
from typing import Tuple

# Entity names: start with letter, then alphanumeric/underscore/hyphen
_ENTITY_NAME_PATTERN = re.compile(r'^[A-Za-z][A-Za-z0-9_-]*$')
MAX_ENTITY_NAME_LENGTH = 128

# Component types: letters, digits, spaces, hyphens, underscores, parens
_COMPONENT_TYPE_PATTERN = re.compile(r'^[A-Za-z][A-Za-z0-9 _\-()\[\]]*$')

# Asset paths: no traversal, no absolute, no null bytes
_PATH_TRAVERSAL_PATTERN = re.compile(r'\.\.')

# Position bounds
MAX_POSITION_BOUND = 10000.0

# Protected system entity names that cannot be deleted/modified
PROTECTED_ENTITIES = frozenset([
    "EditorGlobal",
    "SystemEntity",
    "AZ::SystemEntity",
])


def validate_entity_name(name: str) -> Tuple[bool, str]:
    """Validate an entity name. Returns (valid, error_message)."""
    if not isinstance(name, str):
        return False, f"Entity name must be a string, got {type(name).__name__}"
    if not name:
        return False, "Entity name cannot be empty"
    if len(name) > MAX_ENTITY_NAME_LENGTH:
        return False, f"Entity name exceeds {MAX_ENTITY_NAME_LENGTH} characters"
    if not _ENTITY_NAME_PATTERN.match(name):
        return False, (
            "Entity name must start with a letter and contain only "
            "letters, digits, underscores, or hyphens"
        )
    return True, ""


def validate_component_type(component_type: str) -> Tuple[bool, str]:
    """Validate a component type name. Returns (valid, error_message)."""
    if not isinstance(component_type, str):
        return False, f"Component type must be a string, got {type(component_type).__name__}"
    if not component_type:
        return False, "Component type cannot be empty"
    if len(component_type) > 256:
        return False, "Component type name is too long"
    if not _COMPONENT_TYPE_PATTERN.match(component_type):
        return False, (
            "Component type contains invalid characters. "
            "Allowed: letters, digits, spaces, hyphens, underscores, parentheses"
        )
    return True, ""


def validate_position(position) -> Tuple[bool, str]:
    """Validate a 3D position [x, y, z]. Returns (valid, error_message)."""
    if not isinstance(position, (list, tuple)):
        return False, f"Position must be a list or tuple, got {type(position).__name__}"
    if len(position) != 3:
        return False, f"Position must have exactly 3 elements, got {len(position)}"

    for i, val in enumerate(position):
        if not isinstance(val, (int, float)):
            return False, f"Position[{i}] must be a number, got {type(val).__name__}"
        if math.isnan(val) or math.isinf(val):
            return False, f"Position[{i}] must be finite (got {'NaN' if math.isnan(val) else 'Inf'})"
        if abs(val) > MAX_POSITION_BOUND:
            return False, (
                f"Position[{i}] = {val} exceeds bounds (±{MAX_POSITION_BOUND})"
            )

    return True, ""


def validate_asset_path(path: str) -> Tuple[bool, str]:
    """Validate a script/asset path. Returns (valid, error_message)."""
    if not isinstance(path, str):
        return False, f"Path must be a string, got {type(path).__name__}"
    if not path:
        return False, "Path cannot be empty"
    if len(path) > 1024:
        return False, "Path is too long"
    if '\0' in path:
        return False, "Path contains null bytes"
    if _PATH_TRAVERSAL_PATTERN.search(path):
        return False, "Path contains directory traversal (..)"
    if path.startswith('/') or path.startswith('\\'):
        return False, "Path must be relative, not absolute"
    if len(path) >= 2 and path[1] == ':':
        return False, "Path must be relative, not an absolute Windows path"
    return True, ""


def validate_float(value, min_val: float, max_val: float, name: str = "value") -> Tuple[bool, str]:
    """Validate a float is finite and within bounds. Returns (valid, error_message)."""
    if not isinstance(value, (int, float)):
        return False, f"{name} must be a number, got {type(value).__name__}"
    if math.isnan(value) or math.isinf(value):
        return False, f"{name} must be finite"
    if value < min_val or value > max_val:
        return False, f"{name} = {value} is out of range [{min_val}, {max_val}]"
    return True, ""


def is_protected_entity(name: str) -> bool:
    """Check if an entity name matches a protected system entity."""
    if name in PROTECTED_ENTITIES:
        return True
    if name.startswith("AZ::"):
        return True
    return False
