#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/jsonb.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"
#include "miscadmin.h"
#include "erlang_cnode.h"
#include "utils/json.h"

// Forward declaration for jsonb_erlang_converter functions  
static Jsonb *erlang_term_to_jsonb(ei_x_buff *buf);

#include "jsonb_erlang_converter.c"
#include <ei_connect.h>
#define EI_HAVE_ERL_CONNECT
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif

PG_MODULE_MAGIC;

// Global connection map
static HTAB *connection_map = NULL;

// Forward declarations
static Datum erlang_call_internal(PG_FUNCTION_ARGS, int timeout_ms);

// Initialize the extension
void _PG_init(void) {
    HASHCTL ctl;
    
    // Initialize ei library - must be called exactly once
    if (ei_init() < 0) {
        ereport(ERROR, (errmsg("Failed to initialize ei library")));
    }
    
    MemSet(&ctl, 0, sizeof(ctl));
    ctl.keysize = MAX_NODE_NAME;
    ctl.entrysize = sizeof(ErlangConnection);
    ctl.hcxt = TopMemoryContext;
    connection_map = hash_create("ErlangConnections", 16, &ctl, HASH_ELEM | HASH_CONTEXT);
}

// Connect to an Erlang node
PG_FUNCTION_INFO_V1(erlang_connect);
Datum erlang_connect(PG_FUNCTION_ARGS) {
    text *node_name_text;
    text *cookie_text;
    char *node_name;
    char *cookie;
    MemoryContext oldcontext;
    MemoryContext tmpcontext;
    ei_cnode ec;
    int fd = -1;
    char cnode_name[256];
    bool found;
    ErlangConnection *conn;

    node_name_text = PG_GETARG_TEXT_PP(0);
    cookie_text = PG_GETARG_TEXT_PP(1);
    node_name = text_to_cstring(node_name_text);
    cookie = text_to_cstring(cookie_text);
    oldcontext = CurrentMemoryContext;
    tmpcontext = AllocSetContextCreate(CurrentMemoryContext, "ErlangCNodeTemp", ALLOCSET_DEFAULT_SIZES);
    MemoryContextSwitchTo(tmpcontext);

    snprintf(cnode_name, sizeof(cnode_name), "pgcnode_%d_%ld@127.0.1.1", getpid(), (long)time(NULL));
    
    // Debug: Print the HOME environment variable
    {
        char *home = getenv("HOME");
        if (home) {
            char cookie_path[512];
            ereport(NOTICE, (errmsg("C-Node HOME: %s", home)));
            // Debug: Check if the cookie file exists
        snprintf(cookie_path, sizeof(cookie_path), "%s/.erlang.cookie", home);
        ereport(NOTICE, (errmsg("C-Node looking for cookie at: %s", cookie_path)));
        
        FILE *f = fopen(cookie_path, "r");
        if (f) {
            ereport(NOTICE, (errmsg("Cookie file exists and is readable")));
            fclose(f);
        } else {
            int err = errno;
            ereport(NOTICE, (errmsg("Cookie file not found or not readable: %s", strerror(err))));
        }
        } else {
            ereport(NOTICE, (errmsg("C-Node HOME: not set")));
        }
    }
    
    // Try to initialize the C-Node with more detailed error reporting
    ereport(NOTICE, (errmsg("Attempting to initialize C-Node as: %s", cnode_name)));
    
    // Initialize C-Node first
    snprintf(cnode_name, sizeof(cnode_name), "pgcnode_%d@127.0.1.1", getpid());
    ereport(NOTICE, (errmsg("Initializing C-Node as: %s", cnode_name)));
    
    // Initialize the ei_cnode structure
    memset(&ec, 0, sizeof(ei_cnode));
    
    // Try to set some environment variables that might help
    setenv("EI_TRACELEVEL", "5", 1);
    setenv("ERL_EPMD_PORT", "4369", 1);
    
    // Initialize the C-Node (old method - removed)
    
    ereport(NOTICE, (errmsg("C-Node initialized, now trying connection to: %s", node_name)));
    
    // Check if epmd is running and accessible
    ereport(NOTICE, (errmsg("Checking epmd connectivity...")));
    
    // Initialize the C-Node first
    char *hostname = "127.0.1.1";
    char *alive = "pgcnode";
    int creation = 0;
    
    int init_result = ei_connect_xinit(&ec, hostname, alive, cnode_name, NULL, cookie, creation);
    ereport(NOTICE, (errmsg("ei_connect_xinit returned: %d", init_result)));
    if (init_result < 0) {
        int err = errno;
        ereport(ERROR, (errmsg("ei_connect_xinit failed with return value %d: %s (errno: %d)", init_result, strerror(err), err)));
    }
    
    // Now connect to the target node
    fd = ei_connect(&ec, node_name);
    ereport(NOTICE, (errmsg("ei_connect returned fd: %d", fd)));
    if (fd < 0) {
        int err = errno;
        ereport(ERROR, (errmsg("ei_connect failed with return value %d: %s (errno: %d)", fd, strerror(err), err)));
    }
    
    ereport(NOTICE, (errmsg("Connection established successfully")));

    conn = (ErlangConnection *) hash_search(connection_map, node_name, HASH_ENTER, &found);
    if (found) {
        ereport(NOTICE, (errmsg("Closing existing connection fd: %d", conn->fd)));
        close(conn->fd);
    }
    strlcpy(conn->node_name, node_name, MAX_NODE_NAME);
    strlcpy(conn->cookie, cookie, MAX_COOKIE);
    conn->fd = fd;
    ereport(NOTICE, (errmsg("Stored connection fd: %d", conn->fd)));
    memcpy(&conn->ec, &ec, sizeof(ei_cnode));

    MemoryContextSwitchTo(oldcontext);
    MemoryContextDelete(tmpcontext);
    pfree(node_name);
    pfree(cookie);
    PG_RETURN_BOOL(true);
}

