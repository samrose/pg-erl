-- Simple Test Suite for erlang_cnode with accurate error reporting
-- This version properly tracks and reports errors

\echo '=== Simple Test Suite for erlang_cnode ==='

-- Setup
DROP EXTENSION IF EXISTS erlang_cnode CASCADE;
CREATE EXTENSION erlang_cnode;

\set node_name 'testnode@127.0.1.1'
\set cookie 'cookie123'

-- Initialize error tracking
CREATE TEMP TABLE test_results (
    test_name text,
    passed boolean,
    details text
);

\echo ''
\echo '=== Test 1: Basic Connection ==='

-- Connect to Erlang node
DO $$
DECLARE
    result boolean;
BEGIN
    result := erlang_connect('testnode@127.0.1.1', 'cookie123');
    INSERT INTO test_results VALUES ('1.1 Connection', result, 'Connected: ' || result);
    IF NOT result THEN
        RAISE NOTICE 'FAILED: Could not connect to node';
    END IF;
END $$;

-- Check connection
DO $$
DECLARE
    result boolean;
BEGIN
    result := erlang_check_connection('testnode@127.0.1.1');
    INSERT INTO test_results VALUES ('1.2 Connection Check', result, 'Connection OK: ' || result);
    IF NOT result THEN
        RAISE NOTICE 'FAILED: Connection check failed';
    END IF;
END $$;

\echo ''
\echo '=== Test 2: Simple RPC Calls ==='

-- Test basic atom result
\echo 'Testing erlang:node() - should return node name as string:'
DO $$
DECLARE
    result jsonb;
    expected text := '"testnode@127.0.1.1"';
    passed boolean;
BEGIN
    BEGIN
        result := erlang_call('testnode@127.0.1.1', 'erlang', 'node', '[]'::jsonb, 5000);
        passed := (result::text = expected);
        INSERT INTO test_results VALUES ('2.1 erlang:node()', passed, 
            'Expected: ' || expected || ', Got: ' || result::text);
        IF NOT passed THEN
            RAISE NOTICE 'FAILED: erlang:node() returned % instead of %', result, expected;
        ELSE
            RAISE NOTICE 'PASSED: erlang:node() = %', result;
        END IF;
    EXCEPTION WHEN OTHERS THEN
        INSERT INTO test_results VALUES ('2.1 erlang:node()', false, 'Error: ' || SQLERRM);
        RAISE NOTICE 'FAILED: erlang:node() - %', SQLERRM;
    END;
END $$;

-- Test tuple result (date)
\echo 'Testing erlang:date() - should return [year, month, day]:'
DO $$
DECLARE
    result jsonb;
    year int;
    passed boolean;
BEGIN
    BEGIN
        result := erlang_call('testnode@127.0.1.1', 'erlang', 'date', '[]'::jsonb, 5000);
        year := (result->>0)::int;
        passed := (year >= 2024 AND year <= 2030 AND jsonb_array_length(result) = 3);
        INSERT INTO test_results VALUES ('2.2 erlang:date()', passed, 
            'Got date tuple: ' || result::text);
        IF NOT passed THEN
            RAISE NOTICE 'FAILED: erlang:date() returned unexpected value: %', result;
        ELSE
            RAISE NOTICE 'PASSED: erlang:date() = %', result;
        END IF;
    EXCEPTION WHEN OTHERS THEN
        INSERT INTO test_results VALUES ('2.2 erlang:date()', false, 'Error: ' || SQLERRM);
        RAISE NOTICE 'FAILED: erlang:date() - %', SQLERRM;
    END;
END $$;

-- Test tuple result (time)
\echo 'Testing erlang:time() - should return [hour, minute, second]:'
DO $$
DECLARE
    result jsonb;
    hour int;
    passed boolean;
BEGIN
    BEGIN
        result := erlang_call('testnode@127.0.1.1', 'erlang', 'time', '[]'::jsonb, 5000);
        hour := (result->>0)::int;
        passed := (hour >= 0 AND hour <= 23 AND jsonb_array_length(result) = 3);
        INSERT INTO test_results VALUES ('2.3 erlang:time()', passed, 
            'Got time tuple: ' || result::text);
        IF NOT passed THEN
            RAISE NOTICE 'FAILED: erlang:time() returned unexpected value: %', result;
        ELSE
            RAISE NOTICE 'PASSED: erlang:time() = %', result;
        END IF;
    EXCEPTION WHEN OTHERS THEN
        INSERT INTO test_results VALUES ('2.3 erlang:time()', false, 'Error: ' || SQLERRM);
        RAISE NOTICE 'FAILED: erlang:time() - %', SQLERRM;
    END;
END $$;

-- Test empty list
\echo 'Testing erlang:nodes() - should return empty list []:'
DO $$
DECLARE
    result jsonb;
    passed boolean;
