# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Component name registry for O3DE. Maps human-readable names to component
metadata and provides fuzzy matching to prevent mistyped component names."""

from typing import Dict, List, Optional, Tuple

# Component catalog organized by category
# Each entry: canonical_name -> {category, aliases, description}
COMPONENT_CATALOG: Dict[str, Dict] = {
    # --- Core ---
    "Transform": {
        "category": "Core",
        "description": "Provides position, rotation, and scale for an entity.",
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
    "Decal (Atom)": {
        "category": "Rendering",
        "description": "Projects a decal onto surfaces.",
    },
    # --- Lighting ---
    "Directional Light": {
        "category": "Lighting",
        "aliases": ["DirectionalLight"],
        "description": "Sun-like directional light source.",
    },
    "Point Light": {
        "category": "Lighting",
        "aliases": ["PointLight"],
        "description": "Omnidirectional point light source.",
    },
    "Spot Light": {
        "category": "Lighting",
        "aliases": ["SpotLight"],
        "description": "Focused cone-shaped light source.",
    },
    "Area Light": {
        "category": "Lighting",
        "aliases": ["AreaLight"],
        "description": "Area-based light source.",
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
    "PhysX Collider": {
        "category": "Physics",
        "aliases": ["Collider", "PhysXCollider"],
        "description": "Adds collision detection to an entity.",
    },
    "PhysX Rigid Body": {
        "category": "Physics",
        "aliases": ["RigidBody", "PhysXRigidBody", "PhysX Dynamic Rigid Body"],
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
