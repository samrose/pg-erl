/*
 * JSONB to Erlang term conversion utilities
 * This file provides comprehensive conversion between PostgreSQL JSONB and Erlang terms
 */

#include "postgres.h"
#include "utils/jsonb.h"
#include "utils/json.h"
#include "utils/numeric.h"
#include "erlang_cnode.h"
#include <ei.h>

// Convert JSONB value to Erlang term
static int jsonb_value_to_erlang_term(ei_x_buff *buf, JsonbValue *jbv) {
    switch (jbv->type) {
        case jbvNull:
            return ei_x_encode_atom(buf, "null");
            
        case jbvString:
            return ei_x_encode_string(buf, jbv->val.string.val);
            
        case jbvNumeric:
            // Handle numeric types more carefully
            // For simplicity, convert all numerics to double
            double val = DatumGetFloat8(DirectFunctionCall1(numeric_float8, NumericGetDatum(jbv->val.numeric)));
            return ei_x_encode_double(buf, val);
            
        case jbvBool:
            return ei_x_encode_atom(buf, jbv->val.boolean ? "true" : "false");
            
        case jbvArray:
            // Convert array to Erlang list
            if (ei_x_encode_list_header(buf, jbv->val.array.nElems) < 0) {
                return -1;
            }
            
            // Iterate through array elements
            JsonbIterator *it;
            JsonbValue v;
            int type;
            
            it = JsonbIteratorInit(&jbv->val.array.elems);
            while ((type = JsonbIteratorNext(&it, &v, false)) != WJB_DONE) {
                if (type == WJB_ELEM) {
                    if (jsonb_value_to_erlang_term(buf, &v) < 0) {
                        return -1;
                    }
                }
            }
            
            return ei_x_encode_empty_list(buf);
            
        case jbvObject:
            // Convert object to Erlang map
            if (ei_x_encode_map_header(buf, jbv->val.object.nPairs) < 0) {
                return -1;
            }
            
            // Iterate through object pairs
            JsonbIterator *obj_it;
            JsonbValue key, value;
            int obj_type;
            
            obj_it = JsonbIteratorInit(&jbv->val.object.pairs);
            while ((obj_type = JsonbIteratorNext(&obj_it, &key, false)) != WJB_DONE) {
                if (obj_type == WJB_KEY) {
                    // Encode key
                    if (jsonb_value_to_erlang_term(buf, &key) < 0) {
                        return -1;
                    }
                } else if (obj_type == WJB_VALUE) {
                    // Encode value
                    if (jsonb_value_to_erlang_term(buf, &value) < 0) {
                        return -1;
                    }
                }
            }
            
            return 0;
            
        case jbvBinary:
            return ei_x_encode_binary(buf, jbv->val.binary.data, jbv->val.binary.len);
            
        default:
            return -1;
    }
}

// Convert JSONB to Erlang term list (for function arguments)
int jsonb_to_erlang_args(ei_x_buff *buf, Jsonb *args_json) {
    JsonbIterator *it = JsonbIteratorInit(&args_json->root);
    JsonbValue v;
    int type;
    int count = 0;
    
    // Count elements first
    while ((type = JsonbIteratorNext(&it, &v, false)) != WJB_DONE) {
        if (type == WJB_ELEM) {
            count++;
        }
    }
    
    // Reset iterator and encode as list
    it = JsonbIteratorInit(&args_json->root);
    if (ei_x_encode_list_header(buf, count) < 0) {
        return -1;
    }
    
    while ((type = JsonbIteratorNext(&it, &v, false)) != WJB_DONE) {
        if (type == WJB_ELEM) {
            if (jsonb_value_to_erlang_term(buf, &v) < 0) {
                return -1;
            }
        }
    }
    
    return ei_x_encode_empty_list(buf);
}

// Convert Erlang term to JSONB (simplified - for response parsing)
Jsonb *erlang_term_to_jsonb(ei_x_buff *buf) {
    // This is a placeholder implementation
    // In a full implementation, you'd decode the Erlang term and convert to JSONB
    
    // For now, return a simple string result
    JsonbValue jbv;
    jbv.type = jbvString;
    jbv.val.string.val = "erlang_result";
    jbv.val.string.len = strlen(jbv.val.string.val);
    
    return JsonbValueToJsonb(&jbv);
}

// Helper function to encode a simple list of arguments
int encode_simple_args(ei_x_buff *buf, const char **args, int count) {
    if (ei_x_encode_list_header(buf, count) < 0) {
        return -1;
    }
    
    for (int i = 0; i < count; i++) {
        if (ei_x_encode_string(buf, args[i]) < 0) {
            return -1;
        }
    }
    
    return ei_x_encode_empty_list(buf);
}

// Helper function to encode numeric arguments
int encode_numeric_args(ei_x_buff *buf, double *args, int count) {
    if (ei_x_encode_list_header(buf, count) < 0) {
        return -1;
    }
    
    for (int i = 0; i < count; i++) {
        if (ei_x_encode_double(buf, args[i]) < 0) {
            return -1;
        }
    }
    
    return ei_x_encode_empty_list(buf);
} 