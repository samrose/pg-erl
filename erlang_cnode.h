#ifndef ERLANG_CNODE_H
#define ERLANG_CNODE_H

#include "postgres.h"
#include "fmgr.h"
#include <ei.h>
#include <ei_connect.h>

#define MAX_NODE_NAME 256
#define MAX_COOKIE 256
#define MAX_PENDING_REQUESTS 1000

// Structure to store connection state
typedef struct {
    char node_name[MAX_NODE_NAME];
    char cookie[MAX_COOKIE];
    int fd; // File descriptor for the Erlang connection
    ei_cnode ec; // Store the ei_cnode struct
} ErlangConnection;

// Structure to track async requests
typedef struct {
    int64 request_id;          // Unique request ID
    char node_name[MAX_NODE_NAME];
    unsigned long ref;          // Erlang reference for matching response
    time_t timestamp;           // When request was sent
    bool completed;             // Whether response has been received
    ei_x_buff response;         // Buffer to store response
} AsyncRequest;

// Function declarations
Datum erlang_connect(PG_FUNCTION_ARGS);
Datum erlang_call(PG_FUNCTION_ARGS);
Datum erlang_call_with_timeout(PG_FUNCTION_ARGS);
Datum erlang_ping(PG_FUNCTION_ARGS);
Datum erlang_disconnect(PG_FUNCTION_ARGS);

// Async function declarations
Datum erlang_send_async(PG_FUNCTION_ARGS);
Datum erlang_receive_async(PG_FUNCTION_ARGS);
Datum erlang_cast(PG_FUNCTION_ARGS);
Datum erlang_check_connection(PG_FUNCTION_ARGS);
Datum erlang_pending_requests(PG_FUNCTION_ARGS);

// JSONB conversion function declarations
int jsonb_to_erlang_args(ei_x_buff *buf, Jsonb *args_json);

#endif 