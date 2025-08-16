CREATE FUNCTION erlang_connect(node_name text, cookie text) RETURNS boolean
AS 'MODULE_PATHNAME', 'erlang_connect'
LANGUAGE C STRICT;

-- Original function signature for backward compatibility
CREATE FUNCTION erlang_call(node_name text, module text, function text, args jsonb) RETURNS jsonb
AS 'MODULE_PATHNAME', 'erlang_call'
LANGUAGE C STRICT;

-- Overloaded function with timeout parameter (in milliseconds, max 30000ms/30s)
CREATE FUNCTION erlang_call(node_name text, module text, function text, args jsonb, timeout_ms integer DEFAULT 5000) RETURNS jsonb
AS 'MODULE_PATHNAME', 'erlang_call_with_timeout'
LANGUAGE C STRICT;

-- Test basic connectivity without RPC
CREATE FUNCTION erlang_ping(node_name text) RETURNS text
AS 'MODULE_PATHNAME', 'erlang_ping'
LANGUAGE C STRICT;

CREATE FUNCTION erlang_disconnect(node_name text) RETURNS boolean
AS 'MODULE_PATHNAME', 'erlang_disconnect'
LANGUAGE C STRICT;

-- Async RPC functions
CREATE FUNCTION erlang_send_async(node_name text, module text, function text, args jsonb) RETURNS bigint
AS 'MODULE_PATHNAME', 'erlang_send_async'
LANGUAGE C STRICT;

CREATE FUNCTION erlang_receive_async(request_id bigint, timeout_ms integer DEFAULT 0) RETURNS jsonb
AS 'MODULE_PATHNAME', 'erlang_receive_async'
LANGUAGE C STRICT;

CREATE FUNCTION erlang_cast(node_name text, module text, function text, args jsonb) RETURNS boolean
AS 'MODULE_PATHNAME', 'erlang_cast'
LANGUAGE C STRICT;

CREATE FUNCTION erlang_check_connection(node_name text) RETURNS boolean
AS 'MODULE_PATHNAME', 'erlang_check_connection'
LANGUAGE C STRICT;

CREATE FUNCTION erlang_pending_requests() RETURNS integer
AS 'MODULE_PATHNAME', 'erlang_pending_requests'
LANGUAGE C STRICT; 