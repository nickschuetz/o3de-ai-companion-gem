# Safety Model

AI Companion implements defense-in-depth security for AI-driven operations.

## Threat Model

When an AI agent creates game content, the primary risks are:

1. **Injection** — Malicious strings in entity names or script paths executing unintended code
2. **Resource exhaustion** — Creating thousands of entities in a single call
3. **Data loss** — Accidentally deleting important entities or overwriting levels
4. **Path traversal** — Accessing files outside the project directory

## Input Validation

All user-supplied strings are validated before reaching `azlmbr` APIs.

### Entity Names
- Pattern: `^[A-Za-z][A-Za-z0-9_-]*$`
- Max length: 128 characters
- Must start with a letter

### Component Types
- Validated against the component registry whitelist
- Fuzzy matching suggests corrections for typos
- Pattern: `^[A-Za-z][A-Za-z0-9 _\-()\[\]]*$`

### Positions
- Must be finite (no NaN, no Infinity)
- Within bounds: +-10,000 on each axis
- Must be a 3-element list/tuple of numbers

### Asset Paths
- Must be relative (no leading `/` or `C:\`)
- No path traversal (`..`)
- No null bytes
- Max length: 1024 characters

### Implementation

Validation is implemented in two layers:
- **Python** (`ai_companion/safety/validators.py`) — Used by all Python API functions
- **C++** (`Code/Source/Validation/InputValidator.cpp`) — Used by EBus handlers

## Operation Sandboxing

Each API call runs within an `OperationSandbox` that enforces:

| Limit | Default | Purpose |
|-------|---------|---------|
| Max entities per call | 100 | Prevent scene explosion |
| Max recursion depth | 10 | Prevent infinite nesting |
| Operation timeout | 30 seconds | Prevent hangs |

These limits are configurable via the `OperationSandbox` constructor.

## System Entity Protection

The following entities cannot be deleted or modified:
- `EditorGlobal`
- `SystemEntity`
- `AZ::SystemEntity`
- Any entity with a name starting with `AZ::`

## Undo/Rollback

Every mutating API function is wrapped with `@with_undo_batch`:

1. An undo batch is started before the operation
2. All entity/component operations execute within the batch
3. If any operation fails:
   - The batch is ended
   - The entire batch is undone (rolled back)
   - An error response is returned with `"rolled_back": true`
4. On success, the batch is committed normally

Manual control is also available:

```python
begin_undo_batch("My Operation")
# ... operations ...
end_undo_batch()

# Or roll back
rollback_last_batch()
```

## No Code Generation

The Python API uses **parameterized function calls**, not code generation.
User-supplied strings are never interpolated into Python code. This eliminates
the primary injection vector in `run_editor_python()` workflows.

The only code that runs is pre-written, auditable Python in the
`ai_companion` package.

## Network Security

AI Companion includes the **AgentServer**, a purpose-built TCP listener for
AI agent communication (default port 4600). It replaces the former
RemoteConsole dependency with a length-prefixed JSON protocol designed
specifically for agent workflows.

### Bind Address

The AgentServer binds to `127.0.0.1` (localhost) by default. When configured
to bind to a non-loopback address, the server emits a warning at startup with
security guidance: enable TLS, use a firewall, restrict to trusted networks,
consider SSH tunneling, and enable secure mode.

### Secure Mode

When secure mode is enabled (`AI_COMPANION_SECURE_MODE=1`), the AgentServer
disables `execute_python` — the most powerful request type — and only allows
safe, read-only operations via C++ EBus:

- `ping` — connection health check
- `get_api_version` — protocol and gem version info
- `get_scene_snapshot` — full scene state
- `get_entity_tree` — entity hierarchy
- `validate_scene` — scene validation

This limits the attack surface when the server is exposed beyond localhost.

### TLS Encryption

Optional TLS encryption is available for non-localhost connections. When
enabled, all communication is encrypted using OpenSSL (TLS 1.2 minimum).
Self-signed certificates are supported for local development. See the
[integration guide](integration-with-o3de-mcp.md) for configuration details.

### Single Client

The AgentServer accepts only one connection at a time, preventing concurrent
mutation conflicts from multiple agents.

### Message Size Limits

Incoming messages are limited to 16 MiB to prevent memory exhaustion attacks.