// Call a remote Erlang function (original, uses default timeout)
PG_FUNCTION_INFO_V1(erlang_call);
Datum erlang_call(PG_FUNCTION_ARGS) {
    // Call the timeout version with default 5000ms timeout
    return erlang_call_internal(fcinfo, 5000);
}

// Call a remote Erlang function with custom timeout
PG_FUNCTION_INFO_V1(erlang_call_with_timeout);
Datum erlang_call_with_timeout(PG_FUNCTION_ARGS) {
    int32 timeout_ms = PG_GETARG_INT32(4);
    
    // Enforce maximum timeout of 30 seconds
    if (timeout_ms > 30000) {
        timeout_ms = 30000;
        ereport(NOTICE, (errmsg("Timeout capped at maximum 30000ms")));
    }
    if (timeout_ms <= 0) {
        timeout_ms = 5000;
        ereport(NOTICE, (errmsg("Invalid timeout, using default 5000ms")));
    }
    
    return erlang_call_internal(fcinfo, timeout_ms);
}

// Internal implementation with timeout support and non-blocking I/O
static Datum erlang_call_internal(PG_FUNCTION_ARGS, int timeout_ms) {
    text *node_name_text;
    text *module_text;
    text *function_text;
    Jsonb *args_json;
    char *node_name;
    char *module;
    char *function;
    MemoryContext oldcontext;
    MemoryContext tmpcontext;
    bool found;
    ErlangConnection *conn;
    ei_x_buff send_buf;
    ei_x_buff recv_buf;
    Jsonb *result;
    erlang_ref ref;
    erlang_msg msg;
    
    node_name_text = PG_GETARG_TEXT_PP(0);
    module_text = PG_GETARG_TEXT_PP(1);
    function_text = PG_GETARG_TEXT_PP(2);
    args_json = PG_GETARG_JSONB_P(3);
    node_name = text_to_cstring(node_name_text);
    module = text_to_cstring(module_text);
    function = text_to_cstring(function_text);
    oldcontext = CurrentMemoryContext;
    tmpcontext = AllocSetContextCreate(CurrentMemoryContext, "ErlangCNodeTemp", ALLOCSET_DEFAULT_SIZES);
    MemoryContextSwitchTo(tmpcontext);

    conn = (ErlangConnection *) hash_search(connection_map, node_name, HASH_FIND, &found);
    if (!found) {
        MemoryContextSwitchTo(oldcontext);
        MemoryContextDelete(tmpcontext);
        pfree(node_name);
        pfree(module);
        pfree(function);
        ereport(ERROR, (errmsg("No connection to node: %s", node_name)));
    }
    ereport(NOTICE, (errmsg("Found connection with fd: %d, using timeout: %dms", conn->fd, timeout_ms)));

    // Initialize buffers
    ei_x_new_with_version(&send_buf);
    ei_x_new(&recv_buf);
    
    ereport(NOTICE, (errmsg("Using MANUAL RPC implementation for %s:%s with timeout %dms", module, function, timeout_ms)));
    ereport(NOTICE, (errmsg("DEBUG: About to start manual RPC message construction")));
    
    // Manual RPC message construction
    // Format: {'$gen_call', {FromPid, Ref}, {call, Module, Function, Args, user}}
    ei_x_encode_tuple_header(&send_buf, 3);
    ei_x_encode_atom(&send_buf, "$gen_call");
    
    // {FromPid, Ref}
    ei_x_encode_tuple_header(&send_buf, 2);
    ei_x_encode_pid(&send_buf, ei_self(&conn->ec));
    ei_x_encode_ulong(&send_buf, (unsigned long)time(NULL)); // Simple ref
    
    // {call, Module, Function, Args, user}
    ei_x_encode_tuple_header(&send_buf, 5);
    ei_x_encode_atom(&send_buf, "call");
    ei_x_encode_atom(&send_buf, module);
    ei_x_encode_atom(&send_buf, function);
    ei_x_encode_empty_list(&send_buf);  // Args = []
    ei_x_encode_atom(&send_buf, "user");  // Group leader
    
    ereport(NOTICE, (errmsg("Sending manual RPC message to rex process...")));
    
    // Send to rex process
    int send_status = ei_reg_send(&conn->ec, conn->fd, "rex", send_buf.buff, send_buf.index);
    
    if (send_status < 0) {
        int err = errno;
        ei_x_free(&send_buf);
        ei_x_free(&recv_buf);
        MemoryContextSwitchTo(oldcontext);
        MemoryContextDelete(tmpcontext);
        pfree(node_name);
        pfree(module);
        pfree(function);
        ereport(ERROR, (errmsg("Manual RPC send failed: %s (error: %d)", strerror(err), err)));
    }
    
    ereport(NOTICE, (errmsg("Manual RPC message sent, waiting for response...")));
    
    // Receive the response
    int recv_status = ei_receive_msg_tmo(conn->fd, &msg, &recv_buf, timeout_ms);
    if (recv_status < 0) {
        int err = errno;
        ei_x_free(&send_buf);
        ei_x_free(&recv_buf);
        MemoryContextSwitchTo(oldcontext);
        MemoryContextDelete(tmpcontext);
        pfree(node_name);
        pfree(module);
        pfree(function);
        ereport(ERROR, (errmsg("Manual RPC receive failed: %s (error: %d)", strerror(err), err)));
    }
    
    ereport(NOTICE, (errmsg("Manual RPC response received, buffer size: %d", recv_buf.index)));
    
    // Parse the response
    result = erlang_term_to_jsonb(&recv_buf);
    
    ei_x_free(&send_buf);
    ei_x_free(&recv_buf);
    MemoryContextSwitchTo(oldcontext);
    MemoryContextDelete(tmpcontext);
    pfree(node_name);
    pfree(module);
    pfree(function);
    
    PG_RETURN_JSONB_P(result);
}

