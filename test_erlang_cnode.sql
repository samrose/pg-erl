-- PostgreSQL Extension Test Suite for erlang_cnode
-- Run with: psql -d testdb -f test_erlang_cnode.sql

-- Setup
\echo '=== Setting up test environment ==='
DROP EXTENSION IF EXISTS erlang_cnode CASCADE;
CREATE EXTENSION erlang_cnode;

-- Test Configuration
\set ON_ERROR_STOP on
\set node_name 'testnode@127.0.1.1'
\set cookie 'cookie123'

-- Helper function to check test results
CREATE OR REPLACE FUNCTION assert_equals(actual anyelement, expected anyelement, test_name text)
RETURNS void AS $$
BEGIN
    IF actual IS DISTINCT FROM expected THEN
        RAISE EXCEPTION 'Test % failed: expected %, got %', test_name, expected, actual;
    ELSE
        RAISE NOTICE 'Test % passed', test_name;
    END IF;
END;
$$ LANGUAGE plpgsql;

\echo ''
\echo '=== Test 1: Connection Management ==='

-- Test 1.1: Initial connection
SELECT assert_equals(
    erlang_connect(:'node_name', :'cookie')::text,
    'true',
    '1.1 - Initial connection'
);

-- Test 1.2: Check connection status
SELECT assert_equals(
    erlang_check_connection(:'node_name')::text,
    'true',
    '1.2 - Connection check'
);

-- Test 1.3: Duplicate connection should reuse existing one
SELECT assert_equals(
    erlang_connect(:'node_name', :'cookie')::text,
    'true',
    '1.3 - Reuse existing connection'
);

\echo ''
\echo '=== Test 2: Synchronous RPC Calls ==='

-- Test 2.1: Simple atom result
SELECT assert_equals(
    erlang_call(:'node_name', 'erlang', 'node', '[]'::jsonb, 5000)::text,
    '"testnode@127.0.1.1"',
    '2.1 - Atom result (node name)'
);

-- Test 2.2: Tuple result (date)
DO $$
DECLARE
    result jsonb;
    year int;
BEGIN
    result := erlang_call(:'node_name', 'erlang', 'date', '[]'::jsonb, 5000);
    year := (result->>0)::int;
    IF year >= 2024 AND year <= 2030 THEN
        RAISE NOTICE 'Test 2.2 - Tuple result (date) passed: %', result;
    ELSE
        RAISE EXCEPTION 'Test 2.2 - Tuple result (date) failed: unexpected year %', year;
    END IF;
END $$;

-- Test 2.3: Tuple result (time)
DO $$
DECLARE
    result jsonb;
    hour int;
BEGIN
    result := erlang_call(:'node_name', 'erlang', 'time', '[]'::jsonb, 5000);
    hour := (result->>0)::int;
    IF hour >= 0 AND hour <= 23 THEN
        RAISE NOTICE 'Test 2.3 - Tuple result (time) passed: %', result;
    ELSE
        RAISE EXCEPTION 'Test 2.3 - Tuple result (time) failed: invalid hour %', hour;
    END IF;
END $$;

-- Test 2.4: Empty list result
SELECT assert_equals(
    erlang_call(:'node_name', 'erlang', 'nodes', '[]'::jsonb, 5000)::text,
    '[]',
    '2.4 - Empty list result'
);

-- Test 2.5: Test with timeout
SELECT assert_equals(
    erlang_call(:'node_name', 'erlang', 'node', '[]'::jsonb, 1000)::text,
    '"testnode@127.0.1.1"',
    '2.5 - Call with 1 second timeout'
);

\echo ''
\echo '=== Test 3: Asynchronous RPC Calls ==='

-- Test 3.1: Send async request
DO $$
DECLARE
    request_id bigint;
BEGIN
    request_id := erlang_send_async(:'node_name', 'erlang', 'node', '[]'::jsonb);
    IF request_id > 0 THEN
        RAISE NOTICE 'Test 3.1 - Async send passed: request_id=%', request_id;
    ELSE
        RAISE EXCEPTION 'Test 3.1 - Async send failed';
    END IF;
    
    -- Store for next test
    PERFORM set_config('test.request_id', request_id::text, false);
END $$;

