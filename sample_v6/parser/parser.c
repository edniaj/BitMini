#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"

ASTNode* create_node(ASTNodeType type, int subtype, const char* value, TrackerMessageType msgType) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    if (!node) {
        perror("Failed to allocate AST node");
        return NULL;
    }
    node->type = type;
    node->subtype = (union { ActionType action; SubjectType subject; SourceDestType source_dest; ComparatorType comparator; }) { .action = subtype };
    node->value = value ? strdup(value) : NULL;
    node->messageType = msgType;
    node->function = NULL; // LEFT BLANK FOR JD
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    return node;
}

void free_ast(ASTNode* node) {
    if (!node) return;
    free_ast(node->left);
    free_ast(node->right);
    free_ast(node->next);
    if (node->value) free(node->value);
    free(node);
}

typedef struct {
    const char* input;
    size_t pos;
} Tokenizer;

void init_tokenizer(Tokenizer* t, const char* input) {
    t->input = input;
    t->pos = 0;
}

char* next_token(Tokenizer* t) {
    while (t->input[t->pos] && isspace(t->input[t->pos])) t->pos++;
    if (!t->input[t->pos]) return NULL;

    size_t start = t->pos;
    if (t->input[t->pos] == '"') {
        // 4 quoted strings
        t->pos++;
        start = t->pos;
        while (t->input[t->pos] && t->input[t->pos] != '"') t->pos++;
        if (t->input[t->pos] == '"') t->pos++;
    } else {
        // 4 unquoted tokens
        while (t->input[t->pos] && !isspace(t->input[t->pos]) && t->input[t->pos] != '>') t->pos++;
    }

    size_t len = t->pos - start;
    char* token = (char*)malloc(len + 1);
    strncpy(token, t->input + start, len);
    token[len] = '\0';
    return token;
}

// Parse command string
ASTNode* parse_command(const char* command) {
    Tokenizer t;
    init_tokenizer(&t, command);
    
    ASTNode* root = NULL;
    ASTNode* current = NULL;
    char* token;

    // Parse ACTION
    token = next_token(&t);
    if (!token) return NULL;

    ActionType action;
    if (strcasecmp(token, "BLOCK") == 0) action = ACTION_BLOCK;
    else if (strcasecmp(token, "ALLOW") == 0) action = ACTION_ALLOW;
    else if (strcasecmp(token, "REGISTER") == 0) action = ACTION_REGISTER;
    else {
        free(token);
        return NULL; // Invalid
    }
    free(token);

    root = create_node(AST_ACTION, action, NULL, 0);
    current = root;

    // Parse SUBJECT
    token = next_token(&t);
    if (!token) {
        free_ast(root);
        return NULL;
    }

    SubjectType subject;
    if (strcasecmp(token, "FILENAME") == 0) subject = SUBJECT_FILE;
    else if (strcasecmp(token, "CONNECTION") == 0) subject = SUBJECT_CONNECTION;
    else {
        free(token);
        free_ast(root);
        return NULL;
    }
    free(token);

    ASTNode* subject_node = create_node(AST_SUBJECT, subject, NULL, 0);
    current->left = subject_node;

    // Parse PARAMETERS
    token = next_token(&t);
    if (!token || token[0] != '"') {
        if (token) free(token);
        free_ast(root);
        return NULL;
    }
    ASTNode* param_node = create_node(AST_PARAMETERS, 0, token + 1, 0); // Skip quotatiions
    subject_node->left = param_node;
    free(token);

    // Parse SOURCE
    token = next_token(&t);
    if (!token || strcasecmp(token, "FROM") != 0) {
        if (token) free(token);
        free_ast(root);
        return NULL;
    }
    free(token);

    token = next_token(&t);
    if (!token) {
        free_ast(root);
        return NULL;
    }

    SourceDestType source_type;
    char* source_value = next_token(&t);
    if (!source_value) {
        free(token);
        free_ast(root);
        return NULL;
    }
    if (strcasecmp(token, "IP") == 0) source_type = SOURCE_IP;
    else if (strcasecmp(token, "REGION") == 0) source_type = SOURCE_REGION;
    else {
        free(token);
        free(source_value);
        free_ast(root);
        return NULL;
    }
    free(token);

    ASTNode* source_node = create_node(AST_SOURCE, source_type, source_value, 0);
    current->right = source_node;
    free(source_value);

    // Parse DESTINATION
    token = next_token(&t);
    if (!token || strcasecmp(token, "TO") != 0) {
        if (token) free(token);
        free_ast(root);
        return NULL;
    }
    free(token);

    token = next_token(&t);
    if (!token) {
        free_ast(root);
        return NULL;
    }

    SourceDestType dest_type;
    char* dest_value = next_token(&t);
    if (!dest_value) {
        free(token);
        free_ast(root);
        return NULL;
    }
    if (strcasecmp(token, "IP") == 0) dest_type = SOURCE_IP;
    else if (strcasecmp(token, "REGION") == 0) dest_type = SOURCE_REGION;
    else {
        free(token);
        free(dest_value);
        free_ast(root);
        return NULL;
    }
    free(token);

    ASTNode* dest_node = create_node(AST_DESTINATION, dest_type, dest_value, 0);
    source_node->next = dest_node;
    free(dest_value);

    //SPEED TRACKER FOR BANDWIDTH. IF DONT NEED, FEEL FREE TO COMMENT OUT @JD
    token = next_token(&t);
    if (token && strcasecmp(token, "SPEED") == 0) {
        free(token);
        token = next_token(&t); // Should be bigger than
        if (!token || token[0] != '>') {
            if (token) free(token);
            free_ast(root);
            return NULL;
        }
        free(token);
        token = next_token(&t); // Speed value
        if (!token) {
            free_ast(root);
            return NULL;
        }
        ASTNode* comp_node = create_node(AST_COMPARATOR, COMP_SPEED, token, 0);
        dest_node->next = comp_node;
        free(token);
    }

    return root;
}

