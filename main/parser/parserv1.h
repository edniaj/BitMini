#ifndef PARSER_H
#define PARSER_H

#include "tracker.h"
#include "seeder.h"

// AST Node Types
typedef enum {
    AST_ALLOW,
    AST_BLOCK,
    AST_REGISTER
} ASTNodeType;

// AST Node Structure
typedef struct ASTNode {
    ASTNodeType type;
    TrackerMessageType messageType;
    void (*function)(int, TrackerMessageBody*); 
    struct ASTNode* left;
    struct ASTNode* right;
} ASTNode;

ASTNode* create_node(ASTNodeType type, TrackerMessageType msgType);
void free_ast(ASTNode* node);
ASTNode* parse_message(TrackerMessage* message);
void execute_ast(ASTNode* node, int client_socket, TrackerMessage* message);

#endif // PARSER_H