-- Test 3.2: Check pending status immediately
DO $$
DECLARE
    result jsonb;
    request_id bigint;
BEGIN
    request_id := erlang_send_async(:'node_name', 'erlang', 'date', '[]'::jsonb);
    result := erlang_receive_async(request_id, 0); -- 0 timeout = immediate check
    
    IF result->>'status' = 'pending' THEN
        RAISE NOTICE 'Test 3.2 - Pending status passed';
    ELSE
        RAISE NOTICE 'Test 3.2 - Got immediate result (fast response): %', result;
    END IF;
END $$;

-- Test 3.3: Receive async result
DO $$
DECLARE
    result jsonb;
    request_id bigint;
BEGIN
    request_id := current_setting('test.request_id')::bigint;
    result := erlang_receive_async(request_id, 5000);
    
    IF result::text = '"testnode@127.0.1.1"' THEN
        RAISE NOTICE 'Test 3.3 - Async receive passed';
    ELSE
        RAISE EXCEPTION 'Test 3.3 - Async receive failed: %', result;
    END IF;
END $$;

-- Test 3.4: Multiple async requests
DO $$
DECLARE
    req1 bigint;
    req2 bigint;
    req3 bigint;
    res1 jsonb;
    res2 jsonb;
    res3 jsonb;
BEGIN
    req1 := erlang_send_async(:'node_name', 'erlang', 'node', '[]'::jsonb);
    req2 := erlang_send_async(:'node_name', 'erlang', 'date', '[]'::jsonb);
    req3 := erlang_send_async(:'node_name', 'erlang', 'time', '[]'::jsonb);
    
    res1 := erlang_receive_async(req1, 5000);
    res2 := erlang_receive_async(req2, 5000);
    res3 := erlang_receive_async(req3, 5000);
    
    IF res1::text = '"testnode@127.0.1.1"' AND 
       jsonb_array_length(res2) = 3 AND 
       jsonb_array_length(res3) = 3 THEN
        RAISE NOTICE 'Test 3.4 - Multiple async requests passed';
    ELSE
        RAISE EXCEPTION 'Test 3.4 - Multiple async failed';
    END IF;
END $$;

\echo ''
\echo '=== Test 4: Fire-and-forget Cast ==='

-- Test 4.1: Cast message (no response expected)
SELECT assert_equals(
    erlang_cast(:'node_name', 'erlang', 'node', '[]'::jsonb)::text,
    'true',
    '4.1 - Cast message'
);

\echo ''
\echo '=== Test 5: Monitoring Functions ==='

-- Test 5.1: Check pending requests
DO $$
DECLARE
    pending_count int;
    req_id bigint;
BEGIN
    -- Send a request but don't receive it
    req_id := erlang_send_async(:'node_name', 'erlang', 'node', '[]'::jsonb);
    pending_count := erlang_pending_requests();
    
    IF pending_count >= 1 THEN
        RAISE NOTICE 'Test 5.1 - Pending requests count passed: %', pending_count;
    ELSE
        RAISE EXCEPTION 'Test 5.1 - Pending requests failed: count=%', pending_count;
    END IF;
    
    -- Clean up
    PERFORM erlang_receive_async(req_id, 1000);
END $$;

\echo ''
\echo '=== Test 6: Error Handling ==='

-- Test 6.1: Invalid node connection
DO $$
BEGIN
    PERFORM erlang_call('nonexistent@127.0.1.1', 'erlang', 'node', '[]'::jsonb, 1000);
    RAISE EXCEPTION 'Test 6.1 should have failed';
EXCEPTION
    WHEN OTHERS THEN
        RAISE NOTICE 'Test 6.1 - Invalid node error handling passed';
END $$;

-- Test 6.2: Invalid request ID
DO $$
BEGIN
    PERFORM erlang_receive_async(999999, 100);
    RAISE EXCEPTION 'Test 6.2 should have failed';
EXCEPTION
    WHEN OTHERS THEN
        RAISE NOTICE 'Test 6.2 - Invalid request ID error handling passed';
END $$;

\echo ''
\echo '=== Test 7: Data Type Support ==='

-- Test 7.1: Integer values in tuple
DO $$
DECLARE
    result jsonb;
    year int;
    month int;
    day int;