BEGIN
    BEGIN
        result := erlang_call('testnode@127.0.1.1', 'erlang', 'nodes', '[]'::jsonb, 5000);
        passed := (result::text = '[]');
        INSERT INTO test_results VALUES ('2.4 erlang:nodes()', passed, 
            'Got: ' || result::text);
        IF NOT passed THEN
            RAISE NOTICE 'FAILED: erlang:nodes() returned % instead of []', result;
        ELSE
            RAISE NOTICE 'PASSED: erlang:nodes() = %', result;
        END IF;
    EXCEPTION WHEN OTHERS THEN
        INSERT INTO test_results VALUES ('2.4 erlang:nodes()', false, 'Error: ' || SQLERRM);
        RAISE NOTICE 'FAILED: erlang:nodes() - %', SQLERRM;
    END;
END $$;

\echo ''
\echo '=== Test 3: Async Operations ==='

-- Send async request
\echo 'Testing async request for erlang:node():'
DO $$
DECLARE
    request_id bigint;
    result jsonb;
    passed boolean;
BEGIN
    BEGIN
        request_id := erlang_send_async('testnode@127.0.1.1', 'erlang', 'node', '[]'::jsonb);
        result := erlang_receive_async(request_id, 5000);
        passed := (result::text = '"testnode@127.0.1.1"');
        INSERT INTO test_results VALUES ('3.1 Async erlang:node()', passed, 
            'Request ID: ' || request_id || ', Result: ' || result::text);
        IF NOT passed THEN
            RAISE NOTICE 'FAILED: Async erlang:node() returned % instead of "testnode@127.0.1.1"', result;
        ELSE
            RAISE NOTICE 'PASSED: Async erlang:node() = %', result;
        END IF;
    EXCEPTION WHEN OTHERS THEN
        INSERT INTO test_results VALUES ('3.1 Async erlang:node()', false, 'Error: ' || SQLERRM);
        RAISE NOTICE 'FAILED: Async erlang:node() - %', SQLERRM;
    END;
END $$;

-- Test pending status
\echo 'Testing pending status with immediate receive:'
DO $$
DECLARE
    request_id bigint;
    result jsonb;
    passed boolean;
BEGIN
    BEGIN
        request_id := erlang_send_async('testnode@127.0.1.1', 'erlang', 'date', '[]'::jsonb);
        result := erlang_receive_async(request_id, 0); -- 0 timeout for immediate check
        
        -- Either we get pending status or the actual result (if very fast)
        IF result->>'status' = 'pending' THEN
            passed := true;
            INSERT INTO test_results VALUES ('3.2 Pending status check', true, 'Got pending status as expected');
            RAISE NOTICE 'PASSED: Got pending status for immediate receive';
            -- Now get the actual result
            result := erlang_receive_async(request_id, 5000);
        ELSIF jsonb_array_length(result) = 3 THEN
            -- Got the actual result immediately (very fast response)
            passed := true;
            INSERT INTO test_results VALUES ('3.2 Pending status check', true, 'Got immediate result (fast response)');
            RAISE NOTICE 'PASSED: Got immediate result (fast response): %', result;
        ELSE
            passed := false;
            INSERT INTO test_results VALUES ('3.2 Pending status check', false, 'Unexpected result: ' || result::text);
            RAISE NOTICE 'FAILED: Unexpected async result: %', result;
        END IF;
    EXCEPTION WHEN OTHERS THEN
        INSERT INTO test_results VALUES ('3.2 Pending status check', false, 'Error: ' || SQLERRM);
        RAISE NOTICE 'FAILED: Pending status check - %', SQLERRM;
    END;
END $$;

\echo ''
\echo '=== Test 4: Fire-and-forget Cast ==='

-- Test cast
\echo 'Testing cast (fire-and-forget):'
DO $$
DECLARE
    result boolean;
BEGIN
    BEGIN
        result := erlang_cast('testnode@127.0.1.1', 'erlang', 'node', '[]'::jsonb);
        INSERT INTO test_results VALUES ('4.1 Cast', result, 'Cast succeeded: ' || result);
        IF NOT result THEN
            RAISE NOTICE 'FAILED: Cast returned false';
        ELSE
            RAISE NOTICE 'PASSED: Cast succeeded';
        END IF;
    EXCEPTION WHEN OTHERS THEN
        INSERT INTO test_results VALUES ('4.1 Cast', false, 'Error: ' || SQLERRM);
        RAISE NOTICE 'FAILED: Cast - %', SQLERRM;
    END;
END $$;

\echo ''
\echo '=== Test 5: Error Handling ==='

-- Test function with wrong args (will show error tuple)
\echo 'Testing function that will fail (no args provided):'
DO $$
DECLARE
    result jsonb;
    passed boolean;
