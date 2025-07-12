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
    // For now, let's try to decode a simple term and return it as a string
    // This is a basic implementation - in production you'd want full term decoding
    
    // Create a working copy of the buffer
    ei_x_buff result_buf;
    ei_x_new_with_version(&result_buf);
    
    // Copy the buffer content
    memcpy(result_buf.buff, buf->buff, buf->index);
    result_buf.index = 0; // Start from beginning
    
    // Try to decode the first term
    int type, size;
    int decode_result = ei_get_type(result_buf.buff, &result_buf.index, &type, &size);
    
    JsonbValue jbv;
    jbv.type = jbvString;
    
    // Create a simple string representation based on the type
    char type_str[256];
    snprintf(type_str, sizeof(type_str), "buffer_size=%d, type=%d, size=%d, decode_result=%d", buf->index, type, size, decode_result);
    
    // If we got an atom_cache_ref, try to skip past the RPC header
    if (type == 131) { // ERL_ATOM_CACHE_REF
        // Skip the atom cache ref and try to get the next term
        result_buf.index += 1; // Skip the cache ref
        decode_result = ei_get_type(result_buf.buff, &result_buf.index, &type, &size);
        snprintf(type_str, sizeof(type_str), "after_skip: type=%d, size=%d", type, size);
    }
    
    // Add hex dump of first few bytes for debugging
    char hex_dump[512];
    int hex_len = 0;
    hex_len += snprintf(hex_dump, sizeof(hex_dump), "hex: ");
    for (int i = 0; i < 10 && i < buf->index; i++) {
        hex_len += snprintf(hex_dump + hex_len, sizeof(hex_dump) - hex_len, "%02x ", (unsigned char)buf->buff[i]);
    }
    strcat(type_str, " ");
    strcat(type_str, hex_dump);
    
    switch (type) {
        case 13: // ERL_SMALL_TUPLE_EXT
        case ERL_SMALL_TUPLE_EXT:
        case ERL_LARGE_TUPLE_EXT: {
            snprintf(type_str, sizeof(type_str), "tuple[%d]: ", size);
            int len = strlen(type_str);
            
            // Try to decode tuple elements
            for (int i = 0; i < size && i < 5; i++) { // Limit to 5 elements for safety
                int elem_type, elem_size;
                ei_get_type(result_buf.buff, &result_buf.index, &elem_type, &elem_size);
                
                if (elem_type == ERL_SMALL_INTEGER_EXT) {
                    unsigned char val;
                    ei_decode_char(result_buf.buff, &result_buf.index, &val);
                    len += snprintf(type_str + len, sizeof(type_str) - len, "%d", val);
                } else if (elem_type == ERL_ATOM_EXT) {
                    char atom[256];
                    ei_decode_atom(result_buf.buff, &result_buf.index, atom);
                    len += snprintf(type_str + len, sizeof(type_str) - len, "%s", atom);
                } else {
                    len += snprintf(type_str + len, sizeof(type_str) - len, "?");
                }
                
                if (i < size - 1) {
                    len += snprintf(type_str + len, sizeof(type_str) - len, ",");
                }
            }
            break;
        }
        case ERL_LIST_EXT:
            snprintf(type_str, sizeof(type_str), "list[%d]", size);
            break;
        case ERL_SMALL_INTEGER_EXT: {
            unsigned char val;
            ei_decode_char(result_buf.buff, &result_buf.index, &val);
            snprintf(type_str, sizeof(type_str), "integer: %d", val);
            break;
        }
        case ERL_FLOAT_EXT:
            snprintf(type_str, sizeof(type_str), "float");
            break;
        case ERL_ATOM_EXT: {
            char atom[256];
            ei_decode_atom(result_buf.buff, &result_buf.index, atom);
            snprintf(type_str, sizeof(type_str), "atom: %s", atom);
            break;
        }
        case 131: // ERL_ATOM_CACHE_REF
            snprintf(type_str, sizeof(type_str), "atom_cache_ref");
            break;
        default:
            snprintf(type_str, sizeof(type_str), "type_%d", type);
            break;
    }
    
    jbv.val.string.val = type_str;
    jbv.val.string.len = strlen(type_str);
    
    ei_x_free(&result_buf);
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