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
