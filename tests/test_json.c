#include "test.h"

#include "m65/json.h"

void test_json(void)
{
    M65Json json;
    const char control_string[] = {'a', '"', '\\', '\n', '\t', 1, '\0'};
    EXPECT_TRUE(m65_json_init(&json));
    EXPECT_TRUE(m65_json_begin_object(&json));
    EXPECT_TRUE(m65_json_key(&json, "escaped"));
    EXPECT_TRUE(m65_json_string(&json, control_string));
    EXPECT_TRUE(m65_json_key(&json, "array"));
    EXPECT_TRUE(m65_json_begin_array(&json));
    EXPECT_TRUE(m65_json_uint(&json, 42U));
    EXPECT_TRUE(m65_json_int(&json, -7));
    EXPECT_TRUE(m65_json_bool(&json, true));
    EXPECT_TRUE(m65_json_null(&json));
    EXPECT_TRUE(m65_json_end_array(&json));
    EXPECT_TRUE(m65_json_end_object(&json));
    EXPECT_TRUE(m65_json_finish(&json));
    EXPECT_STREQ(m65_json_data(&json),
                 "{\"escaped\":\"a\\\"\\\\\\n\\t\\u0001\",\"array\":[42,-7,true,null]}");
    m65_json_destroy(&json);

    EXPECT_TRUE(m65_json_init(&json));
    EXPECT_TRUE(m65_json_begin_object(&json));
    EXPECT_TRUE(m65_json_key(&json, "missing"));
    EXPECT_FALSE(m65_json_end_object(&json));
    EXPECT_FALSE(m65_json_finish(&json));
    m65_json_destroy(&json);
}
