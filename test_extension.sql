-- Test script for erlang_cnode extension
-- Run this after installing the extension to verify it works

-- First, create the extension
CREATE EXTENSION IF NOT EXISTS erlang_cnode;

-- Test 1: Check if functions are available
SELECT 
    proname, 
    proargtypes::regtype[] as arg_types, 
    prorettype::regtype as return_type
FROM pg_proc 
WHERE proname IN ('erlang_connect', 'erlang_call', 'erlang_disconnect')
ORDER BY proname;

-- Test 2: Check extension information
SELECT 
    extname, 
    extversion, 
    extrelocatable 
FROM pg_extension 
WHERE extname = 'erlang_cnode';

-- Test 3: Verify function signatures
\df+ erlang_connect
\df+ erlang_call  
\df+ erlang_disconnect

-- Note: To test actual Erlang connectivity, you need to:
-- 1. Start an Erlang node: erl -name node1@localhost -setcookie cookie123
-- 2. Run the following commands:
/*
-- Connect to Erlang node
SELECT erlang_connect('node1@localhost', 'cookie123');

-- Call a simple function (if available on the Erlang node)
SELECT erlang_call('node1@localhost', 'erlang', 'node', '[]'::jsonb);

-- Disconnect
SELECT erlang_disconnect('node1@localhost');
*/ 