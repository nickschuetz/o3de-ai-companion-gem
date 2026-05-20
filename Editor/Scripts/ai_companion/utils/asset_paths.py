# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Mappings from human-readable primitive names to actual O3DE asset paths.

The AI Companion API accepts placeholder names like ``primitive_cube`` so
the agent doesn't need to know exact asset paths. This table resolves
those to product paths under the project cache (``Cache/<platform>/...``)
that ship with stock O3DE.

If a primitive's mapping is missing, callers fall back to leaving the
Mesh component without a Model Asset assigned (the entity exists but
won't render). That's the same behavior as before this table existed.
"""

from typing import Optional


PRIMITIVE_MESH_PATHS = {
    "primitive_sphere": "models/sphere.fbx.azmodel",
    "primitive_cube": "materialeditor/viewportmodels/cube.fbx.azmodel",
    "primitive_beveled_cube": "materialeditor/viewportmodels/beveledcube.fbx.azmodel",
    "primitive_cylinder": "materialeditor/viewportmodels/beveledcylinder.fbx.azmodel",
    "primitive_cone": "materialeditor/viewportmodels/cone.fbx.azmodel",
    # No upstream primitive_capsule asset; use sphere so the entity is at
    # least visible. The player entity ends up rendering as a sphere until
    # we ship a real capsule.
    "primitive_capsule": "models/sphere.fbx.azmodel",
    "primitive_groundplane": "objects/groudplane/groundplane_512x512m.fbx.azmodel",
}


def resolve_primitive_mesh(name: str) -> Optional[str]:
    """Resolve a primitive name like ``primitive_cube`` to a product path.

    Returns the product path (e.g. ``models/sphere.fbx.azmodel``) or None
    if the name is unknown. A name that already looks like an asset path
    (contains a ``/`` and ends in ``.azmodel``) is returned unchanged.
    """
    if not name:
        return None
    if name in PRIMITIVE_MESH_PATHS:
        return PRIMITIVE_MESH_PATHS[name]
    if "/" in name and name.endswith(".azmodel"):
        return name
    return None


def resolve_input_bindings(path: str) -> Optional[str]:
    """Resolve an ``.inputbindings`` source path to its product path.

    AssetProcessor copies ``.inputbindings`` files verbatim into the cache
    with lowercased path components (e.g. ``Assets/Input/foo.inputbindings``
    becomes ``input/foo.inputbindings``). The gem's ``Assets/`` is the asset
    root; anything beneath it is mirrored into the project cache.

    Returns None for input that does not look like a bindings path.
    """
    if not path:
        return None
    p = path.replace("\\", "/")
    if not p.lower().endswith(".inputbindings"):
        return None
    if p.lower().startswith("assets/"):
        p = p[len("assets/"):]
    return p.lower()


def resolve_lua_script(path: str) -> Optional[str]:
    """Resolve a Lua script source path to its product (.luac) path.

    AssetProcessor compiles ``Scripts/Lua/foo.lua`` to a lowercase
    ``scripts/lua/foo.luac`` in the project cache. Source paths in the
    gem are case-mixed (``Scripts/Lua/...``); we lower-case and swap
    extension to .luac to get the runtime asset path.

    Returns None for input that does not look like a Lua source path.
    """
    if not path:
        return None
    p = path.replace("\\", "/")
    if not p.lower().endswith((".lua", ".luac")):
        return None
    if p.lower().endswith(".lua"):
        p = p[:-4] + ".luac"
    return p.lower()
