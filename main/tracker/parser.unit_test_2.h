#ifndef PARSER_H
#define PARSER_H

#include <stdint.h>
#include "tracker.h"




typedef enum
{
    AST_ACTION,     /* BLOCK / ALLOW  / GET           */
    AST_SUBJECT,    /* FILE / CONNECTION                         */
    AST_PARAMETERS, /* literal hash / IP / region prefix        */
    AST_SOURCE,     /* FROM (IP | REGION)                      */
    AST_DESTINATION /* TO   (IP | REGION)                      */
} ASTNodeType;

typedef enum
{
    ACTION_BLOCK,
    ACTION_ALLOW,
    ACTION_GET
} ActionType;

typedef enum
{
    SUBJECT_FILE,
    SUBJECT_CONNECTION
} SubjectType;
typedef enum
{
    SOURCE_IP,
    SOURCE_REGION
} SourceDestType;

typedef struct ASTNode
{
    ASTNodeType type;
    union
    {
        ActionType action;
        SubjectType subject;
        SourceDestType source_dest;
    } subtype;
    char *value;                         /* may be NULL                    */
    void (*function)(int, void *);       /* optional callback              */
    struct ASTNode *left, *right, *next; /* usual tree / list links        */
} ASTNode;

ASTNode *create_node(ASTNodeType type, int subtype, const char *value);
void free_ast(ASTNode *node);
ASTNode *parse_command(const char *command);
void execute_ast(ASTNode *node, int client_socket, void *ctx);

#endif /* PARSER_H */
