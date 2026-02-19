#include "token.h"

const char *token_to_string(enum TokenType token) {
    switch (token) {
        case TOKEN_LEFT_PAREN:      return "TOKEN_LEFT_PAREN";
        case TOKEN_RIGHT_PAREN:     return "TOKEN_RIGHT_PAREN";
        case TOKEN_COMMA:           return "TOKEN_COMMA";
        case TOKEN_DOT:             return "TOKEN_DOT";
        case TOKEN_MINUS:           return "TOKEN_MINUS";
        case TOKEN_PLUS:            return "TOKEN_PLUS";
        case TOKEN_SEMICOLON:       return "TOKEN_SEMICOLON";
        case TOKEN_SLASH:           return "TOKEN_SLASH";
        case TOKEN_STAR:            return "TOKEN_STAR";
        case TOKEN_APOS:            return "TOKEN_APOS";
        case TOKEN_DOUBLE_APOS:     return "TOKEN_DOUBLE_APOS";
        case TOKEN_PERCENT:         return "TOKEN_PERCENT";

        case TOKEN_BANG:            return "TOKEN_BANG";
        case TOKEN_BANG_EQUAL:      return "TOKEN_BANG_EQUAL";
        case TOKEN_GREATER:         return "TOKEN_GREATER";
        case TOKEN_GREATER_EQUAL:   return "TOKEN_GREATER_EQUAL";
        case TOKEN_LESS:            return "TOKEN_LESS";
        case TOKEN_LESS_EQUAL:      return "TOKEN_LESS_EQUAL";
        case TOKEN_EQUAL:           return "TOKEN_EQUAL";

        case TOKEN_IDENTIFIER:      return "TOKEN_IDENTIFIER";
        case TOKEN_STRING:          return "TOKEN_STRING";
        case TOKEN_NUMBER:          return "TOKEN_NUMBER";

        case TOKEN_SELECT:          return "TOKEN_SELECT";
        case TOKEN_FROM:            return "TOKEN_FROM";
        case TOKEN_WHERE:           return "TOKEN_WHERE";
        case TOKEN_AND:             return "TOKEN_AND";
        case TOKEN_CREATE:          return "TOKEN_CREATE";
        case TOKEN_TABLE:           return "TOKEN_TABLE";
        case TOKEN_INTEGER:         return "TOKEN_INTEGER";
        case TOKEN_PRIMARY:         return "TOKEN_PRIMARY";
        case TOKEN_KEY:             return "TOKEN_KEY";
        case TOKEN_TEXT:            return "TOKEN_TEXT";
        case TOKEN_UNIQUE:          return "UNIQUE";
        case TOKEN_IF:              return "IF";
        case TOKEN_NOT:             return "NOT";
        case TOKEN_EXISTS:          return "EXISTS";
        case TOKEN_ON:              return "ON";
        case TOKEN_INDEX:           return "INDEX";

        case TOKEN_ERROR:           return "TOKEN_ERROR";
        case TOKEN_EOF:             return "TOKEN_EOF";

        default:                    return "UNDEFINED TOKEN";
    }
}