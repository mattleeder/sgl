#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "common.h"

bool unterminated_string_equals(const struct UnterminatedString *a, const struct UnterminatedString *b) {
    fprintf(stderr, "unterminated_string_equals: comparing '%.*s' and '%.*s'\n", a->len, a->start, b->len, b->start);
    if (a->len != b->len) {
        return false;
    }

    if (a->start == b->start) {
        return true;
    }

    return (memcmp(a->start, b->start, b->len) == 0);
}

bool unterminated_string_less_than(const struct UnterminatedString *a, const struct UnterminatedString *b) {
    size_t length = (a->len <= b->len) ? a->len : b->len;
    int result = strncmp(a->start, b->start, length);
    if (result != 0) return result;
    // If result is 0, strings might be equal but we didnt check full length
    // Return true if a is shorter than b
    return a->len < b->len;
}

bool unterminated_string_greater_than(const struct UnterminatedString *a, const struct UnterminatedString *b) {
    size_t length = (a->len <= b->len) ? a->len : b->len;
    int result = strncmp(a->start, b->start, length);
    if (result != 0) return result;
    // If result is 0, strings might be equal but we didnt check full length
    // Return true if a is longer than b
    return a->len > b->len;
}

void print_unterminated_string_to_stderr(const struct UnterminatedString *string) {
    fprintf(stderr, "%.*s\n", string->len, string->start);
}