// Test basic connectivity with direct message passing (no RPC)
PG_FUNCTION_INFO_V1(erlang_ping);
Datum erlang_ping(PG_FUNCTION_ARGS) {
    text *node_name_text;
    char *node_name;
    bool found;
    ErlangConnection *conn;
    ei_x_buff send_buf;
    ei_x_buff recv_buf;
    erlang_msg msg;
    text *result_text;
    int recv_result;
    
    node_name_text = PG_GETARG_TEXT_PP(0);
    node_name = text_to_cstring(node_name_text);
    
    conn = (ErlangConnection *) hash_search(connection_map, node_name, HASH_FIND, &found);
    if (!found) {
        pfree(node_name);
        ereport(ERROR, (errmsg("No connection to node: %s", node_name)));
    }
    
    ereport(NOTICE, (errmsg("Testing basic connectivity to node: %s (fd: %d)", node_name, conn->fd)));
    
    // Initialize buffers
    ei_x_new_with_version(&send_buf);
    ei_x_new(&recv_buf);
    
    // Send a simple ping message to the shell process
    ei_x_encode_atom(&send_buf, "ping");
    
    // Send to the shell process (user process)  
    if (ei_reg_send(&conn->ec, conn->fd, "user", send_buf.buff, send_buf.index) < 0) {
        int err = errno;
        ei_x_free(&send_buf);
        ei_x_free(&recv_buf);
        pfree(node_name);
        ereport(ERROR, (errmsg("Failed to send ping message: %s", strerror(err))));
    }
    
    ereport(NOTICE, (errmsg("Ping message sent, waiting for any response...")));
    
    // Try to receive any message (with 5 second timeout)
    recv_result = ei_receive_msg_tmo(conn->fd, &msg, &recv_buf, 5000);
    
    if (recv_result == ERL_TICK) {
        result_text = cstring_to_text("TICK_RECEIVED");
    } else if (recv_result == ERL_MSG) {
        char response[256];
        snprintf(response, sizeof(response), "MESSAGE_RECEIVED_TYPE_%ld_FROM_%s", 
                (long)msg.msgtype, msg.from.node);
        result_text = cstring_to_text(response);
    } else if (recv_result == ERL_ERROR) {
        char response[256];
        snprintf(response, sizeof(response), "ERROR_%d", errno);
        result_text = cstring_to_text(response);
    } else {
        char response[256];
        snprintf(response, sizeof(response), "UNKNOWN_RESULT_%d", recv_result);
        result_text = cstring_to_text(response);
    }
    
    ei_x_free(&send_buf);
    ei_x_free(&recv_buf);
    pfree(node_name);
    
    PG_RETURN_TEXT_P(result_text);
}

