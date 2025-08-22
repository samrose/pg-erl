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
5. **Synchronous Operations**: Current implementation blocks PostgreSQL backend during Erlang calls

## TODO: Asynchronous Implementation

The current implementation uses synchronous RPC calls that block PostgreSQL's event loop. For production use, the following changes are needed:

### High Priority

- [ ] **Replace synchronous `ei_rpc_to()` with non-blocking I/O**
  - Implement `ei_xreceive_msg()` with timeout for non-blocking message handling
  - Use `ei_send()` and `ei_receive()` instead of blocking RPC calls
  - Set socket file descriptors to non-blocking mode

- [ ] **Implement PostgreSQL Background Worker**
  - Create background worker to poll for Erlang messages at regular intervals
  - Handle message processing without blocking the main PostgreSQL process
  - Integrate with PostgreSQL's event loop using `WaitEventSet`

- [ ] **Add proper response handling**
  - Fix `erlang_term_to_jsonb()` to properly decode actual Erlang responses
  - Implement proper message parsing and error handling
  - Add timeout mechanisms for unresponsive Erlang nodes

### Medium Priority

- [ ] **Implement threading for Erlang communication**
  - Create dedicated POSIX thread for Erlang communication
  - Use shared memory queues or pipes for thread-safe message exchange
  - Ensure PostgreSQL API calls are only made from main thread

- [ ] **Add connection state management**
  - Implement proper connection pooling with async capabilities
  - Add connection health monitoring and automatic reconnection
  - Handle multiple concurrent connections efficiently

- [ ] **Integrate with async I/O libraries**
  - Consider libuv or libevent integration for robust event loops
  - Implement proper event-driven architecture
  - Add support for high-concurrency scenarios

### Low Priority

- [ ] **Enhanced error handling and recovery**
  - Add comprehensive error handling for network failures
  - Implement circuit breaker patterns for failing Erlang nodes
  - Add logging and monitoring capabilities

- [ ] **Security improvements**
  - Add message validation to prevent injection attacks
  - Implement proper authentication and authorization
  - Add rate limiting and resource protection

- [ ] **Performance optimizations**
  - Implement message batching for multiple calls
  - Add connection pooling optimizations
  - Consider caching mechanisms for frequently called functions

## Comprehensive Improvement Roadmap

Based on a thorough code analysis, the following improvements should be implemented to make this extension production-ready:

### Critical Issues - Must Fix Before Production

#### 1. Debug/Notice Statement Cleanup
- [ ] Remove all `ereport(NOTICE,...)` debug statements from production code (erlang_cnode.c:76-135, 236, 269, etc.)
- [ ] Implement conditional debug logging controlled by configuration parameter
- [ ] Remove sensitive information exposure (HOME environment, cookie paths)
- [ ] Add proper log levels (DEBUG1-5) for development vs production

#### 2. Memory Management & Resource Cleanup
- [ ] Fix memory leaks in error paths (erlang_cnode.c:256-265, 477-484, 491-497)
- [ ] Ensure all `ei_x_buff` structures are freed in all code paths
- [ ] Add `PG_TRY/PG_CATCH` blocks for proper cleanup on exceptions
- [ ] Review and fix all `palloc` calls without corresponding `pfree`
- [ ] Implement proper memory context management for long-lived connections

#### 3. Error Handling & Recovery
- [ ] Add comprehensive error checking for all ei library calls
- [ ] Implement automatic reconnection on connection failure
- [ ] Add connection retry logic with exponential backoff
- [ ] Properly handle all errno values from socket operations
- [ ] Add transaction-safe error recovery mechanisms
- [ ] Implement proper cleanup in signal handlers

#### 4. Security Vulnerabilities
- [ ] Add input validation for node names (prevent injection attacks)
- [ ] Validate cookie format and length
- [ ] Encrypt or secure cookie storage in memory
- [ ] Add SQL injection prevention for all user inputs
- [ ] Implement rate limiting to prevent DoS attacks
- [ ] Add connection attempt limits per session
- [ ] Remove environment variable exposure in logs
- [ ] Add authentication beyond simple cookie verification

### High Priority Improvements

#### 5. Connection Pool Management
- [ ] Add proper locking for multi-process/thread safety
- [ ] Implement connection pool size limits
- [ ] Add connection age and idle timeout tracking
- [ ] Implement health checks with automatic stale connection removal
- [ ] Add connection statistics and monitoring
- [ ] Implement proper connection lifecycle management
- [ ] Add support for connection pooling across backends

