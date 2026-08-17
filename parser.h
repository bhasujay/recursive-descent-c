#ifndef PARSER_H
#define PARSER_H

#include <stdio.h>
#include <stdlib.h>

// Constants ==========================================================================================================
#define INITAL_TOKEN_ARRAY_SIZE 128
#define INITIAL_SYMBOL_TABLE_SIZE 32
#define HASH_THRESHOLD 0.75

// Token types enumeration ============================================================================================
typedef enum {
    // Keywords
    KW_IF,
    KW_ELSE,
    KW_WHILE,
    KW_RETURN,
    KW_FOR,
    KW_PRINT,
    // Types
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    // Operators
    PLUS,        // +
    MINUS,       // -
    STAR,        // *
    SLASH,       // /
    ASSIGN,      // =
    EQ,          // ==
    NEQ,         // !=
    LT,          // <
    LTE,         // <=
    GT,          // >
    GTE,         // >=
    PLUS_ASSIGN, // +=
    MINUS_ASSIGN,// -=
    STAR_ASSIGN, // *=
    SLASH_ASSIGN,// /=
    INC,         // ++
    DEC,         // --
    // Delimiters
    LPAREN,      // (
    RPAREN,      // )
    LBRACE,      // {
    RBRACE,      // }
    LSQUARE,     // [
    RSQUARE,     // ]
    SEMICOLON,   // ;
    COMMA,       // ,
    // Literals
    INT_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    // Identifier
    IDENTIFIER,
    // Special
    END_OF_FILE,
    UNKNOWN      // for illegal/unrecognized tokens
} TokenType;

// String representation of token types (for debugging)
extern const char* token_name[];

// Keyword mapping structure ==========================================================================================
typedef struct {
    const char* keyword;
    TokenType type;
} KeywordMap;

extern KeywordMap keyword_map[];

// Token structure definition =========================================================================================
typedef struct {
    TokenType type;
    char* lexeme;
    int length;
    int line;
    int column;
} Token;

// Symbol table structures ============================================================================================
typedef enum {
    SYM_INT,
    SYM_FLOAT,
    SYM_STRING,
    SYM_EMPTY, // For declared but uninitialized variables
    SYM_UNKNOWN
} SymbolType;

typedef struct {
    SymbolType type;
    union symbol_value {
        int int_value;
        float float_value;
        char* string_value;
    } available_value;
} Value;

// Symbol structure
typedef struct Symbol {
    char* identifier;
    SymbolType type;
    Value val;
    int declared_line;
    struct Symbol* next; // For handling collisions in the hash table
} Symbol;

// Error structure for reporting errors
typedef struct Error {
    char* message;
    int line;
    int column;
    struct Error* next;
} Error;

// Function Prototypes ================================================================================================

// Utility / Parser stream helpers
int consume(TokenType expected);
int match(TokenType expected);
int match_n(TokenType* expected, int count);
int match_pattern(TokenType* pattern, int length);
Token* peek(void);
int is_end_of_line(Token* token);

// Error handling functions
void add_error(int line, int column, const char* message, ...);
void report_error(Error* error);
void print_errors(const char* filename);
void free_errors(void);

// Lexical analysis functions
void free_tokens(int count);
TokenType get_keyword_type(const char* lexeme, int len);
int tokenize(const char* input, int show_tokens);

// Symbol table management functions
unsigned int hash(const char* str);
int resize_symbol_table(void);
int insert_to_table(const char* identifier, SymbolType type, int line);
int add_value(const char* identifier, Value value);
int symbol_exists(const char* identifier);
Value get_value(const char* identifier);
void free_symbol_table(void);

// Grammar and semantic analysis functions
Value solve(Value left, TokenType operator, Value right);
void program(void);
void stmt_list(void);
void stmt(void);
void decl(void);
void assign_stmt(void);
void print_stmt(void);
Value expr(void);
Value term(void);
Value factor(void);
SymbolType types(void);
int parse(void);

// File helper
char* read_file_to_string(const char* filename);

#endif // PARSER_H
