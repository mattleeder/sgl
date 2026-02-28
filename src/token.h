#ifndef sql_token
#define sql_token

enum TokenType {
    // Single character tokens
    TOKEN_APOS,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_DOUBLE_APOS,
    TOKEN_LEFT_PAREN,
    TOKEN_MINUS,
    TOKEN_PERCENT,
    TOKEN_PLUS,
    TOKEN_RIGHT_PAREN,
    TOKEN_SEMICOLON,
    TOKEN_SLASH,
    TOKEN_STAR,
    
    // One or two character tokens
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL, TOKEN_EQUAL,

    // Literals
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,

    // Keywords
    TOKEN_AND,
    TOKEN_CREATE,
    TOKEN_EXISTS,
    TOKEN_FROM,
    TOKEN_IF,
    TOKEN_INDEX,
    TOKEN_INTEGER,
    TOKEN_KEY,
    TOKEN_NOT,
    TOKEN_ON,
    TOKEN_PRIMARY,
    TOKEN_SELECT,
    TOKEN_TABLE,
    TOKEN_TEXT,
    TOKEN_UNIQUE,
    TOKEN_WHERE,

    TOKEN_ERROR, TOKEN_EOF
};

struct Token {
    enum TokenType  type;
    const char      *start;
    int             length;
    int             line;
};

struct ReservedWord {
    const char      *word;
    enum TokenType  type;
};

const char *token_to_string(enum TokenType token);

#endif