#ifndef M65_JSON_H
#define M65_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define M65_JSON_MAX_DEPTH 16U

typedef enum {
    M65_JSON_OBJECT = 1,
    M65_JSON_ARRAY = 2
} M65JsonContainer;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    M65JsonContainer containers[M65_JSON_MAX_DEPTH];
    size_t counts[M65_JSON_MAX_DEPTH];
    bool expecting_value[M65_JSON_MAX_DEPTH];
    size_t depth;
    bool root_written;
    bool failed;
} M65Json;

bool m65_json_init(M65Json *json);
void m65_json_destroy(M65Json *json);
bool m65_json_begin_object(M65Json *json);
bool m65_json_end_object(M65Json *json);
bool m65_json_begin_array(M65Json *json);
bool m65_json_end_array(M65Json *json);
bool m65_json_key(M65Json *json, const char *key);
bool m65_json_string(M65Json *json, const char *value);
bool m65_json_uint(M65Json *json, uint64_t value);
bool m65_json_int(M65Json *json, int64_t value);
bool m65_json_bool(M65Json *json, bool value);
bool m65_json_null(M65Json *json);
bool m65_json_finish(const M65Json *json);
const char *m65_json_data(const M65Json *json);

#endif
