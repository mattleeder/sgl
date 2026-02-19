#ifndef sql_token
#define sql_token

enum TokenType {
    // Single character tokens
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS, 
    TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR, 
    TOKEN_APOS, TOKEN_DOUBLE_APOS, TOKEN_PERCENT,
    
    // One or two character tokens
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL, TOKEN_EQUAL,

    // Literals
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,

    // Keywords
    TOKEN_SELECT, TOKEN_FROM, TOKEN_WHERE, TOKEN_AND,
    TOKEN_CREATE, TOKEN_TABLE, TOKEN_INTEGER,
    TOKEN_PRIMARY, TOKEN_KEY, TOKEN_TEXT,
    TOKEN_UNIQUE, TOKEN_IF, TOKEN_NOT, TOKEN_EXISTS,
    TOKEN_ON, TOKEN_INDEX,

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