// Disconnect from an Erlang node
PG_FUNCTION_INFO_V1(erlang_disconnect);
Datum erlang_disconnect(PG_FUNCTION_ARGS) {
    text *node_name_text;
    char *node_name;
    bool found;
    ErlangConnection *conn;

    node_name_text = PG_GETARG_TEXT_PP(0);
    node_name = text_to_cstring(node_name_text);
    conn = (ErlangConnection *) hash_search(connection_map, node_name, HASH_REMOVE, &found);
    if (found) {
        close(conn->fd);
    }
    pfree(node_name);
    PG_RETURN_BOOL(found);
}

// Global request tracking
static HTAB *async_request_map = NULL;
static int64 next_request_id = 1;

// Initialize async request tracking
static void init_async_requests(void) {
    if (async_request_map == NULL) {
        HASHCTL ctl;
        MemSet(&ctl, 0, sizeof(ctl));
        ctl.keysize = sizeof(int64);
        ctl.entrysize = sizeof(AsyncRequest);
        ctl.hcxt = TopMemoryContext;
        async_request_map = hash_create("AsyncRequests", 32, &ctl, HASH_ELEM | HASH_CONTEXT);
    }
}

// Send async RPC request, returns request ID immediately
PG_FUNCTION_INFO_V1(erlang_send_async);
Datum erlang_send_async(PG_FUNCTION_ARGS) {
    text *node_name_text;
    text *module_text;
    text *function_text;
    Jsonb *args_json;
    char *node_name;
    char *module;
    char *function;
    bool found;
    ErlangConnection *conn;
    AsyncRequest *request;
    ei_x_buff send_buf;
    int64 request_id;
    unsigned long ref;
    
    init_async_requests();
    
    node_name_text = PG_GETARG_TEXT_PP(0);
    module_text = PG_GETARG_TEXT_PP(1);
    function_text = PG_GETARG_TEXT_PP(2);
    args_json = PG_GETARG_JSONB_P(3);
    
    node_name = text_to_cstring(node_name_text);
    module = text_to_cstring(module_text);
    function = text_to_cstring(function_text);
    
    // Find connection
    conn = (ErlangConnection *) hash_search(connection_map, node_name, HASH_FIND, &found);
    if (!found) {
        pfree(node_name);
        pfree(module);
        pfree(function);
        ereport(ERROR, (errmsg("No connection to node: %s", node_name)));
    }
    
    // Generate unique request ID and reference
    request_id = next_request_id++;
    ref = (unsigned long)request_id;  // Use request_id as ref for simplicity
    
    // Create async request entry
    request = (AsyncRequest *) hash_search(async_request_map, &request_id, HASH_ENTER, &found);
    request->request_id = request_id;
    strlcpy(request->node_name, node_name, MAX_NODE_NAME);
    request->ref = ref;
    request->timestamp = time(NULL);
    request->completed = false;
    ei_x_new(&request->response);
    
    // Build and send RPC message
    ei_x_new_with_version(&send_buf);
    
    // Format: {'$gen_call', {FromPid, Ref}, {call, Module, Function, Args, user}}
    ei_x_encode_tuple_header(&send_buf, 3);
    ei_x_encode_atom(&send_buf, "$gen_call");
    
    // {FromPid, Ref}
    ei_x_encode_tuple_header(&send_buf, 2);
    ei_x_encode_pid(&send_buf, ei_self(&conn->ec));
    ei_x_encode_ulong(&send_buf, ref);
    
    // {call, Module, Function, Args, user}
    ei_x_encode_tuple_header(&send_buf, 5);
    ei_x_encode_atom(&send_buf, "call");
    ei_x_encode_atom(&send_buf, module);
    ei_x_encode_atom(&send_buf, function);
    
    // TODO: Encode actual args from JSONB
    ei_x_encode_empty_list(&send_buf);
    
    ei_x_encode_atom(&send_buf, "user");
    
    // Send to rex process
    if (ei_reg_send(&conn->ec, conn->fd, "rex", send_buf.buff, send_buf.index) < 0) {
        ei_x_free(&send_buf);
        ei_x_free(&request->response);
        hash_search(async_request_map, &request_id, HASH_REMOVE, NULL);
        pfree(node_name);
        pfree(module);
        pfree(function);
        ereport(ERROR, (errmsg("Failed to send async RPC request")));
    }
    
    ei_x_free(&send_buf);
    pfree(node_name);
    pfree(module);
    pfree(function);
    
    PG_RETURN_INT64(request_id);
}

