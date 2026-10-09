# Safety Model

AI Companion implements defense-in-depth security for AI-driven operations.

## Threat Model

When an AI agent creates game content, the primary risks are:

1. **Injection**: Malicious strings in entity names or script paths executing unintended code
2. **Resource exhaustion**: Creating thousands of entities in a single call
3. **Data loss**: Accidentally deleting important entities or overwriting levels
4. **Path traversal**: Accessing files outside the project directory

## Input Validation

All user-supplied strings are validated before reaching `azlmbr` APIs.

### Entity Names
- Pattern: `^[A-Za-z][A-Za-z0-9_-]*$`
- Max length: 128 characters
- Must start with a letter

### Component Types
- `EntityBuilder.with_component` validates the characters first, pattern
  `^[A-Za-z][A-Za-z0-9 _\-()\[\]]*$`, so a name carrying shell or Python syntax
  is refused outright
- It then resolves the name against the component registry (case-insensitive,
  aliases accepted) and refuses an unknown one, suggesting the closest known
  names
- The typed builder methods (`with_mesh`, `with_physics`, `with_collider`,
  `with_lua_script`, ...) use fixed registry names and never take a type

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
- **Python** (`ai_companion/safety/validators.py`): Used by all Python API functions
- **C++** (`Code/Source/Validation/InputValidator.cpp`): Used by EBus handlers

## Operation Sandboxing

Each API call runs within an `OperationSandbox` that enforces:

| Limit | Default | Purpose |
|-------|---------|---------|
| Max entities per call (`MAX_ENTITIES_PER_CALL`) | 100 | Prevent scene explosion |
| Operation timeout (`MAX_OPERATION_TIMEOUT_SECONDS`) | 30 seconds | Prevent hangs |

A call is one outermost undo batch: `with_undo_batch` resets the sandbox when
it opens the batch, so nested API calls share the count and the clock. Both
limits are constructor arguments of `OperationSandbox`. The sandbox also
records every entity the call creates, which is what rollback deletes if the
editor's Undo leaves one behind.

## System Entity Protection

The protected names are `EditorGlobal`, `SystemEntity` and any name starting
with `AZ::` (which covers `AZ::SystemEntity`). Both layers enforce them:

- The native `set_transform` and `delete_entity` request types look the entity's
  name up and refuse a protected one with `validation_failed` and the message
  `entity is protected: '<name>'`, before any undo batch opens
  (`InputValidator::IsProtectedEntityName`).
- The Python package never deletes or edits an existing entity by id; its one
  mutation that names an existing entity is parenting a new entity under it
  (`EntityBuilder.with_parent`, reached by `create_projectile_spawner`'s
  `parent_entity_id`). `validate_target_entity` resolves the parent's name
  through `EditorEntityInfoRequestBus` and refuses a protected one with the same
  message, before the new entity is created. Any new id-taking mutation must
  call it.

## Undo/Rollback

Every mutating API function is wrapped with `@with_undo_batch`:

1. An undo batch is started before the operation
2. All entity/component operations execute within the batch
3. If any operation fails:
   - The batch is ended
   - If the editor kept the batch it is undone (an empty batch is discarded by
     the editor, and an Undo then would land on the previous operation), and
     any entity the batch created that the undo left behind is deleted
   - The caller receives a JSON error with `rolled_back: true`, a `code` and
     `details.rolled_back` (whether an Undo step ran) instead of a traceback
4. On success, the batch is committed normally

The validators run inside the batch: `with_undo_batch` opens it and resets the
sandbox first, then the function validates and acts, so a refused argument ends
as an empty, discarded batch.

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
disables `execute_python`, the most powerful request type, and only allows
operations served by the gem's own C++, which are read-only except for the
validated mutation set and the validated anim graph authoring set:

- `ping`: connection health check
- `get_api_version`: protocol and gem version info
- `get_scene_snapshot`: full scene state
- `get_entity_tree`: entity hierarchy
- `get_entity`: one entity's transform, parent and components
- `validate_scene`: scene validation
- `get_bus_schema`: reflected EBus description from the live BehaviorContext
- `list_anim_graphs`, `get_anim_graph`: read-only views of the EMotion FX anim
  graphs the engine holds
- `create_anim_graph`, `remove_anim_graph`, `load_anim_graph`,
  `save_anim_graph`, `add_anim_graph_node`, `remove_anim_graph_node`,
  `set_anim_graph_entry_state`, `add_anim_graph_parameter`,
  `remove_anim_graph_parameter`, `add_anim_graph_transition`,
  `remove_anim_graph_transition`, `set_anim_graph_transition`,
  `connect_anim_graph_ports`, `disconnect_anim_graph_ports`,
  `set_anim_graph_node`: anim graph authoring through EMotion Studio's
  command system; names, types, placement, values, condition attributes,
  ports, node fields and paths are validated in C++ before a command is
  sent, paths must stay inside the project (or, for a load, the engine)
  root, and a graph an asset or runtime instance owns is refused
- `create_entity`, `set_transform`, `delete_entity`: the validated mutation set:
  arguments go through the C++ `InputValidator`, missing entities, the level
  root and the protected system entities are refused, and each call is its own
  editor undo batch

An `execute_python` request in secure mode is answered with `status`
`error`, the code `secure_mode` and a message that lists the request types the
server does serve; it is never dispatched to the main thread. Every other
refusal the native types make carries its own code (`validation_failed` for a
refused argument or the level root, `not_found`, `unavailable`,
`engine_error`), defined in the
[integration guide](integration-with-o3de-mcp.md#response-format).

This limits the attack surface when the server is exposed beyond localhost.
o3de-mcp's `get_capabilities`, `get_scene_snapshot`, `get_entity_tree`,
`get_entity`, `validate_scene` and `get_bus_schema_live` tools use only these
request types, its `create_entity`, `set_transform` and `delete_entity` tools
try the native mutations first, and its seventeen anim graph tools wrap the
anim graph types one to one, so all of them keep working in secure mode;
`run_editor_python`, the session tools and the `ai_companion` Python API do
not.

### TLS Encryption

Optional TLS encryption is available for non-localhost connections. When
enabled, all communication is encrypted using OpenSSL (TLS 1.2 minimum,
cipher suite restricted to `HIGH:!aNULL:!MD5:!RC4`). Self-signed certificates
are supported for local development. See the
[integration guide](integration-with-o3de-mcp.md) for configuration details.

### Request ID Sanitization

Request IDs received from clients are sanitized to alphanumeric characters,
hyphens, and underscores only (max 64 characters) in the connection handler, and
are used for audit logging. Note that the `execute_python` result file is not
guarded by this sanitization: `execute_python` runs arbitrary Python by design,
so the result path is protected instead by exclusive file creation (`O_EXCL`, see
Temp File Security) and by disabling `execute_python` in secure mode.

### Script Encoding

Python scripts submitted via `execute_python` are base64-decoded and then
re-encoded as hex-escaped byte literals (`b'\x48\x65\x6c...'`) before being
passed to `exec()`. This encoding is injection-proof: no character in the user
script can break out of the byte string literal.

### Temp File Security

Result files from `execute_python` are created with `O_EXCL` (exclusive create)
and permissions `0600` (owner read/write only). Any pre-existing file at the
result path is removed before creation to mitigate symlink attacks. Files are
cleaned up immediately after reading, including on error paths.

### Single Client

The AgentServer accepts only one connection at a time, preventing concurrent
mutation conflicts from multiple agents. Stale connections (CLOSE-WAIT state)
are detected via non-blocking socket probing and automatically cleaned up so
new clients can connect.

### Process Isolation

The AgentServer listen socket uses `SOCK_CLOEXEC` (Linux), `FD_CLOEXEC` (macOS),
or `SetHandleInformation` (Windows) to prevent child processes (AssetProcessor,
AssetBuilder) from inheriting the socket. This ensures only the Editor process
accepts connections and processes requests with proper main-thread dispatch.

### Message Size Limits

Incoming messages are limited to 16 MiB to prevent memory exhaustion attacks.

### Known Limitations

- **No authentication**: The AgentServer does not implement API keys, tokens,
  or client certificates. Security relies on binding to localhost (the default)
  and OS-level access control. Do not expose to untrusted networks without
  additional protection (SSH tunnel, VPN, firewall).
- **No Python sandbox**: `execute_python` runs arbitrary Python code with full
  editor privileges. It can access the filesystem, network, and all O3DE APIs.
  The `OperationSandbox` only limits entity creation counts and timeouts, not
  Python language features. Use secure mode to disable `execute_python` entirely
  in untrusted environments.
- **No rate limiting**: The server does not limit request frequency. A malicious
  client could flood the server with requests to degrade editor performance.
