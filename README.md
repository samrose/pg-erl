# PostgreSQL Erlang C-Node Extension

A PostgreSQL extension that acts as an Erlang C-Node using the `ei` library, allowing SQL functions to connect to Erlang/Elixir nodes, execute remote functions, and manage connections.

## Features

- **Connect to Erlang nodes**: `erlang_connect(node_name, cookie)`
- **Execute remote functions**: `erlang_call(node_name, module, function, args)`
- **Disconnect from nodes**: `erlang_disconnect(node_name)`
- **Robust memory management**: Uses PostgreSQL memory contexts and proper `ei` library cleanup
- **Connection pooling**: Stores connections in a hash table for reuse

## Development Environment

This project uses Nix for reproducible development environments.

### Prerequisites

- Nix with flakes enabled
- Linux environment (tested on Ubuntu)

### Getting Started

1. **Enter the development environment**:
   ```bash
   nix develop
   ```

2. **Build the extension**:
   ```bash
   nix build
   ```

3. **Start PostgreSQL with the extension**:
   ```bash
   reset-postgres
   start-postgres
   create-testdb
   ```

4. **Enable the extension**:
   ```bash
   psql testdb -c "CREATE EXTENSION erlang_cnode;"
   ```

## Usage Examples

### Basic Connection and Function Call

```sql
-- Connect to an Erlang node
SELECT erlang_connect('testnode@127.0.1.1', 'cookie123');

-- Call a remote function
SELECT erlang_call('testnode@127.0.1.1', 'erlang', 'node', '[]'::jsonb);

-- Disconnect
SELECT erlang_disconnect('testnode@127.0.1.1');
```

### Setting up an Erlang Node for Testing

1. **Start an Erlang node**:
   ```bash
   erl -name testnode@127.0.1.1 -setcookie cookie123
   ```

2. **Test from PostgreSQL**:
   ```sql
   -- Connect
   SELECT erlang_connect('testnode@127.0.1.1', 'cookie123');
   
   -- Call a function
   SELECT erlang_call('testnode@127.0.1.1', 'erlang', 'self', '[]'::jsonb);
   ```

## API Reference

### `erlang_connect(node_name text, cookie text) RETURNS boolean`

Connects to an Erlang node with the specified name and cookie.

- `node_name`: The Erlang node name (e.g., 'testnode@127.0.1.1')
- `cookie`: The Erlang cookie for authentication
- Returns: `true` on success, throws error on failure

### `erlang_call(node_name text, module text, function text, args jsonb) RETURNS jsonb`

Executes a remote function call on the specified Erlang node.

- `node_name`: The Erlang node name to call
- `module`: The Erlang module name
- `function`: The function name to call
- `args`: JSONB array of arguments to pass to the function
- Returns: JSONB result from the Erlang function

### `erlang_disconnect(node_name text) RETURNS boolean`

Disconnects from the specified Erlang node.

- `node_name`: The Erlang node name to disconnect from
- Returns: `true` if connection was found and closed, `false` if no connection existed

## Available Commands

The development environment provides several convenience commands:

- `start-postgres` - Initialize and start PostgreSQL
- `stop-postgres` - Stop PostgreSQL
- `create-testdb` - Create testdb database
- `reset-postgres` - Reset data directory and re-init PostgreSQL
- `setup-elixir` - Start an Elixir node for testing

## Troubleshooting

### Common Issues

1. **PostgreSQL won't start**: Check if port 5432 is available
2. **Connection refused**: Ensure Erlang node is running with correct hostname
3. **Cookie mismatch**: Verify both PostgreSQL and Erlang use the same cookie

### Debugging

Enable PostgreSQL logging to see detailed error messages:

```sql
SET log_statement = 'all';
SET log_min_messages = 'debug1';
```

## Limitations

1. **JSONB Conversion**: Current implementation uses placeholder logic for JSONB-to-Erlang term conversion
2. **Thread Safety**: Assumes backend-local connections; shared connections require additional locking
3. **Security**: Basic input validation; production use requires additional security measures
4. **Error Recovery**: Limited handling of Erlang node crashes or network failures

## License

This extension is licensed under the PostgreSQL License. See the LICENSE file for details.

This extension is provided as-is for educational and development purposes. 