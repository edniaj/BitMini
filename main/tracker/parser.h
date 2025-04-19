#ifndef PARSER_H
#define PARSER_H

#include <stdint.h>
#include "tracker.h"

#define MAX_BLOCKED_FILEHASHES 1024
#define MAX_BLOCKED_IPS 1024
#define MAX_BLOCKED_BY_REGION 1024

typedef enum
{
    CHINA,
    RUSSIA,
    IRAN
} Region;

typedef struct
{
    Region region;
    uint8_t filehash[32];
} BlockFileToRegion;

extern char *region_prefix[];
extern char *region_name[];

/* Global tracking arrays */
extern BlockFileToRegion list_blocked_filehash_to_region[2048]; 
extern int list_blocked_filehash_to_region_count;
extern uint8_t list_blocked_filehash[MAX_BLOCKED_FILEHASHES][32];
extern int list_blocked_filehash_count;
extern char list_blocked_ip[MAX_BLOCKED_IPS][64]; 
extern int list_blocked_ip_count;

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

/* Helper functions */
void print_hash_hex(const uint8_t hash[32]);
int hash_equal(const uint8_t a[32], const uint8_t b[32]);
int parse_region(const char *s, Region *out);
int parse_hash(const char *hex, uint8_t out[32]);

/* Action functions */
void block_ip(const char *ip);
void allow_ip(const char *ip);
void block_file_by_region(const uint8_t hash[32], Region region);
void allow_file_by_region(const uint8_t hash[32], Region region);
void block_file(const uint8_t hash[32]);
void allow_file(const uint8_t hash[32]);

/* Query functions */
void get_blocked_file(void);
void get_blocked_filehash_by_region(Region region);
void get_blocked_peer(void);

/* AST functions */
ASTNode *create_node(ASTNodeType type, int subtype, const char *value);
void free_ast(ASTNode *node);
ASTNode *parse_command(const char *command);
void execute_ast(ASTNode *node, int client_socket, void *ctx);

#endif /* PARSER_H */