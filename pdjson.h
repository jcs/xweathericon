/*
 * Fetched from https://github.com/skeeto/pdjson 67108d883
 *
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <http://unlicense.org/>
 */

#ifndef PDJSON_H
#define PDJSON_H

#ifndef PDJSON_SYMEXPORT
#   define PDJSON_SYMEXPORT
#endif

#ifdef __cplusplus
extern "C" {
#else
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
    #include <stdbool.h>
#else
    #ifndef bool
        #define bool int
        #define true 1
        #define false 0
    #endif /* bool */
#endif /* __STDC_VERSION__ */
#endif /* __cplusplus */

#include <stdio.h>

enum json_type {
    JSON_ERROR = 1, JSON_DONE,
    JSON_OBJECT, JSON_OBJECT_END, JSON_ARRAY, JSON_ARRAY_END,
    JSON_STRING, JSON_NUMBER, JSON_TRUE, JSON_FALSE, JSON_NULL
};

struct json_allocator {
    void *(*malloc)(size_t);
    void *(*realloc)(void *, size_t);
    void (*free)(void *);
};

typedef int (*json_user_io)(void *user);

typedef struct json_stream json_stream;
typedef struct json_allocator json_allocator;

PDJSON_SYMEXPORT void json_open_buffer(json_stream *json, const void *buffer, size_t size);
PDJSON_SYMEXPORT void json_open_string(json_stream *json, const char *string);
PDJSON_SYMEXPORT void json_open_stream(json_stream *json, FILE *stream);
PDJSON_SYMEXPORT void json_open_user(json_stream *json, json_user_io get, json_user_io peek, void *user);
PDJSON_SYMEXPORT void json_close(json_stream *json);

PDJSON_SYMEXPORT void json_set_allocator(json_stream *json, json_allocator *a);
PDJSON_SYMEXPORT void json_set_streaming(json_stream *json, bool mode);

PDJSON_SYMEXPORT enum json_type json_next(json_stream *json);
PDJSON_SYMEXPORT enum json_type json_peek(json_stream *json);
PDJSON_SYMEXPORT void json_reset(json_stream *json);
PDJSON_SYMEXPORT const char *json_get_string(json_stream *json, size_t *length);
PDJSON_SYMEXPORT double json_get_number(json_stream *json);

PDJSON_SYMEXPORT enum json_type json_skip(json_stream *json);
PDJSON_SYMEXPORT enum json_type json_skip_until(json_stream *json, enum json_type type);

PDJSON_SYMEXPORT size_t json_get_lineno(json_stream *json);
PDJSON_SYMEXPORT size_t json_get_position(json_stream *json);
PDJSON_SYMEXPORT size_t json_get_depth(json_stream *json);
PDJSON_SYMEXPORT enum json_type json_get_context(json_stream *json, size_t *count);
PDJSON_SYMEXPORT const char *json_get_error(json_stream *json);

PDJSON_SYMEXPORT int json_source_get(json_stream *json);
PDJSON_SYMEXPORT int json_source_peek(json_stream *json);
PDJSON_SYMEXPORT bool json_isspace(int c);

/* internal */

struct json_source {
    int (*get)(struct json_source *);
    int (*peek)(struct json_source *);
    size_t position;
    union {
        struct {
            FILE *stream;
        } stream;
        struct {
            const char *buffer;
            size_t length;
        } buffer;
        struct {
            void *ptr;
            json_user_io get;
            json_user_io peek;
        } user;
    } source;
};

struct json_stream {
    size_t lineno;

    struct json_stack *stack;
    size_t stack_top;
    size_t stack_size;
    enum json_type next;
    unsigned flags;

    struct {
        char *string;
        size_t string_fill;
        size_t string_size;
    } data;

    size_t ntokens;

    struct json_source source;
    struct json_allocator alloc;
    char errmsg[128];
};

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif
