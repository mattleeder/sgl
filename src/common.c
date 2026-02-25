#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "common.h"

bool unterminated_string_equals(struct UnterminatedString *a, struct UnterminatedString *b) {
    fprintf(stderr, "unterminated_string_equals: comparing '%.*s' and '%.*s'\n", a->len, a->start, b->len, b->start);
    if (a->len != b->len) {
        return false;
    }

    if (a->start == b->start) {
        return true;
    }

    return (strncmp(a->start, b->start, b->len) == 0);
}

void print_unterminated_string_to_stderr(struct UnterminatedString *string) {
    fprintf(stderr, "%.*s\n", string->len, string->start);
}