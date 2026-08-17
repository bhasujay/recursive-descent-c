#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>

#include "parser.h"

// for debugging: convert TokenType to string
const char* token_name[] = {
    "KW_IF",
    "KW_ELSE",
    "KW_WHILE",
    "KW_RETURN",
    "KW_FOR",
    "KW_PRINT",
    "TYPE_INT",
    "TYPE_FLOAT",
    "TYPE_STRING",
    "PLUS",
    "MINUS",
    "STAR",
    "SLASH",
    "ASSIGN",
    "EQ",
    "NEQ",
    "LT",
    "LTE",
    "GT",
    "GTE",
    "PLUS_ASSIGN",
    "MINUS_ASSIGN",
    "STAR_ASSIGN",
    "SLASH_ASSIGN",
    "INC",
    "DEC",
    "LPAREN",
    "RPAREN",
    "LBRACE",
    "RBRACE",
    "LSQUARE",
    "RSQUARE",
    "SEMICOLON",
    "COMMA",
    "INT_LITERAL",
    "FLOAT_LITERAL",
    "STRING_LITERAL",
    "IDENTIFIER",
    "END_OF_FILE",
    "UNKNOWN"
};

// Keyword map array
KeywordMap keyword_map[] = {
    {"if", KW_IF},
    {"else", KW_ELSE},
    {"while", KW_WHILE},
    {"return", KW_RETURN},
    {"for", KW_FOR},
    {"int", TYPE_INT},
    {"float", TYPE_FLOAT},
    {"string", TYPE_STRING},
    {"print", KW_PRINT},
};

// Data structures
Token* tokens;          // Dynamic array of tokens
Token* current_token;   // Pointer to the current token
Symbol* symbol_table; // Symbol table

int current_token_index = 0; // Index of the current token
int token_count;        // Number of tokens in the array
int symbol_table_size = INITIAL_SYMBOL_TABLE_SIZE; // Current size of the symbol table
int symbol_count = 0;     // Number of symbols in the symbol table

Error* errors;          // Linked list of errors

// Utility functions =====================================================================================================

// For the parser =============================================================

// match the expected token type and advance the token stream
int consume(TokenType expected) {
    if (current_token->type == expected) {
        current_token++;
        current_token_index++;
        return 1; // success
    }
    return 0; // failure
}

// check if the current token matches the expected type without consuming it
int match(TokenType expected) {
    return current_token->type == expected;
}

// match any of the expected token types (for lookahead)
int match_n(TokenType* expected, int count) {
    for (int i = 0; i < count; i++)
        if (current_token->type == expected[i])
            return 1;
    return 0;
}

// match a specific pattern of token types (for lookahead)
int match_pattern(TokenType* pattern, int length) {
    for (int i = 0; i < length; i++)
        if (current_token[i].type != pattern[i])
            return 0;
    return 1;
}

// peek at the current token without consuming it
Token* peek() {
    return current_token;
}

// Check if a token is at the end of a line
int is_end_of_line(Token* token) {
    return token->length < 0; // We marked tokens that are followed by a newline with negative length
}

// Other utility functions ====================================================

// Add a new error to the error list
void add_error(int line, int column, const char* message, ...) {
    static Error* errors_tail = NULL;  // Keep track of the last error
    
    char buffer[512];
    va_list args;
    va_start(args, message);
    vsnprintf(buffer, sizeof(buffer), message, args);

    Error* new_error = (Error*)malloc(sizeof(Error));
    new_error->message = strdup(buffer);
    new_error->line = line;
    new_error->column = column;
    new_error->next = NULL;
    
    // Add to end of list
    if (errors == NULL) {
        errors = new_error;
        errors_tail = new_error;
    } else {
        errors_tail->next = new_error;
        errors_tail = new_error;
    }
}

// Error reporting function
void report_error(Error* error) {
    fprintf(stderr,
        "\x1b[1;31merror\x1b[0m: %s\n"
        "  \x1b[36m--> line %d, column %d\x1b[0m\n",
        error->message, error->line, error->column
    );
}

// Print all errors
void print_errors(const char* filename) {
    Error* current = errors;
    fprintf(stderr, "\x1b[1;33mErrors in file '%s':\x1b[0m\n", filename);
    while (current) {
        report_error(current);
        current = current->next;
    }
}

// Free all errors
void free_errors() {
    Error* current = errors;
    while (current) {
        Error* next = current->next;
        free(current->message);
        free(current);
        current = next;
    }
    errors = NULL;
}

