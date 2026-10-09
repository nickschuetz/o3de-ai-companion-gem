# Copyright (c) Contributors to the Open 3D Engine Project.
# SPDX-License-Identifier: Apache-2.0 OR MIT

"""Minimal AgentServer client for the live tests.

Speaks the gem's length-prefixed JSON protocol directly (4-byte big-endian
length, then a UTF-8 JSON body) so the suite needs nothing beyond the
standard library. Each request opens a fresh connection: the AgentServer
serves one client at a time and this keeps the tests independent of each
other's connection state.
"""

from __future__ import annotations

import base64
import json
import os
import socket
import struct
import uuid
from typing import Any


class AgentClient:
    def __init__(self, host: str | None = None, port: int | None = None, timeout: float = 120.0) -> None:
        self.host = host or os.environ.get("O3DE_EDITOR_HOST", "127.0.0.1")
        self.port = int(port or os.environ.get("O3DE_EDITOR_PORT", "4600"))
        self.timeout = timeout

    def request(self, request_type: str, **fields: Any) -> dict[str, Any]:
        """Send one request and return the decoded response object."""
        body = {"id": uuid.uuid4().hex[:16], "type": request_type, **fields}
        payload = json.dumps(body).encode("utf-8")
        with socket.create_connection((self.host, self.port), timeout=self.timeout) as sock:
            sock.sendall(struct.pack(">I", len(payload)) + payload)
            header = self._read_exactly(sock, 4)
            (length,) = struct.unpack(">I", header)
            data = self._read_exactly(sock, length)
        response: dict[str, Any] = json.loads(data.decode("utf-8"))
        return response

    def execute_python(self, script: str) -> dict[str, Any]:
        encoded = base64.b64encode(script.encode("utf-8")).decode("ascii")
        return self.request("execute_python", script=encoded)

    def run(self, script: str) -> str:
        """Execute a script and return its printed output, raising on a protocol error."""
        response = self.execute_python(script)
        if response.get("status") != "ok":
            raise RuntimeError(f"execute_python failed ({response.get('code')}): {response.get('error')}")
        return str(response.get("output", ""))

    def api(self, expression: str, imports: str = "") -> Any:
        """Evaluate an ``ai_companion.api`` call and parse its JSON result.

        ``expression`` is a Python expression returning a JSON string, for example
        ``create_player("P", position=[0, 0, 1])``. ``imports`` lists the names
        to import from ``ai_companion.api``; it defaults to the leading
        identifier of the expression.
        """
        names = imports or expression.split("(", 1)[0].strip()
        script = f"from ai_companion.api import {names}\nprint({expression})\n"
        output = self.run(script).strip()
        last_line = output.splitlines()[-1] if output else ""
        return json.loads(last_line)

    @staticmethod
    def _read_exactly(sock: socket.socket, count: int) -> bytes:
        chunks = []
        remaining = count
        while remaining:
            chunk = sock.recv(remaining)
            if not chunk:
                raise ConnectionError("AgentServer closed the connection mid-frame")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)