// Parse TrackerMessage
ASTNode* parse_message(TrackerMessage* message) {
    TrackerMessageType msgType = message->header.type;
    
    switch (msgType) {
        case MSG_REQUEST_ALL_AVAILABLE_SEED:
        case MSG_REQUEST_META_DATA:
        case MSG_REQUEST_SEEDER_BY_FILEID:
            return create_node(AST_ACTION, ACTION_GET, NULL, msgType);
            
        case MSG_REQUEST_CREATE_SEEDER:
        case MSG_REQUEST_DELETE_SEEDER:
        case MSG_REQUEST_PARTICIPATE_SEED_BY_FILEID:
        case MSG_REQUEST_UNPARTICIPATE_SEED:
            return create_node(AST_ACTION, ACTION_REGISTER, NULL, msgType);
            
        case MSG_REQUEST_CREATE_NEW_SEED:
            if (message->header.bodySize != sizeof(FileMetadata)) {
                return create_node(AST_ACTION, ACTION_BLOCK, NULL, msgType);
            }
            return create_node(AST_ACTION, ACTION_REGISTER, NULL, msgType);
            
        case MSG_ACK_CREATE_NEW_SEED:
        case MSG_ACK_PARTICIPATE_SEED_BY_FILEID:
        case MSG_ACK_SEEDER_BY_FILEID:
        case MSG_RESPOND_ERROR:
            return create_node(AST_ACTION, ACTION_BLOCK, NULL, msgType);
            
        default:
            return create_node(AST_ACTION, ACTION_BLOCK, NULL, msgType);
    }
}

void execute_ast(ASTNode* node, int client_socket, void* context) {
    if (!node) return;
    
    if (node->type == AST_ACTION) {
        switch (node->subtype.action) {
            case ACTION_BLOCK: {
                // Find SUBJECT
                ASTNode* subject = node->left;
                if (!subject || subject->type != AST_SUBJECT) return;
                
                // Get filename or connection details
                ASTNode* param = subject->left;
                if (!param || param->type != AST_PARAMETERS) return;
                
                // Get SOURCE and DESTINATION
                ASTNode* source = node->right;
                if (!source || source->type != AST_SOURCE) return;
                
                ASTNode* dest = source->next;
                if (!dest || dest->type != AST_DESTINATION) return;
                
                ASTNode* comp = dest->next;
                
                printf("Executing BLOCK: %s %s from %s (%s) to %s (%s)\n",
                       subject->subtype.subject == SUBJECT_FILE ? "FILENAME" : "CONNECTION",
                       param->value,
                       source->subtype.source_dest == SOURCE_IP ? "IP" : "REGION",
                       source->value,
                       dest->subtype.source_dest == SOURCE_IP ? "IP" : "REGION",
                       dest->value);
                
                // TODO: Implement tracker integration
                break;
            }
            case ACTION_ALLOW:
            case ACTION_REGISTER:
                if (node->function) {
                    node->function(client_socket, context);
                }
                break;
            case ACTION_GET:
                if (node->function) {
                    node->function(client_socket, context);
                }
                break;
            default:
                break;
        }
    }
    
    execute_ast(node->left, client_socket, context);
    execute_ast(node->right, client_socket, context);
    execute_ast(node->next, client_socket, context);
}