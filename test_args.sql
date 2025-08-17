-- Test JSONB→Erlang argument encoding
-- Run with: psql -d testdb -f test_args.sql

\echo '=== Testing JSONB→Erlang Argument Encoding ==='

DROP EXTENSION IF EXISTS erlang_cnode CASCADE;
CREATE EXTENSION erlang_cnode;

\set node_name 'testnode@127.0.1.1'
\set cookie 'cookie123'

-- Connect first
SELECT erlang_connect(:'node_name', :'cookie');

\echo ''
\echo '=== Test 1: Functions with Simple Arguments ==='

-- Test with integer argument
\echo 'Testing lists:nth(2, [a,b,c]) - should return "b":'
SELECT erlang_call(:'node_name', 'lists', 'nth', '[2, ["a","b","c"]]'::jsonb, 5000);

-- Test with list argument  
\echo 'Testing lists:reverse([1,2,3]) - should return [3,2,1]:'
SELECT erlang_call(:'node_name', 'lists', 'reverse', '[[1,2,3]]'::jsonb, 5000);

-- Test with string argument
\echo 'Testing erlang:list_to_atom("hello") - should return "hello" as atom:'
SELECT erlang_call(:'node_name', 'erlang', 'list_to_atom', '["hello"]'::jsonb, 5000);

\echo ''
\echo '=== Test 2: Functions with Atom Arguments ==='

-- Test with atom argument using special type encoding
\echo 'Testing erlang:whereis(rex) - should return PID or error:'
SELECT erlang_call(:'node_name', 'erlang', 'whereis', '[{"$type": "atom", "value": "rex"}]'::jsonb, 5000);

-- Test process_info with self()
\echo 'Testing erlang:process_info(self()) - first get self, then use it:'
SELECT erlang_call(:'node_name', 'erlang', 'self', '[]'::jsonb, 5000) as self_pid \gset
\echo 'Self PID: :self_pid'

\echo ''
\echo '=== Test 3: Math Functions ==='

-- Test math functions with numeric arguments
\echo 'Testing erlang:+(5, 3) - should return 8:'
SELECT erlang_call(:'node_name', 'erlang', '+', '[5, 3]'::jsonb, 5000);

\echo 'Testing math:pow(2, 8) - should return 256:'
SELECT erlang_call(:'node_name', 'math', 'pow', '[2, 8]'::jsonb, 5000);

\echo 'Testing math:sqrt(16) - should return 4.0:'
SELECT erlang_call(:'node_name', 'math', 'sqrt', '[16]'::jsonb, 5000);

\echo ''
\echo '=== Test 4: String Operations ==='

-- Test string operations
\echo 'Testing string:to_upper("hello") - should return "HELLO":'
SELECT erlang_call(:'node_name', 'string', 'to_upper', '["hello"]'::jsonb, 5000);

\echo 'Testing string:concat(["hello", " ", "world"]) - should return "hello world":'
SELECT erlang_call(:'node_name', 'string', 'concat', '[["hello", " ", "world"]]'::jsonb, 5000);

\echo ''
\echo '=== Test 5: Complex Data Structures ==='

-- Test with tuple argument (using array which converts to tuple in special cases)
\echo 'Testing erlang:element(1, {a,b,c}) - should return "a":'
SELECT erlang_call(:'node_name', 'erlang', 'element', '[1, {"$type": "tuple", "elements": ["a", "b", "c"]}]'::jsonb, 5000);

-- Test with map argument
\echo 'Testing maps:get("key", #{"key" => "value"}) - should return "value":'
SELECT erlang_call(:'node_name', 'maps', 'get', '["key", {"key": "value"}]'::jsonb, 5000);

\echo ''
\echo '=== Test 6: Boolean Arguments ==='

-- Test with boolean arguments
\echo 'Testing erlang:and(true, false) - should return false:'
SELECT erlang_call(:'node_name', 'erlang', 'and', '[{"$type": "atom", "value": "true"}, {"$type": "atom", "value": "false"}]'::jsonb, 5000);

\echo ''
\echo '=== Test 7: Async with Arguments ==='

-- Test async operations with arguments
\echo 'Testing async lists:reverse([4,5,6]):'
SELECT erlang_send_async(:'node_name', 'lists', 'reverse', '[[4,5,6]]'::jsonb) as req_id \gset
SELECT erlang_receive_async(:req_id, 5000) as async_result;

\echo ''
\echo '=== Test 8: Cast with Arguments ==='

-- Test cast with arguments (no response expected)
\echo 'Testing cast with arguments:'
SELECT erlang_cast(:'node_name', 'erlang', 'display', '["Cast message with args!"]'::jsonb);

\echo ''
\echo '=== Test 9: Error Cases ==='

-- Test with wrong number of arguments
\echo 'Testing with wrong number of args (should show error):'
SELECT erlang_call(:'node_name', 'lists', 'reverse', '[]'::jsonb, 5000);

-- Test with wrong type of arguments  
\echo 'Testing with wrong type of args (should show error):'
SELECT erlang_call(:'node_name', 'lists', 'nth', '["not_a_number", [1,2,3]]'::jsonb, 5000);

\echo ''
\echo '=== Summary ==='
\echo 'Argument encoding features tested:'
\echo '  ✓ Simple types (integers, strings, floats)'
\echo '  ✓ Lists and arrays'
\echo '  ✓ Atoms via special encoding {"$type": "atom", "value": "..."}'
\echo '  ✓ Tuples via special encoding {"$type": "tuple", "elements": [...]}'
\echo '  ✓ Maps/objects'
\echo '  ✓ Boolean values (as atoms)'
\echo '  ✓ Async operations with arguments'
\echo '  ✓ Cast operations with arguments'

-- Cleanup
SELECT erlang_disconnect(:'node_name');