#### 6. Type Conversion Completeness
- [ ] Complete JSONB to Erlang term conversion (jsonb_erlang_converter.c:185-189)
- [ ] Implement proper special type string parsing
- [ ] Add support for Erlang references
- [ ] Add support for Erlang ports
- [ ] Improve binary data handling with proper encoding/decoding
- [ ] Add support for improper lists
- [ ] Implement big integer support
- [ ] Add comprehensive type conversion tests
- [ ] Handle UTF-8 and Latin-1 string encodings properly

#### 7. Async Implementation Fixes
- [ ] Add size limits to AsyncRequest hash table
- [ ] Implement request expiration and cleanup
- [ ] Add proper timeout handling for async requests
- [ ] Fix race conditions in request tracking
- [ ] Implement request cancellation
- [ ] Add async request statistics
- [ ] Clean up completed requests periodically
- [ ] Add maximum pending request limits

### Medium Priority Enhancements

#### 8. Performance Optimizations
- [ ] Replace synchronous blocking I/O with true async
- [ ] Implement connection pooling and reuse
- [ ] Optimize string operations and conversions
- [ ] Add caching for frequently called functions
- [ ] Reduce maximum timeout values to prevent backend blocking
- [ ] Implement batch operations for multiple calls
- [ ] Add prepared statement support for repeated calls
- [ ] Optimize JSONB parsing and generation

#### 9. Monitoring & Observability
- [ ] Add connection metrics (active, idle, failed)
- [ ] Implement call latency tracking
- [ ] Add error rate monitoring
- [ ] Create system views for connection status
- [ ] Add performance statistics collection
- [ ] Implement trace logging for debugging
- [ ] Add integration with PostgreSQL's statistics collector
- [ ] Create monitoring functions for health checks

#### 10. Testing Infrastructure
- [ ] Add comprehensive unit tests for all functions
- [ ] Implement integration tests with real Erlang nodes
- [ ] Add stress tests for connection pooling
- [ ] Create performance benchmarks
- [ ] Add memory leak detection tests
- [ ] Implement chaos testing for failure scenarios
- [ ] Add regression tests for bug fixes
- [ ] Create automated test suite in CI/CD

### Long-term Strategic Improvements

#### 11. Architecture Refactoring
- [ ] Implement proper background worker for async operations
- [ ] Add support for multiple background workers
- [ ] Create proper abstraction layers
- [ ] Implement plugin architecture for extensions
- [ ] Add support for custom protocols
- [ ] Consider using libpq for connection management
- [ ] Implement proper state machine for connections

#### 12. Feature Enhancements
- [ ] Add support for Erlang node discovery
- [ ] Implement distributed transaction support
- [ ] Add support for Erlang term storage in tables
- [ ] Create foreign data wrapper interface
- [ ] Add support for streaming results
- [ ] Implement publish/subscribe mechanisms
- [ ] Add support for OTP gen_server calls
- [ ] Create stored procedure support in Erlang

#### 13. Documentation & Usability
- [ ] Add comprehensive inline code documentation
- [ ] Create detailed API documentation
- [ ] Add architecture diagrams
- [ ] Write performance tuning guide
- [ ] Create troubleshooting guide
- [ ] Add migration guide from other solutions
- [ ] Create example applications
- [ ] Add best practices documentation

#### 14. Compatibility & Portability
- [ ] Test and support multiple PostgreSQL versions (12-16)
- [ ] Add support for different Erlang/OTP versions
- [ ] Test on multiple operating systems
- [ ] Add ARM architecture support
- [ ] Ensure compatibility with PostgreSQL extensions
- [ ] Add support for connection through proxies
- [ ] Test with various network configurations

#### 15. Production Readiness
- [ ] Add circuit breaker pattern implementation
- [ ] Implement graceful degradation
- [ ] Add connection warmup procedures
- [ ] Create operational runbooks
- [ ] Add deployment automation scripts
- [ ] Implement zero-downtime upgrade procedures
- [ ] Add backup and recovery procedures
- [ ] Create disaster recovery plans

### Code Quality Improvements

#### 16. Code Structure & Maintainability
- [ ] Split large functions into smaller, testable units
- [ ] Remove code duplication
- [ ] Improve variable naming consistency
- [ ] Add proper error code definitions
- [ ] Create consistent coding style
- [ ] Add static analysis tools integration
- [ ] Implement code coverage tracking
- [ ] Add continuous integration pipelines

#### 17. Build System & Dependencies
- [ ] Review and minimize dependencies
- [ ] Add dependency version management
- [ ] Improve build system configuration
- [ ] Add cross-compilation support
- [ ] Create reproducible builds
- [ ] Add package management support (PGXN)
- [ ] Implement automated release process

## License

This extension is licensed under the PostgreSQL License. See the LICENSE file for details.

This extension is provided as-is for educational and development purposes. 