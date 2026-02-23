#ifndef sql_common
#define sql_common

#include <stdbool.h>

struct UnterminatedString {
    const char  *start;
    size_t      len;
};

bool unterminated_string_equals(struct UnterminatedString *a, struct UnterminatedString *b);
void print_unterminated_string_to_stderr(struct UnterminatedString *string);

#endif