// Receive async RPC response
PG_FUNCTION_INFO_V1(erlang_receive_async);
Datum erlang_receive_async(PG_FUNCTION_ARGS) {
    int64 request_id;
    int32 timeout_ms;
    bool found;
    AsyncRequest *request;
    ErlangConnection *conn;
    erlang_msg msg;
    ei_x_buff recv_buf;
    Jsonb *result;
    JsonbParseState *state = NULL;
    JsonbValue *jbv_result;
    JsonbValue key_status;
    JsonbValue val_pending;
    JsonbValue val_error;
    int recv_status;
    
    init_async_requests();
    
    request_id = PG_GETARG_INT64(0);
    timeout_ms = PG_GETARG_INT32(1);
    
    // Find the request
    request = (AsyncRequest *) hash_search(async_request_map, &request_id, HASH_FIND, &found);
    if (!found) {
        ereport(ERROR, (errmsg("Request ID %ld not found", request_id)));
    }
    
    // If already completed, return cached response
    if (request->completed) {
        result = erlang_term_to_jsonb(&request->response);
        PG_RETURN_JSONB_P(result);
    }
    
    // Find connection
    conn = (ErlangConnection *) hash_search(connection_map, request->node_name, HASH_FIND, &found);
    if (!found) {
        ereport(ERROR, (errmsg("Connection lost for request %ld", request_id)));
    }
    
    // Try to receive response with timeout
    ei_x_new(&recv_buf);
    recv_status = ei_receive_msg_tmo(conn->fd, &msg, &recv_buf, timeout_ms);
    
    if (recv_status == ERL_MSG) {
        // Store response and mark as completed
        ei_x_free(&request->response);
        request->response = recv_buf;
        request->completed = true;
        
        result = erlang_term_to_jsonb(&request->response);
        PG_RETURN_JSONB_P(result);
    } else if (recv_status == ERL_TICK) {
        // Got a tick, request still pending
        ei_x_free(&recv_buf);
        
        // Return JSON indicating pending status
        pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);
        
        key_status.type = jbvString;
        key_status.val.string.val = "status";
        key_status.val.string.len = 6;
        pushJsonbValue(&state, WJB_KEY, &key_status);
        
        val_pending.type = jbvString;
        val_pending.val.string.val = "pending";
        val_pending.val.string.len = 7;
        pushJsonbValue(&state, WJB_VALUE, &val_pending);
        
        jbv_result = pushJsonbValue(&state, WJB_END_OBJECT, NULL);
        PG_RETURN_JSONB_P(JsonbValueToJsonb(jbv_result));
    } else {
        // Error or timeout
        ei_x_free(&recv_buf);
        
        pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);
        
        key_status.type = jbvString;
        key_status.val.string.val = "status";
        key_status.val.string.len = 6;
        pushJsonbValue(&state, WJB_KEY, &key_status);
        
        val_error.type = jbvString;
        val_error.val.string.val = "timeout";
        val_error.val.string.len = 7;
        pushJsonbValue(&state, WJB_VALUE, &val_error);
        
        jbv_result = pushJsonbValue(&state, WJB_END_OBJECT, NULL);
        PG_RETURN_JSONB_P(JsonbValueToJsonb(jbv_result));
    }
}

