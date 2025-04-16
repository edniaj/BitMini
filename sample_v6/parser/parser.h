#ifndef PARSER_H
#define PARSER_H

#include "tracker.h"

typedef enum {
    AST_ACTION,
    AST_SUBJECT,
    AST_SOURCE,
    AST_DESTINATION,
    AST_PARAMETERS,
    AST_COMPARATOR
} ASTNodeType;

// Actions
typedef enum {
    ACTION_BLOCK,
    ACTION_ALLOW,
    ACTION_REGISTER,
    ACTION_GET,
    ACTION_FILTER,
    ACTION_LIMIT
} ActionType;

// Subjects
typedef enum {
    SUBJECT_CONNECTION,
    SUBJECT_FILE,
    SUBJECT_CONTENT
} SubjectType;

// Source
typedef enum {
    SOURCE_IP,
    SOURCE_REGION
} SourceDestType;

// Comparators
typedef enum {
    COMP_SPEED,
    COMP_NONE
} ComparatorType;

// AST Struct
typedef struct ASTNode {
    ASTNodeType type;
    union {
        ActionType action;
        SubjectType subject;
        SourceDestType source_dest;
        ComparatorType comparator;
    } subtype;
    char* value; // PARAMETERS (e.g., filename), SOURCE/DESTINATION (e.g., IP, region)
    TrackerMessageType messageType; //message from tracker here
    void (*function)(int, void*); // Function pointer for execution
    struct ASTNode* left;  // Child 
    struct ASTNode* right; // Child 
    struct ASTNode* next;  // Sibling 
} ASTNode;

// Function prototypes
ASTNode* create_node(ASTNodeType type, int subtype, const char* value, TrackerMessageType msgType);
void free_ast(ASTNode* node);
ASTNode* parse_command(const char* command);
ASTNode* parse_message(TrackerMessage* message);
void execute_ast(ASTNode* node, int client_socket, void* context);

#endif // PARSER_H