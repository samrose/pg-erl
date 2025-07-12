#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/jsonb.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"
#include "miscadmin.h"
#include "erlang_cnode.h"
#include "jsonb_erlang_converter.c"
#include <ei_connect.h>
#define EI_HAVE_ERL_CONNECT
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

PG_MODULE_MAGIC;

// Global connection map
static HTAB *connection_map = NULL;

// Initialize the extension
void _PG_init(void) {
    HASHCTL ctl;
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
    char *home = getenv("HOME");
    if (home) {
        ereport(NOTICE, (errmsg("C-Node HOME: %s", home)));
        // Debug: Check if the cookie file exists
        char cookie_path[512];
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

// Call a remote Erlang function
PG_FUNCTION_INFO_V1(erlang_call);
Datum erlang_call(PG_FUNCTION_ARGS) {
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
    ei_x_buff buf;
    ei_x_buff result_buf;
    int rpc_status;
    Jsonb *result;

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
    ereport(NOTICE, (errmsg("Found connection with fd: %d", conn->fd)));

    ei_x_new_with_version(&buf);
    ereport(NOTICE, (errmsg("Encoding arguments, buf index before: %d", buf.index)));
    
    // For now, let's try encoding an empty list manually
    if (ei_x_encode_list_header(&buf, 0) < 0) {
        ei_x_free(&buf);
        MemoryContextSwitchTo(oldcontext);
        MemoryContextDelete(tmpcontext);
        pfree(node_name);
        pfree(module);
        pfree(function);
        ereport(ERROR, (errmsg("Failed to encode empty list")));
    }
    if (ei_x_encode_empty_list(&buf) < 0) {
        ei_x_free(&buf);
        MemoryContextSwitchTo(oldcontext);
        MemoryContextDelete(tmpcontext);
        pfree(node_name);
        pfree(module);
        pfree(function);
        ereport(ERROR, (errmsg("Failed to encode empty list end")));
    }
    
    ereport(NOTICE, (errmsg("Arguments encoded, buf index after: %d", buf.index)));

    ei_x_new_with_version(&result_buf);
    ereport(NOTICE, (errmsg("Calling RPC: %s:%s/%d", module, function, buf.index)));
    
    // Try using ei_rpc_to instead of ei_rpc
    rpc_status = ei_rpc_to(&conn->ec, conn->fd, module, function, buf.buff, buf.index);
    ereport(NOTICE, (errmsg("RPC call returned: %d", rpc_status)));
    if (rpc_status < 0) {
        int err = errno;
        ereport(NOTICE, (errmsg("RPC call failed with errno: %d (%s)", err, strerror(err))));
        ei_x_free(&buf);
        ei_x_free(&result_buf);
        MemoryContextSwitchTo(oldcontext);
        MemoryContextDelete(tmpcontext);
        pfree(node_name);
        pfree(module);
        pfree(function);
        ereport(ERROR, (errmsg("RPC call failed")));
    }

    result = erlang_term_to_jsonb(&result_buf);
    ei_x_free(&buf);
    ei_x_free(&result_buf);
    MemoryContextSwitchTo(oldcontext);
    MemoryContextDelete(tmpcontext);
    pfree(node_name);
    pfree(module);
    pfree(function);
    PG_RETURN_JSONB_P(result);
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