// Free allocated tokens
void free_tokens(int count) {
    for (int i = 0; i < count; i++) {
        if (tokens[i].lexeme) {
            free(tokens[i].lexeme);
        }
    }
    if (tokens) {
        free(tokens);
    }
    tokens = NULL;
}

// hash function for symbol table (djb2 algorithm)
unsigned int hash(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

// Symbol table management ====================================================

// Resize the symbol table when load factor exceeds threshold
int resize_symbol_table() {
    int old_size = symbol_table_size;
    Symbol* old_table = symbol_table;
    
    symbol_table_size *= 2;
    symbol_table = (Symbol*)calloc(symbol_table_size, sizeof(Symbol));
    if (!symbol_table) {
        symbol_table = old_table; // Restore on failure
        symbol_table_size = old_size;
        return -1;
    }
    
    // Rehash all entries from old table
    for (int i = 0; i < old_size; i++) {
        Symbol* current = &old_table[i];
        if (current->identifier != NULL) {
            // Rehash this entry and its chain
            while (current != NULL) {
                int new_index = hash(current->identifier) % symbol_table_size;
                
                if (symbol_table[new_index].identifier == NULL) {
                    symbol_table[new_index].identifier = current->identifier;
                    symbol_table[new_index].type = current->type;
                    symbol_table[new_index].declared_line = current->declared_line;
                    symbol_table[new_index].next = NULL;
                } else {
                    Symbol* new_current = &symbol_table[new_index];
                    while (new_current->next != NULL) new_current = new_current->next;
                    
                    new_current->next = (Symbol*)malloc(sizeof(Symbol));
                    new_current->next->identifier = current->identifier;
                    new_current->next->type = current->type;
                    new_current->next->declared_line = current->declared_line;
                    new_current->next->next = NULL;
                }
                
                Symbol* next = current->next;
                if (current != &old_table[i]) {
                    free(current->identifier);
                    free(current); // Free chained nodes
                }
                current = next;
            }
        }
    }
    
    free(old_table);
    return 0;
}

// Insert a new symbol into the symbol table
int insert_to_table(const char* identifier, SymbolType type, int line) {
    int index = hash(identifier) % symbol_table_size;

    // Check if the symbol already exists in the table
    if (symbol_table[index].identifier != NULL && strcmp(symbol_table[index].identifier, identifier) == 0) {
        add_error(line, 0, "Redeclaration of variable '%s'. Previously declared at line %d.", identifier, symbol_table[index].declared_line);
        return -1; // Indicate error
    }

    if (symbol_table[index].identifier != NULL) {
        Symbol* current = &symbol_table[index];
        while (current->next != NULL) current = current->next;

        current->next = (Symbol*)malloc(sizeof(Symbol));
        current->next->identifier = strdup(identifier);
        current->next->type = type;
        current->next->declared_line = line;
        current->next->val.type = SYM_EMPTY; // Mark as declared but uninitialized
        current->next->next = NULL;
    } else {
        symbol_table[index].identifier = strdup(identifier);
        symbol_table[index].type = type;
        symbol_table[index].declared_line = line;
        symbol_table[index].val.type = SYM_EMPTY; // Mark as declared but uninitialized
        symbol_table[index].next = NULL;
    }

    // update symbol count and check load factor for resizing
    symbol_count++;

    if ((float)symbol_count / symbol_table_size > HASH_THRESHOLD) {
        resize_symbol_table();
    }

    return index; // Return symbol ID or index
}

// Add or update the value of an existing symbol
int add_value(const char* identifier, Value value) {
    int index = hash(identifier) % symbol_table_size;
    Symbol* current = &symbol_table[index];
    while (current != NULL) {
        if (current->identifier != NULL && strcmp(current->identifier, identifier) == 0) {
            if (current->type != value.type) {
                add_error(peek()->line, peek()->column, "Type mismatch for variable '%s'. Expected '%s' but got '%s'.",
                          identifier, 
                          (current->type == SYM_INT) ? "int" : (current->type == SYM_FLOAT) ? "float" : "string",
                          (value.type == SYM_INT) ? "int" : (value.type == SYM_FLOAT) ? "float" : "string");
                return -1; // Type mismatch error
            }
            current->val = value; // Update the value
            return 0; // Success
        }
        current = current->next;
    }
    return -1; // Symbol not found
}

// Check if a symbol exists in the table
int symbol_exists(const char* identifier) {
    int index = hash(identifier) % symbol_table_size;
    Symbol* current = &symbol_table[index];
    while (current != NULL) {
        if (current->identifier != NULL && strcmp(current->identifier, identifier) == 0) {
            return 1; // Symbol exists
        }
        current = current->next;
    }
    return 0; // Symbol not found
}

// Retrieve the value of a symbol
Value get_value(const char* identifier) {
    int index = hash(identifier) % symbol_table_size;
    Symbol* current = &symbol_table[index];
    while (current != NULL) {
        if (current->identifier != NULL && strcmp(current->identifier, identifier) == 0) {
            return current->val; // Return pointer to the value
        }
        current = current->next;
    }
    Value error_val = {0}; // Return an error value (assuming Value is a struct with an int field)
    error_val.type = SYM_UNKNOWN; // Indicate that the symbol was not found
    return error_val;
}

// free the symbol table
void free_symbol_table() {
    if (symbol_table) {
        for (int i = 0; i < symbol_table_size; i++) {
            Symbol* current = &symbol_table[i];
            while (current != NULL) {
                if (current->identifier) {
                    free(current->identifier);
                }
                Symbol* next = current->next;
                if (current != &symbol_table[i]) {
                    free(current); // Free chained nodes
                }
                current = next;
            }
        }
        free(symbol_table);
        symbol_table = NULL;
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lexical analysis ======================================================================================================

// Keyword lookup
TokenType get_keyword_type(const char* lexeme, int len) {
    int num_keywords = sizeof(keyword_map) / sizeof(KeywordMap);
    /* 
    Linear search is used here, 
    A hash map could be more efficient but didn't implement it because of the small number of keywords 
    */
    for (int i = 0; i < num_keywords; i++) {
        if (strlen(keyword_map[i].keyword) == len && 
            strncmp(lexeme, keyword_map[i].keyword, len) == 0) {
            return keyword_map[i].type;
        }
    }
    return IDENTIFIER;
}

// Main tokenization function
int tokenize(const char* input, int show_tokens) {
    int capacity = INITAL_TOKEN_ARRAY_SIZE;
    tokens = (Token*)malloc(sizeof(Token) * capacity);
    if (!tokens) return -1; // Memory error

    const char* p = input;
    int pos = 0;
    int line = 1;
    int column = 1;

    while (*p != '\0') {
        // 1. Skip whitespaces and handle new lines
        if (isspace(*p)) {
            if (*p == '\n') {
                line++;
                column = 1;
                if (pos > 0)
                    tokens[pos-1].length = tokens[pos-1].length * -1; // Mark the previous token as having a newline after it
            } else {
                column++;
            }
            p++;
            continue;
        }

        const char* start = p;
        int start_column = column;
        TokenType type = UNKNOWN;
        int done = 0;
        int skip_token = 0;
        
        // DFA State machine
        while (!done && *p != '\0') {
            char c = *p;
            switch (type) {
                case UNKNOWN: // START state
                    if (isalpha(c) || c == '_') {
                        while (isalnum(*(p + 1)) || *(p + 1) == '_') {
                            p++;
                            column++;
                        }
                        
                        // Check for malformed identifier (identifier followed by a dot)
                        if (*(p + 1) == '.') {
                            // Look ahead to see if there's an alphanumeric character after the dot
                            if (isalnum(*(p + 2)) || *(p + 2) == '_') {
                                // Consume the rest of the malformed identifier
                                p++; // consume the dot
                                column++;
                                while (isalnum(*(p + 1)) || *(p + 1) == '_' || *(p + 1) == '.') {
                                    p++;
                                    column++;
                                }
                                
                                int len = (p - start) + 1;
                                add_error(line, start_column, "Malformed identifier '%.*s'", len, start);
                                skip_token = 1;
                                done = 1;
                                break;
                            }
                        }
                        
                        type = get_keyword_type(start, (p - start) + 1);
                        done = 1;
                    } 
                    else if (isdigit(c)) {
                        int dot_count = 0;
                        while (isdigit(*(p + 1)) || *(p + 1) == '.') {
                            p++;
                            column++;
                            if (*p == '.') {
                                dot_count++;
                                // Check for malformed float (multiple dots or dot not followed by digit)
                                if (dot_count > 1 || !isdigit(*(p + 1))) {
                                    int len = (p - start) + 1;
                                    add_error(line, start_column, "Malformed numeric literal '%.*s'", len, start);
                                    skip_token = 1;
                                    done = 1;
                                    break;
                                }
                            }
                        }
                        
                        type = (dot_count == 1) ? FLOAT_LITERAL : INT_LITERAL;
                        done = 1;
                    } 
                    else if (c == '"') {
                        p++; // skip open quote
                        column++;
                        start = p; // Update start to point after opening quote
                        int string_line = line;
                        int string_column = start_column;
                        
                        while (*p != '"' && *p != '\0') {
                            if (*p == '\n') {
                                line++;
                                column = 1;
                            } else {
                                column++;
                            }
                            p++;
                        }
                        
                        // Check for unclosed string
                        if (*p == '\0') {
                            add_error(string_line, string_column, "Unclosed string literal");
                            skip_token = 1;
                            done = 1;
                            break;
                        }
                        
                        type = STRING_LITERAL;
                        done = 1;
                    }
                    else {
                        // Operator / Delimiter logic
                        switch (c) {
                            case '+': if (*(p+1) == '=') { p++; column++; type = PLUS_ASSIGN; } else if (*(p+1) == '+') { p++; column++; type = INC; } else type = PLUS; break;
                            case '-': if (*(p+1) == '=') { p++; column++; type = MINUS_ASSIGN; } else if (*(p+1) == '-') { p++; column++; type = DEC; } else type = MINUS; break;
                            case '*': if (*(p+1) == '=') { p++; column++; type = STAR_ASSIGN; } else type = STAR; break;
                            case '/': if (*(p+1) == '=') { p++; column++; type = SLASH_ASSIGN; } else type = SLASH; break;
                            case '=': if (*(p+1) == '=') { p++; column++; type = EQ; } else type = ASSIGN; break;
                            case '!': if (*(p+1) == '=') { p++; column++; type = NEQ; } else type = UNKNOWN; break;
                            case '<': if (*(p+1) == '=') { p++; column++; type = LTE; } else type = LT; break;
                            case '>': if (*(p+1) == '=') { p++; column++; type = GTE; } else type = GT; break;
                            case '(': type = LPAREN; break;
                            case ')': type = RPAREN; break;
                            case '{': type = LBRACE; break;
                            case '}': type = RBRACE; break;
                            case '[': type = LSQUARE; break;
                            case ']': type = RSQUARE; break;
                            case ';': type = SEMICOLON; break;
                            case ',': type = COMMA; break;
                            default:  type = UNKNOWN; break;
                        }
                        done = 1;
                    }
                    break;
            }
            p++;
            column++;
        }

        // Skip token if error occurred
        if (skip_token) {
            continue;
        }

        // Check for UNKNOWN token and report error
        if (type == UNKNOWN) {
            int len = p - start;
            add_error(line, start_column, "Unknown symbol '%.*s'", len, start);
            continue;
        }

        // 2. Store the token (Realloc if necessary)
        if (pos >= capacity) {
            capacity *= 2;
            tokens = realloc(tokens, sizeof(Token) * capacity);
        }

        tokens[pos].type = type;
        tokens[pos].line = line;
        tokens[pos].column = start_column;
        // Correctly capture the lexeme
        int len = p - start;
        if (type == STRING_LITERAL) {
            len--; // Exclude closing quote for string literals
        }
        tokens[pos].length = len;
        tokens[pos].lexeme = malloc(len + 1);
        strncpy(tokens[pos].lexeme, start, len);
        tokens[pos].lexeme[len] = '\0';
        
        pos++;
    }

    // Add End of File token
    tokens[pos].type = END_OF_FILE;
    tokens[pos].lexeme = NULL;
    tokens[pos].length = 0;
    tokens[pos].line = line;
    tokens[pos].column = column;

    // Print tokens for debugging
    if (show_tokens) {
        for(int i = 0; i <= pos; i++) {
            if (i == 0) {
                printf("%-15s | %-30s | %6s | %6s |\n", "Token", "Lexeme", "Line", "Column");
                printf("%-15s-+-%-30s-+-%6s-+-%6s--\n",
                "---------------", "------------------------------", "------", "------");
            }
            const char *lex = tokens[i].lexeme ? tokens[i].lexeme : "EOF";
            printf("%-15s | %-30.*s | %6d | %6d |\n",
                token_name[tokens[i].type], 30, lex, tokens[i].line, tokens[i].column);
        }
        printf("%s\n","--------------------------------------------------------------------");
    }
    return pos;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Syntax analysis & Semantic analysis ===================================================================================

/*
    The syntax analysis functions will be implemented as recursive descent parsers for each non-terminal in the grammar.
    The semantic analysis will be integrated into the parsing process, 
        where we will perform type checking, symbol table management, 
        and error reporting as we parse the constructs of the language.
    This will simplify the execution flow for small languages like this,
        as we can directly evaluate expressions and manage the symbol table while parsing, 
        without needing a separate AST construction and traversal phase.

    IMPORTANT NOTE: An LL(k) parser and AST-based approach will be implemented in the next iteration of this project, 
                    but for now we will stick to a more direct approach to keep things 
                    simpler and more manageable within the scope of this assignment.
*/

// Main parsing function
int parse() {
    current_token = &tokens[0]; // current token pointer

    // Initialize symbol table
    symbol_table = (Symbol*)calloc(symbol_table_size, sizeof(Symbol));
    if (!symbol_table) return -1; // Memory error

    // Start parsing from the 'Program' non-terminal
    program();
    return 0;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main function =========================================================================================================

// Read entire file into a string
char* read_file_to_string(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Error opening file");
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* buffer = malloc(length + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    fread(buffer, 1, length, file);
    buffer[length] = '\0';
    fclose(file);
    return buffer;
}

// Entry point
int main(int argc, char* argv[]) {
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: %s <inputfile> [token_flag]\n", argv[0]); // if no input file is provided
        return 1;
    }

    char* input = read_file_to_string(argv[1]);
    if (!input) {
        fprintf(stderr, "Failed to read input file: %s\n", argv[1]);
        return 1;
    }
    
    char* tok_flag = NULL;
    if (argc == 3) {
        tok_flag = strdup(argv[2]);
    }
    
    int show_tokens = 0;
    if (tok_flag && strcmp(tok_flag, "--tokens") == 0) {
        printf("\n\x1b[1;34mTokenization enabled. Tokens will be displayed below:\x1b[0m\n\n");
        show_tokens = 1;
    } else {
        if (tok_flag) free(tok_flag);
        tok_flag = NULL;
    }
    
    token_count = tokenize(input, show_tokens);
    free(input);
    
    // Check for lexical errors
    if (errors != NULL) {
        // fprintf(stderr, "\n\x1b[1;31mLexical analysis failed with errors:\x1b[0m\n\n");
        print_errors(argv[1]);
        
        // Free allocated resources
        if (token_count >= 0) {
            free_tokens(token_count + 1);
        }
        free_errors();
        return 1;
    }
    
    // Proceed to syntax analysis
    // printf("\n\x1b[1;32mLexical and analysis completed successfully!\x1b[0m\n\n");
    
    parse();

    if (errors != NULL) {
        // fprintf(stderr, "\n\x1b[1;31mSyntax analysis and semantic analysis failed with errors:\x1b[0m\n\n");
        print_errors(argv[1]);

        // Free allocated resources
        if (token_count >= 0) {
            free_tokens(token_count + 1);
        }
        return 1;
    } else {
        // printf("\n\x1b[1;32mSyntax analysis and semantic analysis completed successfully!\x1b[0m\n");
    }
    
    // Free allocated resources
    if (token_count >= 0) {
        free_tokens(token_count + 1);
    }
    free_errors();
    free_symbol_table();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Grammar structure definition ==========================================================================================
// Non-terminal symbols are represented as functions 

// Arithmatic Executor
Value solve(Value left, TokenType operator, Value right) {
    Value result;
    result.type = SYM_UNKNOWN;
    
    // If operator and right operand are null/unknown, return left as-is
    if (operator == UNKNOWN && right.type == SYM_UNKNOWN) {
        return left;
    }
    
    // Check if either operand is unknown (error already reported)
    if (left.type == SYM_UNKNOWN || right.type == SYM_UNKNOWN) {
        return result;
    }
    
    // Handle string operations
    if (left.type == SYM_STRING && right.type == SYM_STRING) {
        // String concatenation with + operator
        if (operator == PLUS) {
            result.type = SYM_STRING;
            // Allocate memory for concatenated string
            int left_len = strlen(left.available_value.string_value);
            int right_len = strlen(right.available_value.string_value);
            result.available_value.string_value = (char*)malloc(left_len + right_len + 1);
            if (result.available_value.string_value) {
                strcpy(result.available_value.string_value, left.available_value.string_value);
                strcat(result.available_value.string_value, right.available_value.string_value);
            } else {
                add_error(current_token->line, current_token->column, 
                         "Memory allocation error during string concatenation");
                result.type = SYM_UNKNOWN;
            }
            return result;
        } else {
            add_error(current_token->line, current_token->column, 
                     "Only concatenation (+) is supported for string types");
            return result;
        }
    }
    
    // Mixed string and non-string types are not allowed
    if (left.type == SYM_STRING || right.type == SYM_STRING) {
        add_error(current_token->line, current_token->column, 
                 "Cannot perform operations between string and non-string types");
        return result;
    }
    
    // Determine result type and perform implicit conversion if necessary
    // If either operand is float, result is float
    if (left.type == SYM_FLOAT || right.type == SYM_FLOAT) {
        result.type = SYM_FLOAT;
        
        // Convert operands to float if necessary
        float left_val = (left.type == SYM_FLOAT) ? 
                         left.available_value.float_value : 
                         (float)left.available_value.int_value;
        float right_val = (right.type == SYM_FLOAT) ? 
                          right.available_value.float_value : 
                          (float)right.available_value.int_value;
        
        // Perform the operation
        switch (operator) {
            case PLUS:
                result.available_value.float_value = left_val + right_val;
                break;
            case MINUS:
                result.available_value.float_value = left_val - right_val;
                break;
            case STAR:
                result.available_value.float_value = left_val * right_val;
                break;
            case SLASH:
                if (right_val == 0.0f) {
                    add_error(current_token->line, current_token->column, 
                             "Division by zero");
                    result.type = SYM_UNKNOWN;
                    return result;
                }
                result.available_value.float_value = left_val / right_val;
                break;
            default:
                add_error(current_token->line, current_token->column, 
                         "Invalid operator for arithmetic operation");
                result.type = SYM_UNKNOWN;
                return result;
        }
    } else {
        // Both operands are integers
        result.type = SYM_INT;
        int left_val = left.available_value.int_value;
        int right_val = right.available_value.int_value;
        
        // Perform the operation
        switch (operator) {
            case PLUS:
                result.available_value.int_value = left_val + right_val;
                break;
            case MINUS:
                result.available_value.int_value = left_val - right_val;
                break;
            case STAR:
                result.available_value.int_value = left_val * right_val;
                break;
            case SLASH:
                if (right_val == 0) {
                    add_error(current_token->line, current_token->column, 
                             "Division by zero");
                    result.type = SYM_UNKNOWN;
                    return result;
                }
                result.available_value.int_value = left_val / right_val;
                break;
            default:
                add_error(current_token->line, current_token->column, 
                         "Invalid operator for arithmetic operation");
                result.type = SYM_UNKNOWN;
                return result;
        }
    }
    
    return result;
}

// Program → StmtList
void program() {
    stmt_list();
    consume(END_OF_FILE);
}

// StmtList → Stmt StmtList | ε
void stmt_list() {
    if (match_n((TokenType[]){TYPE_INT, TYPE_FLOAT, TYPE_STRING, KW_PRINT, IDENTIFIER, INT_LITERAL, FLOAT_LITERAL, STRING_LITERAL}, 8)) {
        stmt();
        stmt_list();
    } else if (match(END_OF_FILE)) {
        return; // ε production
    } else {
        Token* tok = peek();
        add_error(tok->line, tok->column, "Unexpected token '%s' in statement list", tok->lexeme ? tok->lexeme : "EOF");
    }
    return;
}

// Stmt → Decl | assign_stmt | PrintStmt | Expr SEMICOLON
void stmt() {
    if (match(KW_PRINT)){
        print_stmt();
    } else if (match_n((TokenType[]){TYPE_INT, TYPE_FLOAT, TYPE_STRING}, 3)){
        decl();
    } else if(match_pattern((TokenType[]){IDENTIFIER, ASSIGN}, 2)){
        assign_stmt();
    } else if (match_n((TokenType[]){IDENTIFIER, INT_LITERAL, FLOAT_LITERAL, STRING_LITERAL}, 4)) {
        expr(); // parse the expression, but we will not use the result since this is an expression statement
        if (match_n((TokenType[]){IDENTIFIER, INT_LITERAL, FLOAT_LITERAL, STRING_LITERAL}, 4)) { // If another term follows the expression, it's an error (missing operator)
            add_error(peek()->line, peek()->column, "Missing operator between '%s' and '%s'", tokens[current_token_index-1].lexeme, peek()->lexeme ? peek()->lexeme : "EOF");
        } else if (!match(SEMICOLON)){
            if (is_end_of_line(&tokens[current_token_index-1])) {
                add_error(tokens[current_token_index-1].line, tokens[current_token_index-1].column - tokens[current_token_index-1].length, "Missing ';' after %s", tokens[current_token_index-1].lexeme);
            } else {
                add_error(peek()->line, peek()->column, "Missing ';' found '%s'", peek()->lexeme ? peek()->lexeme : "EOF");
            }
        } else consume(SEMICOLON); // consume the ';' if it's there
    } else { // If the token doesn't match any valid statement start, report an error
        add_error(peek()->line, peek()->column, "Unexpected token '%s' at the beginning of a statement", peek()->lexeme ? peek()->lexeme : "EOF");
    }
}

// Decl → types IDENTIFIER ASSIGN Expr SEMICOLON | types IDENTIFIER SEMICOLON
void decl() {
    SymbolType type = types(); // parse the type
    char* identifier;
    if (match(IDENTIFIER)) {
        identifier = strdup(peek()->lexeme); // Store the identifier name before consuming it
        insert_to_table(peek()->lexeme, type, peek()->line);
        consume(IDENTIFIER); // consume the identifier
    } else {
        add_error(peek()->line, peek()->column, "Expected identifier in declaration, found '%s'", peek()->lexeme ? peek()->lexeme : "EOF");
        return;
    }
    
    if (match(ASSIGN)) {
        consume(ASSIGN); // consume the '='
        Value expr_result = expr(); // parse the expression
        add_value(identifier, expr_result); 
        if (expr_result.type == SYM_UNKNOWN) {
            add_error(peek()->line, peek()->column, "Invalid expression in declaration");
            free(identifier);
            return;
        }
        if (!match(SEMICOLON)){
            if (is_end_of_line(&tokens[current_token_index-1])) {
                // print last lexeme line and column + length to point to the end of the lexeme in the
                add_error(tokens[current_token_index-1].line, tokens[current_token_index-1].column - tokens[current_token_index-1].length, "Expected ';' after declaration of '%s'", identifier);
            } else {
                add_error(peek()->line, peek()->column, "Expected ';' after declaration of '%s'", identifier);
            }
        } else consume(SEMICOLON); // consume the ';' if it's there
    } else if (consume(SEMICOLON)) {
        free(identifier);
        return; // Declaration without initialization, this is valid
    } else { // If neither ASSIGN nor SEMICOLON is found, it's an error
        add_error(peek()->line, peek()->column, "Expected '=' or ';' after identifier in declaration, found '%s'", peek()->lexeme ? peek()->lexeme : "EOF");
        free(identifier);
        return;
    }   
}

// assign_stmt → IDENTIFIER ASSIGN Expr SEMICOLON
void assign_stmt() {
    char* identifier;
    identifier = strdup(peek()->lexeme); // Store the identifier name before consuming it
    if (!symbol_exists(identifier)) {
        add_error(peek()->line, peek()->column, "Undeclared variable '%s' in assignment statement", peek()->lexeme);
    }
    consume(IDENTIFIER); // consume the identifier
    consume(ASSIGN); // consume the '='
    
    Value expr_result = expr(); // parse the expression
    int res = add_value(identifier, expr_result); // update the symbol table with the value

    if (res == -1) {
        add_error(peek()->line, peek()->column, "Failed to assign value to the variable '%s'", identifier);
    }
    
    if (!match(SEMICOLON)) { 
        if (is_end_of_line(&tokens[current_token_index-1])) {
            add_error(tokens[current_token_index-1].line, tokens[current_token_index-1].column - tokens[current_token_index-1].length, "Expected ';' after assignment statement of '%s'", identifier);
        } else {
            add_error(peek()->line, peek()->column, "Expected ';' after assignment statement of '%s', found '%s'", identifier, peek()->lexeme ? peek()->lexeme : "EOF");
        }
        free(identifier);
    } else {
        free(identifier);
        consume(SEMICOLON); // consume the ';'
    }
}

// PrintStmt → PRINT LPAREN Expr RPAREN SEMICOLON
 void print_stmt() {
    consume(KW_PRINT); // consume 'print'
    char* identifier;
    Value expr_result;
    if (!consume(LPAREN)) { // consume '('
        add_error(peek()->line, peek()->column, "Expected '(' after 'print', found '%s'", peek()->lexeme ? peek()->lexeme : "EOF");
        return;
    }

    expr_result = expr(); // parse the expression inside the parentheses without promise since we want to allow empty print statements (which will just print a newline)

    if (!consume(RPAREN)) { // consume ')'
        add_error(peek()->line, peek()->column, "Expected ')' after expression in print statement, found '%s'", peek()->lexeme ? peek()->lexeme : "EOF");
        return;
    }

    if (!match(SEMICOLON)) { // consume ';'
        if (is_end_of_line(&tokens[current_token_index-1])) {
            add_error(tokens[current_token_index-1].line, tokens[current_token_index-1].column - tokens[current_token_index-1].length, "Expected ';' after print statement");
        } else {
            add_error(peek()->line, peek()->column, "Expected ';' after print statement, found '%s'", peek()->lexeme ? peek()->lexeme : "EOF");
        }
        return;
    } else {
        consume(SEMICOLON);
        // Print the result of the expression based on its type
        switch (expr_result.type) {
            case SYM_INT:
                printf("%d\n", expr_result.available_value.int_value);
                break;
            case SYM_FLOAT:
                printf("%f\n", expr_result.available_value.float_value);
                break;
            case SYM_STRING:
                printf("%s\n", expr_result.available_value.string_value);
                free(expr_result.available_value.string_value); // Free the duplicated string after printing
                break;
            case SYM_EMPTY:
                printf("\n"); // Print a newline for empty print statements
                break;
            case SYM_UNKNOWN:
                add_error(peek()->line-1, peek()->column, "Cannot print expression with unknown type");
                break;
        }
    }
}

// Expr → Term PLUS Expr | Term MINUS Expr | Term 
Value expr() {
    Value term_result = term();
    if (match_n((TokenType[]){PLUS, MINUS}, 2)) {
        TokenType op = peek()->type; // parse the operator
        consume(op); // consume the operator
        Value expr_result = expr(); // parse the next expression
        return solve(term_result, op, expr_result); // perform the operation and get the result
    } else {
        return term_result; // No more addition or subtraction, return the term result
    }
}

// Term → Factor STAR Term | Factor SLASH Term | Factor
Value term() {
    Value factor_result = factor();
    if (match_n((TokenType[]){STAR, SLASH}, 2)) {
        TokenType op = peek()->type; // parse the operator
        consume(op); // consume the operator
        Value term_result = term(); // parse the next term
        return solve(factor_result, op, term_result); // perform the operation and get the result
    } else {
        return factor_result; // No more multiplication or division, return the factor result
    }

}

// factor → LPAREN Expr RPAREN | IDENTIFIER | INT_LITERAL | FLOAT_LITERAL | STRING_LITERAL
Value factor() {
    if (match(LPAREN)) {
        consume(LPAREN);
        Value expr_result = expr();
        if (!consume(RPAREN)) {
            add_error(peek()->line, peek()->column, "Expected ')' after expression in parentheses, found '%s'", peek()->lexeme ? peek()->lexeme : "EOF");
            return (Value){.type = SYM_UNKNOWN}; // Return an error value
        }
        return expr_result;
    } else if (match(IDENTIFIER)) {
        char* identifier;
        if (symbol_exists(peek()->lexeme)) {
            identifier = strdup(peek()->lexeme);
            Value val = get_value(identifier);
            free(identifier);
            consume(IDENTIFIER);
            return val;
        } else {
            add_error(peek()->line, peek()->column, "Undeclared variable '%s' used in expression", peek()->lexeme);
            consume(IDENTIFIER); // Consume the identifier to move forward
            return (Value){.type = SYM_UNKNOWN}; // Return an error value
        }
    } else if (match(INT_LITERAL)) {
        Value val;
        val.type = SYM_INT;
        val.available_value.int_value = atoi(peek()->lexeme);
        consume(INT_LITERAL);
        return val;
    } else if (match(FLOAT_LITERAL)) {
        Value val;
        val.type = SYM_FLOAT;
        val.available_value.float_value = atof(peek()->lexeme);
        consume(FLOAT_LITERAL);
        return val;
    } else if (match(STRING_LITERAL)) {
        Value val;
        val.type = SYM_STRING;
        val.available_value.string_value = strdup(peek()->lexeme); // Duplicate the string literal for safe storage
        consume(STRING_LITERAL);
        return val;
    } else {
        add_error(peek()->line, peek()->column, "Unexpected token '%s' in expression", peek()->lexeme ? peek()->lexeme : "EOF");
        return (Value){.type = SYM_UNKNOWN}; // Return an error value
    }
}

// types → TYPE_INT | TYPE_FLOAT | TYPE_STRING
SymbolType types() {
    if (match(TYPE_INT)) {
        consume(TYPE_INT);
        return SYM_INT;
    } else if (match(TYPE_FLOAT)) {
        consume(TYPE_FLOAT);
        return SYM_FLOAT;
    } else if (match(TYPE_STRING)) {
        consume(TYPE_STRING);
        return SYM_STRING;
    } else {
        add_error(peek()->line, peek()->column, "Expected type specifier, found '%s'", peek()->lexeme ? peek()->lexeme : "EOF");
        return SYM_UNKNOWN; // Return an error type
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////