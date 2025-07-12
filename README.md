# PostgreSQL Erlang C-Node Extension

A PostgreSQL extension that acts as an Erlang C-Node using the `ei` library, allowing SQL functions to connect to Erlang/Elixir nodes, execute remote functions, and manage connections.

## Features

- **Connect to Erlang nodes**: `erlang_connect(node_name, cookie)`
- **Execute remote functions**: `erlang_call(node_name, module, function, args)`
- **Disconnect from nodes**: `erlang_disconnect(node_name)`
- **Robust memory management**: Uses PostgreSQL memory contexts and proper `ei` library cleanup
- **Connection pooling**: Stores connections in a hash table for reuse

## Dependencies

- PostgreSQL development libraries (`postgresql-server-dev-all`)
- Erlang erl_interface library (`liberlinterface-dev`)

### Installation on Ubuntu/Debian

```bash
sudo apt-get install postgresql-server-dev-all liberlinterface-dev
```

### Installation on macOS

```bash
brew install postgresql erlang
```

### Installation on Fedora/RHEL

```bash
sudo dnf install postgresql-server-devel erlang-erlang-interface
```

## Building and Installation

1. **Clone and build the extension**:
   ```bash
   make
   sudo make install
   ```

2. **Enable the extension in PostgreSQL**:
   ```sql
   CREATE EXTENSION erlang_cnode;
   ```

## Usage Examples

### Basic Connection and Function Call

```sql
-- Connect to an Erlang node
SELECT erlang_connect('node1@host', 'cookie123');

-- Call a remote function (e.g., lists:reverse([1, 2, 3]))
SELECT erlang_call('node1@host', 'lists', 'reverse', '[{"type": "list", "value": [1, 2, 3]}]'::jsonb);

-- Disconnect
SELECT erlang_disconnect('node1@host');
```

### Setting up an Erlang Node for Testing

1. **Start an Erlang node**:
   ```bash
   erl -name node1@host -setcookie cookie123
   ```

2. **In the Erlang shell, define a test function**:
   ```erlang
   -module(test).
   -export([hello/0, add/2]).
   
   hello() -> "Hello from Erlang!".
   add(A, B) -> A + B.
   ```

3. **Test from PostgreSQL**:
   ```sql
   -- Connect
   SELECT erlang_connect('node1@host', 'cookie123');
   
   -- Call hello function
   SELECT erlang_call('node1@host', 'test', 'hello', '[]'::jsonb);
   
   -- Call add function
   SELECT erlang_call('node1@host', 'test', 'add', '[{"type": "number", "value": 5}, {"type": "number", "value": 3}]'::jsonb);
   ```

## API Reference

### `erlang_connect(node_name text, cookie text) RETURNS boolean`

Connects to an Erlang node with the specified name and cookie.

- `node_name`: The Erlang node name (e.g., 'node1@host')
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

## Memory Management

The extension implements robust memory management to prevent leaks:

1. **PostgreSQL Memory Contexts**: Uses temporary memory contexts for each function call
2. **ei Library Cleanup**: Explicitly frees `ei_x_buff` and other ei-allocated resources
3. **Connection State**: Stores connections in a hash table with proper cleanup
4. **Error Handling**: Ensures cleanup before throwing errors

## Configuration

### Erlang Interface Paths

The Makefile includes default paths for different systems. You can override them:

```bash
# For custom Erlang installation
make ERL_INTERFACE_INCLUDE_DIR=/path/to/include ERL_INTERFACE_LIB_DIR=/path/to/lib
```

### Common Paths

- **Ubuntu/Debian**: `/usr/lib/erlang/usr/include` and `/usr/lib/erlang/usr/lib`
- **macOS (Homebrew)**: `/usr/local/lib/erlang/usr/include` and `/usr/local/lib/erlang/usr/lib`
- **Fedora/RHEL**: `/usr/lib64/erlang/usr/include` and `/usr/lib64/erlang/usr/lib`

## Troubleshooting

### Build Issues

1. **Missing Erlang headers**: Ensure `liberlinterface-dev` is installed
2. **Wrong paths**: Check and set correct `ERL_INTERFACE_INCLUDE_DIR` and `ERL_INTERFACE_LIB_DIR`
3. **PostgreSQL version**: Ensure PostgreSQL development headers match your PostgreSQL version

### Runtime Issues

1. **Connection failures**: Verify Erlang node is running and accessible
2. **Cookie mismatch**: Ensure the cookie matches between PostgreSQL and Erlang
3. **Permission issues**: Check network connectivity and firewall settings

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

## Future Enhancements

- Full JSONB-to-Erlang term conversion
- Connection pooling and timeout management
- Enhanced error handling and recovery
- Security improvements and input validation
- Performance optimizations for high concurrency

## License

This extension is provided as-is for educational and development purposes. 