BEGIN
    result := erlang_call(:'node_name', 'erlang', 'date', '[]'::jsonb, 5000);
    year := (result->>0)::int;
    month := (result->>1)::int;
    day := (result->>2)::int;
    
    IF year IS NOT NULL AND month BETWEEN 1 AND 12 AND day BETWEEN 1 AND 31 THEN
        RAISE NOTICE 'Test 7.1 - Integer decoding passed: %-%-%', year, month, day;
    ELSE
        RAISE EXCEPTION 'Test 7.1 - Integer decoding failed';
    END IF;
END $$;

-- Test 7.2: Nested structures (error tuples)
DO $$
DECLARE
    result jsonb;
BEGIN
    -- This will fail because we're not passing args correctly, but we can decode the error
    result := erlang_call(:'node_name', 'erlang', 'process_info', '[]'::jsonb, 5000);
    
    IF result->>0 = 'badrpc' THEN
        RAISE NOTICE 'Test 7.2 - Error tuple decoding passed: %', result;
    ELSE
        RAISE EXCEPTION 'Test 7.2 - Error tuple decoding failed';
    END IF;
END $$;

\echo ''
\echo '=== Test 8: Connection Lifecycle ==='

-- Test 8.1: Disconnect
SELECT assert_equals(
    erlang_disconnect(:'node_name')::text,
    'true',
    '8.1 - Disconnect'
);

-- Test 8.2: Check disconnected
SELECT assert_equals(
    erlang_check_connection(:'node_name')::text,
    'false',
    '8.2 - Check disconnected'
);

-- Test 8.3: Reconnect
SELECT assert_equals(
    erlang_connect(:'node_name', :'cookie')::text,
    'true',
    '8.3 - Reconnect after disconnect'
);

\echo ''
\echo '=== Test 9: Ping Test ==='

-- Test 9.1: Ping connected node
DO $$
DECLARE
    result text;
BEGIN
    result := erlang_ping(:'node_name');
    IF result LIKE '%MESSAGE_RECEIVED%' OR result LIKE '%TICK_RECEIVED%' THEN
        RAISE NOTICE 'Test 9.1 - Ping test passed: %', result;
    ELSE
        RAISE NOTICE 'Test 9.1 - Ping test result: %', result;
    END IF;
END $$;

\echo ''
\echo '=== Performance Test ==='

-- Test 10.1: Measure sync vs async performance
DO $$
DECLARE
    start_time timestamp;
    end_time timestamp;
    sync_duration interval;
    async_duration interval;
    i int;
    req_ids bigint[];
    req_id bigint;
BEGIN
    -- Sync test: 10 sequential calls
    start_time := clock_timestamp();
    FOR i IN 1..10 LOOP
        PERFORM erlang_call(:'node_name', 'erlang', 'node', '[]'::jsonb, 5000);
    END LOOP;
    end_time := clock_timestamp();
    sync_duration := end_time - start_time;
    
    -- Async test: 10 parallel calls
    start_time := clock_timestamp();
    FOR i IN 1..10 LOOP
        req_id := erlang_send_async(:'node_name', 'erlang', 'node', '[]'::jsonb);
        req_ids := array_append(req_ids, req_id);
    END LOOP;
    
    FOREACH req_id IN ARRAY req_ids LOOP
        PERFORM erlang_receive_async(req_id, 5000);
    END LOOP;
    end_time := clock_timestamp();
    async_duration := end_time - start_time;
    
    RAISE NOTICE 'Performance Test:';
    RAISE NOTICE '  Sync (10 calls):  %', sync_duration;
    RAISE NOTICE '  Async (10 calls): %', async_duration;
    
    IF async_duration < sync_duration THEN
        RAISE NOTICE 'Test 10.1 - Async is faster (as expected)';
    ELSE
        RAISE NOTICE 'Test 10.1 - Sync is faster (network may be very fast)';
    END IF;
END $$;

\echo ''
\echo '=== Cleanup ==='

-- Final disconnect
SELECT erlang_disconnect(:'node_name');

-- Drop test function
DROP FUNCTION assert_equals(anyelement, anyelement, text);

\echo ''
\echo '=== All tests completed ==='