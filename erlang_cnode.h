#ifndef ERLANG_CNODE_H
#define ERLANG_CNODE_H

#include "postgres.h"
#include "fmgr.h"
#include <ei.h>
#include <ei_connect.h>

#define MAX_NODE_NAME 256
#define MAX_COOKIE 256

// Structure to store connection state
typedef struct {
    char node_name[MAX_NODE_NAME];
    char cookie[MAX_COOKIE];
    int fd; // File descriptor for the Erlang connection
    ei_cnode ec; // Store the ei_cnode struct
} ErlangConnection;

// Function declarations
Datum erlang_connect(PG_FUNCTION_ARGS);
Datum erlang_call(PG_FUNCTION_ARGS);
Datum erlang_disconnect(PG_FUNCTION_ARGS);

#endif 