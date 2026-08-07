#include "m65/json.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool reserve(M65Json *json, size_t extra)
{
    size_t required;
    size_t new_capacity;
    char *replacement;
    if (json->failed || extra > SIZE_MAX - json->length - 1U) {
        json->failed = true;
        return false;
    }
    required = json->length + extra + 1U;
    if (required <= json->capacity) {
        return true;
    }
    new_capacity = json->capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2U) {
            json->failed = true;
            return false;
        }
        new_capacity *= 2U;
    }
    replacement = (char *)realloc(json->data, new_capacity);
    if (replacement == NULL) {
        json->failed = true;
        return false;
    }
    json->data = replacement;
    json->capacity = new_capacity;
    return true;
}

static bool append_bytes(M65Json *json, const char *data, size_t length)
{
    if (!reserve(json, length)) {
        return false;
    }
    (void)memcpy(&json->data[json->length], data, length);
    json->length += length;
    json->data[json->length] = '\0';
    return true;
}

static bool append_text(M65Json *json, const char *text)
{
    return append_bytes(json, text, strlen(text));
}

static bool append_char(M65Json *json, char value)
{
    return append_bytes(json, &value, 1U);
}

static bool append_escaped(M65Json *json, const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;
    if (!append_char(json, '"')) {
        return false;
    }
    while (*cursor != 0U) {
        char unicode[7];
        switch (*cursor) {
        case '"':
            if (!append_text(json, "\\\"")) {
                return false;
            }
            break;
        case '\\':
            if (!append_text(json, "\\\\")) {
                return false;
            }
            break;
        case '\b':
            if (!append_text(json, "\\b")) {
                return false;
            }
            break;
        case '\f':
            if (!append_text(json, "\\f")) {
                return false;
            }
            break;
        case '\n':
            if (!append_text(json, "\\n")) {
                return false;
            }
            break;
        case '\r':
            if (!append_text(json, "\\r")) {
                return false;
            }
            break;
        case '\t':
            if (!append_text(json, "\\t")) {
                return false;
            }
            break;
        default:
            if (*cursor < 0x20U) {
                (void)snprintf(unicode, sizeof(unicode), "\\u%04x", (unsigned int)*cursor);
                if (!append_text(json, unicode)) {
                    return false;
                }
            } else if (!append_char(json, (char)*cursor)) {
                return false;
            }
            break;
        }
        ++cursor;
    }
    return append_char(json, '"');
}

static bool before_value(M65Json *json)
{
    size_t index;
    if (json->failed) {
        return false;
    }
    if (json->depth == 0U) {
        if (json->root_written) {
            json->failed = true;
            return false;
        }
        json->root_written = true;
        return true;
    }
    index = json->depth - 1U;
    if (json->containers[index] == M65_JSON_OBJECT) {
        if (!json->expecting_value[index]) {
            json->failed = true;
            return false;
        }
        json->expecting_value[index] = false;
        ++json->counts[index];
        return true;
    }
    if (json->counts[index] > 0U && !append_char(json, ',')) {
        return false;
    }
    ++json->counts[index];
    return true;
}

bool m65_json_init(M65Json *json)
{
    if (json == NULL) {
        return false;
    }
    (void)memset(json, 0, sizeof(*json));
    json->capacity = 256U;
    json->data = (char *)malloc(json->capacity);
    if (json->data == NULL) {
        json->failed = true;
        return false;
    }
    json->data[0] = '\0';
    return true;
}

void m65_json_destroy(M65Json *json)
{
    if (json != NULL) {
        free(json->data);
        (void)memset(json, 0, sizeof(*json));
    }
}

static bool begin_container(M65Json *json, M65JsonContainer type, char token)
{
    if (json == NULL || json->depth >= M65_JSON_MAX_DEPTH || !before_value(json) ||
        !append_char(json, token)) {
        if (json != NULL) {
            json->failed = true;
        }
        return false;
    }
    json->containers[json->depth] = type;
    json->counts[json->depth] = 0U;
    json->expecting_value[json->depth] = false;
    ++json->depth;
    return true;
}

bool m65_json_begin_object(M65Json *json)
{
    return begin_container(json, M65_JSON_OBJECT, '{');
}

bool m65_json_begin_array(M65Json *json)
{
    return begin_container(json, M65_JSON_ARRAY, '[');
}

static bool end_container(M65Json *json, M65JsonContainer type, char token)
{
    size_t index;
    if (json == NULL || json->depth == 0U) {
        return false;
    }
    index = json->depth - 1U;
    if (json->containers[index] != type || json->expecting_value[index]) {
        json->failed = true;
        return false;
    }
    --json->depth;
    return append_char(json, token);
}

bool m65_json_end_object(M65Json *json)
{
    return end_container(json, M65_JSON_OBJECT, '}');
}

bool m65_json_end_array(M65Json *json)
{
    return end_container(json, M65_JSON_ARRAY, ']');
}

bool m65_json_key(M65Json *json, const char *key)
{
    size_t index;
    if (json == NULL || key == NULL || json->depth == 0U) {
        return false;
    }
    index = json->depth - 1U;
    if (json->containers[index] != M65_JSON_OBJECT || json->expecting_value[index]) {
        json->failed = true;
        return false;
    }
    if (json->counts[index] > 0U && !append_char(json, ',')) {
        return false;
    }
    if (!append_escaped(json, key) || !append_char(json, ':')) {
        return false;
    }
    json->expecting_value[index] = true;
    return true;
}

bool m65_json_string(M65Json *json, const char *value)
{
    return value != NULL && before_value(json) && append_escaped(json, value);
}

bool m65_json_uint(M65Json *json, uint64_t value)
{
    char number[32];
    if (!before_value(json)) {
        return false;
    }
    (void)snprintf(number, sizeof(number), "%" PRIu64, value);
    return append_text(json, number);
}

bool m65_json_int(M65Json *json, int64_t value)
{
    char number[32];
    if (!before_value(json)) {
        return false;
    }
    (void)snprintf(number, sizeof(number), "%" PRId64, value);
    return append_text(json, number);
}

bool m65_json_bool(M65Json *json, bool value)
{
    return before_value(json) && append_text(json, value ? "true" : "false");
}

bool m65_json_null(M65Json *json)
{
    return before_value(json) && append_text(json, "null");
}

bool m65_json_finish(const M65Json *json)
{
    return json != NULL && !json->failed && json->root_written && json->depth == 0U;
}

const char *m65_json_data(const M65Json *json)
{
    return json != NULL && json->data != NULL ? json->data : "";
}