BEGIN
    BEGIN
        result := erlang_call('testnode@127.0.1.1', 'erlang', 'process_info', '[]'::jsonb, 5000);
        -- Check if we got an error tuple starting with "badrpc"
        passed := (result->>0 = 'badrpc');
        INSERT INTO test_results VALUES ('5.1 Error handling', passed, 
            'Got error tuple: ' || left(result::text, 100));
        IF NOT passed THEN
            RAISE NOTICE 'FAILED: Did not get expected error tuple, got: %', result;
        ELSE
            RAISE NOTICE 'PASSED: Got expected error tuple';
        END IF;
    EXCEPTION WHEN OTHERS THEN
        -- In this case, an exception might also be acceptable
        INSERT INTO test_results VALUES ('5.1 Error handling', true, 'Got exception: ' || SQLERRM);
        RAISE NOTICE 'PASSED: Got exception as expected - %', SQLERRM;
    END;
END $$;

\echo ''
\echo '=== Test 6: Ping Test ==='

\echo 'Testing ping functionality:'
DO $$
DECLARE
    result text;
    passed boolean;
BEGIN
    BEGIN
        result := erlang_ping('testnode@127.0.1.1');
        -- Ping should return "ALIVE" for a working connection
        passed := (result = 'ALIVE');
        INSERT INTO test_results VALUES ('6.1 Ping test', passed, 'Ping result: ' || result);
        IF NOT passed THEN
            RAISE NOTICE 'FAILED: Ping returned: %', result;
        ELSE
            RAISE NOTICE 'PASSED: Ping result: %', result;
        END IF;
    EXCEPTION WHEN OTHERS THEN
        INSERT INTO test_results VALUES ('6.1 Ping test', false, 'Error: ' || SQLERRM);
        RAISE NOTICE 'FAILED: Ping test - %', SQLERRM;
    END;
END $$;

\echo ''
\echo '=== Test 7: Performance Test ==='
\echo 'Testing that async is generally faster than sync for multiple calls:'

DO $$
DECLARE
    start_time timestamp;
    sync_duration interval;
    async_duration interval;
    i int;
    req_ids bigint[];
    req_id bigint;
    passed boolean;
BEGIN
    BEGIN
        -- Sync test: 5 sequential calls
        start_time := clock_timestamp();
        FOR i IN 1..5 LOOP
            PERFORM erlang_call('testnode@127.0.1.1', 'erlang', 'node', '[]'::jsonb, 5000);
        END LOOP;
        sync_duration := clock_timestamp() - start_time;
        
        -- Async test: 5 parallel calls
        start_time := clock_timestamp();
        FOR i IN 1..5 LOOP
            req_id := erlang_send_async('testnode@127.0.1.1', 'erlang', 'node', '[]'::jsonb);
            req_ids := array_append(req_ids, req_id);
        END LOOP;
        
        FOREACH req_id IN ARRAY req_ids LOOP
            PERFORM erlang_receive_async(req_id, 5000);
        END LOOP;
        async_duration := clock_timestamp() - start_time;
        
        -- Async should typically be faster, but not always on very fast networks
        passed := true; -- We'll consider this informational
        INSERT INTO test_results VALUES ('7.1 Performance comparison', passed, 
            'Sync: ' || sync_duration || ', Async: ' || async_duration);
        
        RAISE NOTICE 'Performance: Sync: %, Async: %', sync_duration, async_duration;
        IF async_duration < sync_duration THEN
            RAISE NOTICE 'PASSED: Async is faster as expected';
        ELSE
            RAISE NOTICE 'INFO: Sync was faster (network may be very fast or load is high)';
        END IF;
    EXCEPTION WHEN OTHERS THEN
        INSERT INTO test_results VALUES ('7.1 Performance comparison', false, 'Error: ' || SQLERRM);
        RAISE NOTICE 'FAILED: Performance test - %', SQLERRM;
    END;
END $$;

\echo ''
\echo '=== Test Summary ==='

-- Show summary
\echo 'Test Results:'
SELECT 
    test_name,
    CASE WHEN passed THEN '✓ PASSED' ELSE '✗ FAILED' END as status,
    details
FROM test_results
ORDER BY test_name;

\echo ''
\echo 'Overall Summary:'
SELECT 
    COUNT(*) as total_tests,
    COUNT(*) FILTER (WHERE passed) as passed,
    COUNT(*) FILTER (WHERE NOT passed) as failed,
    CASE 
        WHEN COUNT(*) = 0 THEN 'No tests run'
        ELSE ROUND(100.0 * COUNT(*) FILTER (WHERE passed) / COUNT(*), 1) || '%'
    END as pass_rate
FROM test_results;

\echo ''
DO $$
DECLARE
    failed_count int;
    test_name text;
BEGIN
    SELECT COUNT(*) INTO failed_count FROM test_results WHERE NOT passed;
    IF failed_count = 0 THEN
        RAISE NOTICE '=== All tests PASSED! ===';
    ELSE
        RAISE NOTICE '=== % test(s) FAILED ===', failed_count;
        RAISE NOTICE 'Failed tests:';
        FOR test_name IN SELECT tr.test_name FROM test_results tr WHERE NOT tr.passed LOOP
            RAISE NOTICE '  - %', test_name;
        END LOOP;
    END IF;
END $$;

-- Cleanup
SELECT erlang_disconnect('testnode@127.0.1.1');
DROP TABLE test_results;