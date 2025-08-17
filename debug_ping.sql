-- Debug ping test
DROP EXTENSION IF EXISTS erlang_cnode CASCADE;
CREATE EXTENSION erlang_cnode;

-- Connect
SELECT erlang_connect('testnode@127.0.1.1', 'cookie123') as connected;

-- Test what erlang:is_alive() actually returns
\echo 'Direct call to erlang:is_alive():'
SELECT erlang_call('testnode@127.0.1.1', 'erlang', 'is_alive', '[]'::jsonb, 2000) as is_alive_result;

-- Test the ping function
\echo 'Ping function result:'
SELECT erlang_ping('testnode@127.0.1.1') as ping_result;

-- Cleanup
SELECT erlang_disconnect('testnode@127.0.1.1');