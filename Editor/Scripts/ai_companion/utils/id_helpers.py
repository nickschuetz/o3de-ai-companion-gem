# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Helpers for normalizing O3DE entity/component IDs into JSON-safe values.

On O3DE 2310+, EntityId and EntityComponentIdPair values come back as
`azlmbr.object.PythonProxyObject` wrappers rather than Python ints. Their
``str()`` form for types that do not implement ``__repr__`` includes a heap
address (``<Type via PythonProxyObject at 140262403519520>``), which makes
the output unstable across runs. We sanitize that here.
"""

import re
from typing import Any

_PROXY_REPR_RE = re.compile(r"<([A-Za-z_][\w:]*) via PythonProxyObject at \d+>")


def id_to_jsonable(value: Any) -> Any:
    """Convert an O3DE id-like value to something json.dumps can serialize.

    Falls through to int() when possible (older builds, plain ints), then to
    a sanitized str() for proxy wrappers. Returns None if the input is None.
    """
    if value is None:
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        pass
    return _PROXY_REPR_RE.sub(r"<\1>", str(value))


def entity_id_from_value(value: Any) -> Any:
    """An ``azlmbr.entity.EntityId`` proxy for ``value``, or ``None``.

    ``value`` may be an EntityId proxy (returned as is), an int, a decimal
    string or the bracketed ``"[id]"`` form. Observed on O3DE 26.10.0: the
    Python ``EntityId(int)`` constructor ignores its argument and yields the
    invalid id for every value, and a raw int handed to a bus call such as
    ``TransformBus.SetParent`` is marshalled into a wrong id without an error.
    So the constructor result is trusted only when it round-trips to the same
    id text; otherwise the entity is found by scanning ``SearchBus``
    (``SearchEntities`` with an empty filter) for the matching id text.
    Returns ``None`` when ``azlmbr`` is not importable, the value does not
    parse, or no entity with that id exists.
    """
    try:
        import azlmbr.bus as bus
        import azlmbr.entity as entity_api
    except ImportError:
        return None
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        wanted = value
    elif isinstance(value, str):
        digits = "".join(ch for ch in value if ch.isdigit())
        if not digits:
            return None
        wanted = int(digits)
    else:
        return value  # already a proxy (or something the caller vouches for)
    wanted_text = str(wanted)
    try:
        candidate = entity_api.EntityId(wanted)
        if candidate is not None and str(candidate.ToString()).strip("[]") == wanted_text:
            return candidate
    except (AttributeError, TypeError, RuntimeError):
        pass
    try:
        found = entity_api.SearchBus(bus.Broadcast, "SearchEntities", entity_api.SearchFilter())
    except (AttributeError, TypeError, RuntimeError):
        return None
    for candidate in found or []:
        try:
            if str(candidate.ToString()).strip("[]") == wanted_text:
                return candidate
        except (AttributeError, TypeError, RuntimeError):
            continue
    return None
