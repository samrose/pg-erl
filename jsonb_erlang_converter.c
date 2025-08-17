/*
 * JSONB to Erlang term conversion utilities
 * This file provides comprehensive conversion between PostgreSQL JSONB and Erlang terms
 */

#include "postgres.h"
#include "utils/jsonb.h"
#include "utils/json.h"
#include "utils/numeric.h"
#include "utils/builtins.h"
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

// Forward declaration for recursive decoding
static JsonbValue *decode_erlang_term_recursive(char *buf, int *index);

// Convert Erlang term to JSONB with full type support
static Jsonb *erlang_term_to_jsonb(ei_x_buff *buf) {
    int index = 0;
    int type, size;
    JsonbValue *result;
    
    // Skip version byte if present
    if (buf->buff[0] == 131) {
        index = 1;
    }
    
    // Get the type of the response
    if (ei_get_type(buf->buff, &index, &type, &size) < 0) {
        JsonbValue jbv;
        jbv.type = jbvString;
        jbv.val.string.val = "decode_error";
        jbv.val.string.len = strlen("decode_error");
        return JsonbValueToJsonb(&jbv);
    }
    
    // Handle tuple response from manual RPC
    if (type == ERL_SMALL_TUPLE_EXT || type == ERL_LARGE_TUPLE_EXT) {
        int arity;
        if (ei_decode_tuple_header(buf->buff, &index, &arity) < 0) {
            JsonbValue jbv;
            jbv.type = jbvString;
            jbv.val.string.val = "tuple_decode_error";
            jbv.val.string.len = strlen("tuple_decode_error");
            return JsonbValueToJsonb(&jbv);
        } else if (arity == 2) {
            // This is a 2-tuple response from $gen_call: {Ref, Result}
            // Skip first element (reference)
            ei_skip_term(buf->buff, &index);
            
            // Decode the actual result (second element)
            result = decode_erlang_term_recursive(buf->buff, &index);
            return JsonbValueToJsonb(result);
        } else {
            // This is a regular tuple, decode it as an array
            result = decode_erlang_term_recursive(buf->buff, &index);
            return JsonbValueToJsonb(result);
        }
    } else {
        // Direct response, decode it
        result = decode_erlang_term_recursive(buf->buff, &index);
        return JsonbValueToJsonb(result);
    }
}

