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
static Jsonb *erlang_term_to_jsonb(ei_x_buff *buf) {
    JsonbValue jbv;
    char result_str[512];
    int index = 0;
    int type, size;
    
    // Skip version byte if present
    if (buf->buff[0] == 131) {
        index = 1;
    }
    
    // Get the type of the response
    if (ei_get_type(buf->buff, &index, &type, &size) < 0) {
        jbv.type = jbvString;
        jbv.val.string.val = "decode_error";
        jbv.val.string.len = strlen("decode_error");
        return JsonbValueToJsonb(&jbv);
    }
    
    // Handle tuple response from manual RPC
    if (type == ERL_SMALL_TUPLE_EXT || type == ERL_LARGE_TUPLE_EXT) {
        int arity;
        if (ei_decode_tuple_header(buf->buff, &index, &arity) < 0) {
            snprintf(result_str, sizeof(result_str), "tuple_decode_error");
        } else if (arity == 2) {
            // This is a 2-tuple response from $gen_call: {Ref, Result}
            // Use ei functions to properly decode
            
            // Skip first element (reference)
            int elem_type, elem_size;
            if (ei_get_type(buf->buff, &index, &elem_type, &elem_size) == 0) {
                if (elem_type == ERL_BINARY_EXT) {
                    // Skip the binary using ei_skip_term
                    ei_skip_term(buf->buff, &index);
                } else if (elem_type == ERL_NEW_REFERENCE_EXT || elem_type == ERL_NEWER_REFERENCE_EXT) {
                    erlang_ref ref;
                    ei_decode_ref(buf->buff, &index, &ref);
                } else {
                    // Skip whatever it is
                    ei_skip_term(buf->buff, &index);
                }
            }
            
            // Now decode the second element (the actual result)
            if (ei_get_type(buf->buff, &index, &elem_type, &elem_size) == 0) {
                if (elem_type == ERL_ATOM_EXT || elem_type == ERL_SMALL_ATOM_EXT || elem_type == ERL_ATOM_UTF8_EXT || elem_type == ERL_SMALL_ATOM_UTF8_EXT) {
                    char atom_result[256];
                    if (ei_decode_atom(buf->buff, &index, atom_result) == 0) {
                        snprintf(result_str, sizeof(result_str), "%s", atom_result);
                    } else {
                        snprintf(result_str, sizeof(result_str), "atom_decode_failed");
                    }
                } else if (elem_type == ERL_BINARY_EXT) {
                    char *binary_data;
                    long binary_len;
                    if (ei_decode_binary(buf->buff, &index, (void**)&binary_data, &binary_len) == 0) {
                        int copy_len = binary_len < 255 ? binary_len : 255;
                        memcpy(result_str, binary_data, copy_len);
                        result_str[copy_len] = '\0';
                        free(binary_data);
                    } else {
                        snprintf(result_str, sizeof(result_str), "binary_decode_failed");
                    }
                } else {
                    snprintf(result_str, sizeof(result_str), "result_type_%d", elem_type);
                }
            } else {
                snprintf(result_str, sizeof(result_str), "second_element_type_error");
            }
        } else {
            snprintf(result_str, sizeof(result_str), "unexpected_arity_%d", arity);
        }
    } else if (type == ERL_ATOM_EXT) {
        // Direct atom response
        char atom[256];
        if (ei_decode_atom(buf->buff, &index, atom) < 0) {
            snprintf(result_str, sizeof(result_str), "atom_decode_error");
        } else {
            snprintf(result_str, sizeof(result_str), "%s", atom);
        }
    } else {
        snprintf(result_str, sizeof(result_str), "unsupported_type_%d", type);
    }
    
    jbv.type = jbvString;
    jbv.val.string.val = result_str;
    jbv.val.string.len = strlen(result_str);
    
    return JsonbValueToJsonb(&jbv);
}

// Helper function to encode a simple list of arguments
static int encode_simple_args(ei_x_buff *buf, const char **args, int count) {
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
static int encode_numeric_args(ei_x_buff *buf, double *args, int count) {
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