// Fire-and-forget cast (no response expected)
PG_FUNCTION_INFO_V1(erlang_cast);
Datum erlang_cast(PG_FUNCTION_ARGS) {
    text *node_name_text;
    text *module_text;
    text *function_text;
    Jsonb *args_json;
    char *node_name;
    char *module;
    char *function;
    bool found;
    ErlangConnection *conn;
    ei_x_buff send_buf;
    
    node_name_text = PG_GETARG_TEXT_PP(0);
    module_text = PG_GETARG_TEXT_PP(1);
    function_text = PG_GETARG_TEXT_PP(2);
    args_json = PG_GETARG_JSONB_P(3);
    
    node_name = text_to_cstring(node_name_text);
    module = text_to_cstring(module_text);
    function = text_to_cstring(function_text);
    
    // Find connection
    conn = (ErlangConnection *) hash_search(connection_map, node_name, HASH_FIND, &found);
    if (!found) {
        pfree(node_name);
        pfree(module);
        pfree(function);
        ereport(ERROR, (errmsg("No connection to node: %s", node_name)));
    }
    
    // Build cast message
    ei_x_new_with_version(&send_buf);
    
    // Format: {'$gen_cast', {cast, Module, Function, Args}}
    ei_x_encode_tuple_header(&send_buf, 2);
    ei_x_encode_atom(&send_buf, "$gen_cast");
    
    ei_x_encode_tuple_header(&send_buf, 4);
    ei_x_encode_atom(&send_buf, "cast");
    ei_x_encode_atom(&send_buf, module);
    ei_x_encode_atom(&send_buf, function);
    
    // TODO: Encode actual args from JSONB
    ei_x_encode_empty_list(&send_buf);
    
    // Send to rex process
    if (ei_reg_send(&conn->ec, conn->fd, "rex", send_buf.buff, send_buf.index) < 0) {
        ei_x_free(&send_buf);
        pfree(node_name);
        pfree(module);
        pfree(function);
        ereport(ERROR, (errmsg("Failed to send cast message")));
    }
    
    ei_x_free(&send_buf);
    pfree(node_name);
    pfree(module);
    pfree(function);
    
    PG_RETURN_BOOL(true);
}

// Check connection health
PG_FUNCTION_INFO_V1(erlang_check_connection);
Datum erlang_check_connection(PG_FUNCTION_ARGS) {
    text *node_name_text;
    char *node_name;
    bool found;
    ErlangConnection *conn;
    erlang_msg msg;
    ei_x_buff test_buf;
    int result;
    
    node_name_text = PG_GETARG_TEXT_PP(0);
    node_name = text_to_cstring(node_name_text);
    
    conn = (ErlangConnection *) hash_search(connection_map, node_name, HASH_FIND, &found);
    if (!found) {
        pfree(node_name);
        PG_RETURN_BOOL(false);
    }
    
    // Check if connection is still alive by trying to receive with 0 timeout
    ei_x_new(&test_buf);
    result = ei_receive_msg_tmo(conn->fd, &msg, &test_buf, 0);
    ei_x_free(&test_buf);
    
    if (result == ERL_ERROR && errno != EAGAIN && errno != ETIMEDOUT) {
        // Connection is dead, remove it
        close(conn->fd);
        hash_search(connection_map, node_name, HASH_REMOVE, NULL);
        pfree(node_name);
        PG_RETURN_BOOL(false);
    }
    
    pfree(node_name);
    PG_RETURN_BOOL(true);
}

// Get count of pending requests
PG_FUNCTION_INFO_V1(erlang_pending_requests);
Datum erlang_pending_requests(PG_FUNCTION_ARGS) {
    HASH_SEQ_STATUS seq;
    AsyncRequest *request;
    int32 pending_count = 0;
    
    init_async_requests();
    
    if (async_request_map != NULL) {
        hash_seq_init(&seq, async_request_map);
        while ((request = (AsyncRequest *) hash_seq_search(&seq)) != NULL) {
            if (!request->completed) {
                pending_count++;
            }
        }
    }
    
    PG_RETURN_INT32(pending_count);
} 