// Recursive function to decode any Erlang term into JsonbValue
static JsonbValue *decode_erlang_term_recursive(char *buf, int *index) {
    JsonbValue *jbv;
    char *string_buffer;
    int type, size, arity;
    int i;
    JsonbParseState *state = NULL;
    JsonbValue *result;
    JsonbValue key;
    
    // Allocate in PostgreSQL memory context
    jbv = (JsonbValue *) palloc(sizeof(JsonbValue));
    string_buffer = (char *) palloc(1024);
    
    if (ei_get_type(buf, index, &type, &size) < 0) {
        jbv->type = jbvString;
        jbv->val.string.val = pstrdup("type_error");
        jbv->val.string.len = 10;
        return jbv;
    }
    
    switch (type) {
        case ERL_ATOM_EXT:
        case ERL_SMALL_ATOM_EXT:
        case ERL_ATOM_UTF8_EXT:
        case ERL_SMALL_ATOM_UTF8_EXT:
            if (ei_decode_atom(buf, index, string_buffer) == 0) {
                jbv->type = jbvString;
                jbv->val.string.val = pstrdup(string_buffer);
                jbv->val.string.len = strlen(string_buffer);
            } else {
                jbv->type = jbvString;
                jbv->val.string.val = pstrdup("atom_error");
                jbv->val.string.len = 10;
            }
            return jbv;
            
        case ERL_BINARY_EXT:
            {
                char *binary_data;
                long binary_len;
                if (ei_decode_binary(buf, index, (void**)&binary_data, &binary_len) == 0) {
                    int copy_len = binary_len < 1023 ? binary_len : 1023;
                    memcpy(string_buffer, binary_data, copy_len);
                    string_buffer[copy_len] = '\0';
                    free(binary_data);
                    jbv->type = jbvString;
                    jbv->val.string.val = pstrdup(string_buffer);
                    jbv->val.string.len = copy_len;
                } else {
                    jbv->type = jbvString;
                    jbv->val.string.val = pstrdup("binary_error");
                    jbv->val.string.len = 12;
                }
            }
            return jbv;
            
        case ERL_SMALL_INTEGER_EXT:
        case ERL_INTEGER_EXT:
            {
                long val_long;
                if (ei_decode_long(buf, index, &val_long) == 0) {
                    jbv->type = jbvNumeric;
                    jbv->val.numeric = DatumGetNumeric(DirectFunctionCall1(int8_numeric, Int64GetDatum(val_long)));
                } else {
                    jbv->type = jbvString;
                    jbv->val.string.val = pstrdup("integer_error");
                    jbv->val.string.len = 13;
                }
            }
            return jbv;
            
        case ERL_FLOAT_EXT:
        case NEW_FLOAT_EXT:
            {
                double val_double;
                if (ei_decode_double(buf, index, &val_double) == 0) {
                    jbv->type = jbvNumeric;
                    jbv->val.numeric = DatumGetNumeric(DirectFunctionCall1(float8_numeric, Float8GetDatum(val_double)));
                } else {
                    jbv->type = jbvString;
                    jbv->val.string.val = pstrdup("float_error");
                    jbv->val.string.len = 11;
                }
            }
            return jbv;
            
        case ERL_SMALL_TUPLE_EXT:
        case ERL_LARGE_TUPLE_EXT:
            if (ei_decode_tuple_header(buf, index, &arity) == 0) {
                // Decode tuple as JSON array
                pushJsonbValue(&state, WJB_BEGIN_ARRAY, NULL);
                
                for (i = 0; i < arity; i++) {
                    JsonbValue *elem = decode_erlang_term_recursive(buf, index);
                    pushJsonbValue(&state, WJB_ELEM, elem);
                }
                
                result = pushJsonbValue(&state, WJB_END_ARRAY, NULL);
                return result;
            } else {
                jbv->type = jbvString;
                jbv->val.string.val = pstrdup("tuple_error");
                jbv->val.string.len = 11;
                return jbv;
            }
            
        case ERL_LIST_EXT:
        case ERL_NIL_EXT:
            if (type == ERL_NIL_EXT) {
                // Empty list
                pushJsonbValue(&state, WJB_BEGIN_ARRAY, NULL);
                result = pushJsonbValue(&state, WJB_END_ARRAY, NULL);
                return result;
            } else {
                // Non-empty list
                if (ei_decode_list_header(buf, index, &arity) == 0) {
                    pushJsonbValue(&state, WJB_BEGIN_ARRAY, NULL);
                    
                    for (i = 0; i < arity; i++) {
                        JsonbValue *elem = decode_erlang_term_recursive(buf, index);
                        pushJsonbValue(&state, WJB_ELEM, elem);
                    }
                    
                    // Handle list tail (should be NIL for proper lists)
                    // Skip the tail - we assume proper lists ending with NIL
                    ei_skip_term(buf, index);
                    
                    result = pushJsonbValue(&state, WJB_END_ARRAY, NULL);
                    return result;
                } else {
                    jbv->type = jbvString;
                    jbv->val.string.val = pstrdup("list_error");
                    jbv->val.string.len = 10;
                    return jbv;
                }
            }
            
        case ERL_MAP_EXT:
            if (ei_decode_map_header(buf, index, &arity) == 0) {
                pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);
                
                for (i = 0; i < arity; i++) {
                    // Decode key
                    JsonbValue *key_val = decode_erlang_term_recursive(buf, index);
                    if (key_val->type == jbvString) {
                        pushJsonbValue(&state, WJB_KEY, key_val);
                    } else {
                        // Convert non-string keys to strings
                        snprintf(string_buffer, 1024, "key_%d", i);
                        key.type = jbvString;
                        key.val.string.val = pstrdup(string_buffer);
                        key.val.string.len = strlen(string_buffer);
                        pushJsonbValue(&state, WJB_KEY, &key);
                    }
                    
                    // Decode value
                    JsonbValue *val_val = decode_erlang_term_recursive(buf, index);
                    pushJsonbValue(&state, WJB_VALUE, val_val);
                }
                
                result = pushJsonbValue(&state, WJB_END_OBJECT, NULL);
                return result;
            } else {
                jbv->type = jbvString;
                jbv->val.string.val = pstrdup("map_error");
                jbv->val.string.len = 9;
                return jbv;
            }
            
        default:
            snprintf(string_buffer, 1024, "unsupported_type_%d", type);
            jbv->type = jbvString;
            jbv->val.string.val = pstrdup(string_buffer);
            jbv->val.string.len = strlen(string_buffer);
            return jbv;
    }
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