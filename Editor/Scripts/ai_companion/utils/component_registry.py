# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Component name registry for O3DE. Maps human-readable names to component
metadata and provides fuzzy matching to prevent mistyped component names."""

import re
from typing import Dict, List, Optional, Tuple


class ComponentNotFoundError(LookupError):
    """A component the registry knows is not available in the running editor.

    Raised by ``EntityBuilder`` before it creates anything, so the build
    leaves no entity behind; ``with_undo_batch`` reports it as ``not_found``.
    """


# A component type id as O3DE prints it, with or without the braces.
_TYPE_UUID_PATTERN = re.compile(
    r"^\{?([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12})\}?$"
)


def is_component_type_uuid(text: object) -> bool:
    """Whether ``text`` is a component type UUID rather than a name."""
    return isinstance(text, str) and _TYPE_UUID_PATTERN.match(text.strip()) is not None


def normalize_component_type_uuid(text: str) -> str:
    """The UUID in the form O3DE prints: braces and uppercase hex digits."""
    match = _TYPE_UUID_PATTERN.match(text.strip())
    if match is None:
        raise ValueError(f"not a component type UUID: {text!r}")
    return "{" + match.group(1).upper() + "}"

# Component catalog organized by category
# Each entry: canonical_name -> {category, aliases, description}
COMPONENT_CATALOG: Dict[str, Dict] = {
    # --- Core ---
    "Transform": {
        "category": "Core",
        "description": "Provides position, rotation, and scale for an entity. Every entity has one; it cannot be added.",
        "addable": False,
    },
    "Tag": {
        "category": "Core",
        "description": "Adds tags to an entity for identification and filtering.",
    },
    "Comment": {
        "category": "Core",
        "description": "Stores text comments on an entity for documentation.",
    },
    # --- Rendering ---
    "Mesh": {
        "category": "Rendering",
        "aliases": ["MeshComponent", "Atom Mesh"],
        "description": "Renders a 3D mesh.",
    },
    "Material": {
        "category": "Rendering",
        "aliases": ["MaterialComponent"],
        "description": "Assigns a material to a mesh.",
    },
    "Decal": {
        "category": "Rendering",
        "aliases": ["Decal (Atom)", "DecalComponent"],
        "description": "Projects a decal onto surfaces.",
    },
    # --- Lighting ---
    # Atom has one "Light" component for every local light; its "Light type"
    # property picks point, spot, capsule, quad or polygon. The pre-Atom names
    # stay as aliases; EntityBuilder.with_light sets the type.
    "Directional Light": {
        "category": "Lighting",
        "aliases": ["DirectionalLight"],
        "description": "Sun-like directional light source.",
    },
    "Light": {
        "category": "Lighting",
        "aliases": [
            "Point Light", "PointLight", "Spot Light", "SpotLight",
            "Area Light", "AreaLight", "Local Light",
        ],
        "description": (
            "Local light (point, spot, capsule, quad or polygon, chosen by its "
            "'Light type' property; EntityBuilder.with_light sets it)."
        ),
    },
    "Global Skylight (IBL)": {
        "category": "Lighting",
        "aliases": ["Skylight", "IBL"],
        "description": "Image-based global skylight.",
    },
    "HDRi Skybox": {
        "category": "Lighting",
        "aliases": ["Skybox", "HDRISkybox"],
        "description": "HDR skybox rendering.",
    },
    # --- Physics ---
    # Canonical names match the editor display names on stabilization/26050.
    # Pre-rename names (e.g. "PhysX Collider", "PhysX Rigid Body") are kept as
    # aliases so existing scripts keep resolving.
    "PhysX Primitive Collider": {
        "category": "Physics",
        "aliases": ["PhysX Collider", "Collider", "PhysXCollider", "PrimitiveCollider"],
        "description": "Adds collision detection using primitive shapes (box, sphere, capsule).",
    },
    "PhysX Mesh Collider": {
        "category": "Physics",
        "aliases": ["MeshCollider", "PhysXMeshCollider"],
        "description": "Adds collision detection using a mesh asset.",
    },
    "PhysX Dynamic Rigid Body": {
        "category": "Physics",
        "aliases": ["RigidBody", "PhysXRigidBody", "PhysX Rigid Body"],
        "description": "Adds physics simulation (dynamic rigid body).",
    },
    "PhysX Static Rigid Body": {
        "category": "Physics",
        "aliases": ["StaticRigidBody", "PhysXStaticRigidBody"],
        "description": "Adds a static (non-moving) physics body.",
    },
    "PhysX Character Controller": {
        "category": "Physics",
        "aliases": ["CharacterController"],
        "description": "Physics-based character controller.",
    },
    "PhysX Force Region": {
        "category": "Physics",
        "aliases": ["ForceRegion"],
        "description": "Applies forces to entities within a region.",
    },
    # --- Scripting ---
    "Lua Script": {
        "category": "Scripting",
        "aliases": ["LuaScript", "Script"],
        "description": "Attaches a Lua script to an entity.",
    },
    "Script Canvas": {
        "category": "Scripting",
        "aliases": ["ScriptCanvas", "VisualScript"],
        "description": "Attaches a Script Canvas visual script.",
    },
    # --- Gameplay ---
    "Input": {
        "category": "Gameplay",
        "aliases": ["InputConfiguration", "InputComponent"],
        "description": "Binds physical inputs to named events via an .inputbindings asset.",
    },
    # --- Camera ---
    "Camera": {
        "category": "Camera",
        "aliases": ["CameraComponent"],
        "description": "Adds a camera to an entity.",
    },
    # --- Audio ---
    "Audio Trigger": {
        "category": "Audio",
        "aliases": ["AudioTrigger"],
        "description": "Triggers audio events.",
    },
    "Audio Proxy": {
        "category": "Audio",
        "aliases": ["AudioProxy"],
        "description": "Audio proxy for sound positioning.",
    },
    # --- Shapes ---
    "Box Shape": {
        "category": "Shapes",
        "aliases": ["BoxShape"],
        "description": "Defines a box-shaped volume.",
    },
    "Sphere Shape": {
        "category": "Shapes",
        "aliases": ["SphereShape"],
        "description": "Defines a sphere-shaped volume.",
    },
    "Capsule Shape": {
        "category": "Shapes",
        "aliases": ["CapsuleShape"],
        "description": "Defines a capsule-shaped volume.",
    },
    "Cylinder Shape": {
        "category": "Shapes",
        "aliases": ["CylinderShape"],
        "description": "Defines a cylinder-shaped volume.",
    },
    # --- UI ---
    "UI Canvas Asset Ref": {
        "category": "UI",
        "aliases": ["UICanvas", "UiCanvasAssetRef"],
        "description": "References a UI canvas asset.",
    },
    # --- Terrain ---
    "Axis Aligned Box Shape": {
        "category": "Shapes",
        "aliases": ["AABBShape"],
        "description": "Axis-aligned bounding box shape.",
    },
    # --- Networking ---
    "Network Binding": {
        "category": "Networking",
        "aliases": ["NetBinding"],
        "description": "Enables network replication for an entity.",
    },
}

# Build a lookup table for fast name resolution (canonical + aliases)
_LOOKUP: Dict[str, str] = {}
for canonical, info in COMPONENT_CATALOG.items():
    _LOOKUP[canonical.lower()] = canonical
    for alias in info.get("aliases", []):
        _LOOKUP[alias.lower()] = canonical


def resolve_component(name: str) -> Tuple[Optional[str], Optional[str]]:
    """Resolve a component name to its canonical form.

    Returns (canonical_name, None) on success, or (None, suggestion_message) on failure.
    """
    if not name:
        return None, "Component name cannot be empty"

    # Exact match (case-insensitive)
    lower = name.lower()
    if lower in _LOOKUP:
        return _LOOKUP[lower], None

    # Fuzzy match: find closest by substring
    candidates = []
    for key, canonical in _LOOKUP.items():
        if lower in key or key in lower:
            candidates.append(canonical)

    if candidates:
        # Remove duplicates while preserving order
        seen = set()
        unique = []
        for c in candidates:
            if c not in seen:
                seen.add(c)
                unique.append(c)
        suggestions = ", ".join(unique[:5])
        return None, f"Unknown component '{name}'. Did you mean: {suggestions}?"

    return None, f"Unknown component '{name}'. Use get_component_catalog() to see available components."


def is_addable(canonical: str) -> bool:
    """Whether a catalog component can be added to an entity.

    ``Transform`` is on every entity and absent from the editor's Add
    Component menu, so asking for it is a mistake worth refusing up front.
    """
    return COMPONENT_CATALOG.get(canonical, {}).get("addable", True)


def list_components(category: Optional[str] = None) -> List[Dict]:
    """List all known components, optionally filtered by category."""
    results = []
    for canonical, info in COMPONENT_CATALOG.items():
        if category and info["category"].lower() != category.lower():
            continue
        results.append({
            "name": canonical,
            "category": info["category"],
            "description": info.get("description", ""),
        })
    return results


def get_categories() -> List[str]:
    """Get all component categories."""
    categories = set()
    for info in COMPONENT_CATALOG.values():
        categories.add(info["category"])
    return sorted(categories)
