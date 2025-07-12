CREATE FUNCTION erlang_connect(node_name text, cookie text) RETURNS boolean
AS 'MODULE_PATHNAME', 'erlang_connect'
LANGUAGE C STRICT;

CREATE FUNCTION erlang_call(node_name text, module text, function text, args jsonb) RETURNS jsonb
AS 'MODULE_PATHNAME', 'erlang_call'
LANGUAGE C STRICT;

CREATE FUNCTION erlang_disconnect(node_name text) RETURNS boolean
AS 'MODULE_PATHNAME', 'erlang_disconnect'
LANGUAGE C STRICT; 