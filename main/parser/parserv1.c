#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tracker.h"

typedef enum {
    AST_ALLOW,
    AST_BLOCK,
    AST_REGISTER
} ASTNodeType;

typedef struct ASTNode {
    ASTNodeType type;
    TrackerMessageType messageType; 
    void (*function)();          
    struct ASTNode* left;
    struct ASTNode* right;
} ASTNode;


ASTNode* create_node(ASTNodeType type, TrackerMessageType msgType);
void free_ast(ASTNode* node);
ASTNode* parse_message(TrackerMessage* message);
void execute_ast(ASTNode* node, int client_socket, TrackerMessage* message);


ASTNode* create_node(ASTNodeType type, TrackerMessageType msgType) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    if (!node) {
        perror("Failed to allocate AST node");
        return NULL;
    }
    node->type = type;
    node->messageType = msgType;
    node->function = NULL; //function left blank for JD to add
    node->left = NULL;
    node->right = NULL;
    return node;
}

void free_ast(ASTNode* node) {
    if (!node) return;
    free_ast(node->left);
    free_ast(node->right);
    free(node);
}

ASTNode* parse_message(TrackerMessage* message) {
    TrackerMessageType msgType = message->header.type;
    
    switch (msgType) {
        case MSG_REQUEST_ALL_AVAILABLE_SEED:
        case MSG_REQUEST_META_DATA:
        case MSG_REQUEST_SEEDER_BY_FILEID:
            return create_node(AST_ALLOW, msgType);
            
        case MSG_REQUEST_CREATE_SEEDER:
        case MSG_REQUEST_DELETE_SEEDER:
        case MSG_REQUEST_PARTICIPATE_SEED_BY_FILEID:
        case MSG_REQUEST_UNPARTICIPATE_SEED://seeder state modded.
            return create_node(AST_REGISTER, msgType);
            
        case MSG_REQUEST_CREATE_NEW_SEED:
            if (message->header.bodySize != sizeof(FileMetadata)) {
                return create_node(AST_BLOCK, msgType);
            }
            return create_node(AST_REGISTER, msgType);
            
        case MSG_ACK_CREATE_NEW_SEED:
        case MSG_ACK_PARTICIPATE_SEED_BY_FILEID:
        case MSG_ACK_SEEDER_BY_FILEID:
        case MSG_RESPOND_ERROR:
            //block client from sending these (IP Address wrong)
            return create_node(AST_BLOCK, msgType);
            
        default:
            // Unknown message types are blocked
            return create_node(AST_BLOCK, msgType);
    }
}

// Execute the AST
void execute_ast(ASTNode* node, int client_socket, TrackerMessage* message) {
    if (!node) return;
    
    switch (node->type) {
        case AST_ALLOW:
            // Call the associated function (to be implemented)
            if (node->function) {
                node->function(client_socket, &message->body);
            }
            break;
            
        case AST_BLOCK: {
            // Send error response
            TrackerMessageHeader errHeader = {MSG_RESPOND_ERROR, 0};
            write(client_socket, &errHeader, sizeof(errHeader));
            printf("Blocked invalid message type: %d\n", node->messageType);
            break;
        }
            
        case AST_REGISTER:
            // Verify registration (to be implemented)
            // For now, assume registration is valid and proceed
            if (node->function) {
                node->function(client_socket, &message->body);
            }
            break;
    }
}