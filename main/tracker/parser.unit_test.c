#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parser.unit_test.h"

#include <stdio.h>
#include <stdint.h>

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

char *region_prefix[] = {
    "36.", // china
    "5.",  // russia
    "185." // iran
};

/*

Simple parser for the tracker command
Block adds into the list
Allow removes from the list
Get shows the list

*/

BlockFileToRegion list_blocked_filehash_to_region[2048]; // blocks filehash to specific region (for leeching)
int list_blocked_filehash_to_region_count = 0;

uint8_t list_blocked_filehash[MAX_BLOCKED_FILEHASHES][32]; // blocks filehash from seeding and leeching
int list_blocked_filehash_count = 0;

char list_blocked_ip[MAX_BLOCKED_IPS][64]; // block ip from connecting to tracker
int list_blocked_ip_count = 0;

static void print_hash_hex(const uint8_t hash[32])
{
    if (!hash)
    {
        fputs("(null)", stdout);
        return;
    }
    for (int i = 0; i < 32; ++i)
        printf("%02x", hash[i]);
}

void block_file_by_region(const uint8_t hash[32], Region region)
{
    printf("[BLOCK_FILE_BY_REGION] ");
    print_hash_hex(hash);
    printf(" TO REGION %d\n", region);
}

void allow_file_by_region(const uint8_t hash[32], Region region)
{
    printf("[ALLOW_FILE_BY_REGION] ");
    print_hash_hex(hash);
    printf(" TO REGION %d\n", region);
}

void block_file(const uint8_t hash[32])
{
    printf("[BLOCK_FILE] ");
    print_hash_hex(hash);
    printf("\n");
}

void allow_file(const uint8_t hash[32])
{
    printf("[ALLOW_FILE] ");
    print_hash_hex(hash);
    printf("\n");
}

void block_region(Region region)
{
    printf("[BLOCK_REGION] %d\n", region);
    printf("\n");
}
void allow_region(Region region)
{
    printf("[ALLOW_REGION] %d\n", region);
    printf("\n");
}

void get_blocked_file()
{
    printf("[GET_BLOCKED_FILEHASH] ");
    printf("\n");
}

void get_blocked_peer()
{
    printf("[GET_BLOCKED_PEER]\n");
}

void get_blocked_filehash_by_region(Region region)
{
    printf("[GET_BLOCKED_FILEHASH_BY_REGION] %s\n", region_prefix[region]);
}

ASTNode *create_node(ASTNodeType type, int sub, const char *value)
{
    ASTNode *node = malloc(sizeof *node);
    if (!node)
    {
        perror("malloc");
        return NULL;
    }

    node->type = type;
    switch (type)
    {
    case AST_ACTION:
        node->subtype.action = (ActionType)sub;
        break;
    case AST_SUBJECT:
        node->subtype.subject = (SubjectType)sub;
        break;
    case AST_SOURCE:
    case AST_DESTINATION:
        node->subtype.source_dest = (SourceDestType)sub;
        break;
    default:
        break;
    }

    node->value = value ? strdup(value) : NULL;
    node->function = NULL;
    node->left = node->right = node->next = NULL;
    return node;
}

void free_ast(ASTNode *node)
{
    if (!node)
        return;
    free_ast(node->left);
    free_ast(node->right);
    free_ast(node->next);
    if (node->value)
        free(node->value);
    free(node);
}

typedef struct
{
    const char *input;
    size_t pos;
} Tokenizer;

void init_tokenizer(Tokenizer *t, const char *input)
{
    t->input = input;
    t->pos = 0;
}

char *next_token(Tokenizer *t)
{
    while (t->input[t->pos] && isspace(t->input[t->pos]))
        t->pos++;
    if (!t->input[t->pos])
        return NULL;

    size_t start = t->pos;
    if (t->input[t->pos] == '"')
    {

        t->pos++;
        start = t->pos;
        while (t->input[t->pos] && t->input[t->pos] != '"')
            t->pos++;
        if (t->input[t->pos] == '"')
            t->pos++;
    }
    else
    {

        while (t->input[t->pos] && !isspace(t->input[t->pos]) && t->input[t->pos] != '>')
            t->pos++;
    }

    size_t len = t->pos - start;
    char *token = (char *)malloc(len + 1);
    strncpy(token, t->input + start, len);
    token[len] = '\0';
    return token;
}

// Parse command string
ASTNode *parse_command(const char *command)
{
    Tokenizer t;
    init_tokenizer(&t, command);
    char *tok = NULL; /* reusable token pointer            */
    size_t save = 0;  /* “rewind” bookmark                 */

    tok = next_token(&t);
    if (!tok)
        return NULL;

    ActionType action;
    if (!strcasecmp(tok, "BLOCK"))
        action = ACTION_BLOCK;
    else if (!strcasecmp(tok, "ALLOW"))
        action = ACTION_ALLOW;
    else if (!strcasecmp(tok, "GET"))
        action = ACTION_GET;
    else
    {
        free(tok);
        return NULL;
    }
    free(tok);

    ASTNode *root = create_node(AST_ACTION, action, NULL);

    /* Blocked doesnt semantically add value to the parser, so we ignore it */
    if (action == ACTION_GET)
    {
        save = t.pos;
        tok = next_token(&t);
        if (tok && strcasecmp(tok, "BLOCKED") != 0)
        {                 /* not the keyword */
            t.pos = save; /* push back       */
        }
        free(tok);
        tok = NULL;
    }

        tok = next_token(&t);
    if (!tok)
    {
        free_ast(root);
        return NULL;
    }

    SubjectType subject;
    if (!strcasecmp(tok, "FILEHASH") ||
        !strcasecmp(tok, "FILENAME") ||
        !strcasecmp(tok, "FILE"))
        subject = SUBJECT_FILE;
    else if (!strcasecmp(tok, "CONNECTION") ||
             !strcasecmp(tok, "PEER"))
        subject = SUBJECT_CONNECTION;
    else
    {
        free(tok);
        free_ast(root);
        return NULL;
    }
    free(tok);

    ASTNode *subj = create_node(AST_SUBJECT, subject, NULL);
    root->left = subj;


    if (subject == SUBJECT_FILE)
    {
        
        save = t.pos;
        tok = next_token(&t);
        int have_hash = 0;
        if (tok)
        {
            /* For GET, the next lexeme might actually be the keyword TO.
               For BLOCK/ALLOW it must be a hash.  */
            if (action != ACTION_GET ||
                (strcasecmp(tok, "TO") && strcasecmp(tok, "FROM")))
            {
                ASTNode *p = create_node(AST_PARAMETERS, 0, tok);
                subj->left = p;
                have_hash = 1; /* keep 'tok' copied via strdup */
            }
        }
        if (!have_hash)
        { /* not a hash after all → undo */
            t.pos = save;
        }
        free(tok);
        tok = NULL;

        /* ——— optional “TO IP|REGION value” ——— */
        save = t.pos;
        tok = next_token(&t);
        if (tok && !strcasecmp(tok, "TO"))
        {
            free(tok);
            tok = next_token(&t); /* IP | REGION */
            if (!tok)
                goto fail;

            SourceDestType dst_type;
            if (!strcasecmp(tok, "IP"))
                dst_type = SOURCE_IP;
            else if (!strcasecmp(tok, "REGION"))
                dst_type = SOURCE_REGION;
            else
                goto fail;
            free(tok);

            tok = next_token(&t); /* the concrete value */
            if (!tok)
                goto fail;

            ASTNode *dest = create_node(AST_DESTINATION, dst_type, tok);
            /* chain: hash‑param → dest   OR  dest alone           */
            if (subj->left)
                subj->left->next = dest;
            else
                subj->left = dest;
        }
        else
        { /* no TO‑clause, rewind */
            if (tok)
            {
                t.pos = save;
                free(tok);
                tok = NULL;
            }
        }
    }
    else 
    {
        /* optional “FROM IP|REGION value” */
        save = t.pos;
        tok = next_token(&t);
        if (tok && !strcasecmp(tok, "FROM"))
        {
            free(tok);
            tok = next_token(&t); /* IP | REGION */
            if (!tok)
                goto fail;

            SourceDestType src_type;
            if (!strcasecmp(tok, "IP"))
                src_type = SOURCE_IP;
            else if (!strcasecmp(tok, "REGION"))
                src_type = SOURCE_REGION;
            else
                goto fail;
            free(tok);

            tok = next_token(&t); /* the concrete value */
            if (!tok)
                goto fail;

            ASTNode *src = create_node(AST_SOURCE, src_type, tok);
            subj->left = src;
        }
        else
        { /* no FROM‑clause */
            if (tok)
            {
                t.pos = save;
                free(tok);
                tok = NULL;
            }
        }
    }

    return root;

fail:
    free(tok);
    free_ast(root);
    return NULL;
}
static const char *action_str(ActionType a)
{
    switch (a)
    {
    case ACTION_BLOCK:
        return "BLOCK";
    case ACTION_ALLOW:
        return "ALLOW";
    case ACTION_GET:
        return "GET";
    default:
        return "?";
    }
}

static const char *subject_str(SubjectType s)
{
    switch (s)
    {
    case SUBJECT_FILE:
        return "FILE";
    case SUBJECT_CONNECTION:
        return "CONNECTION";
    default:
        return "?";
    }
}

static const char *sd_str(SourceDestType d)
{
    switch (d)
    {
    case SOURCE_IP:
        return "IP";
    case SOURCE_REGION:
        return "REGION";
    default:
        return "?";
    }
}

int parse_region(const char *s, Region *out)
/* case‑insensitive match of "CHINA" / "RUSSIA" / "IRAN"
   returns 1 on success, 0 on unknown                        */
{
    if (!strcasecmp(s, "CHINA"))
        *out = CHINA;
    else if (!strcasecmp(s, "RUSSIA"))
        *out = RUSSIA;
    else if (!strcasecmp(s, "IRAN"))
        *out = IRAN;
    else
        return 0;
    return 1;
}

int parse_hash(const char *hex, uint8_t out[32])
/* 64‑char hex → 32‑byte array ; returns 1 on success */
{
    if (!hex || strlen(hex) != 64)
        return 0;
    for (int i = 0; i < 32; i++)
    {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1)
            return 0;
        out[i] = (uint8_t)v;
    }
    return 1;
}

void execute_ast(ASTNode *node, int sock, void *ctx)
{
    if (!node)
        return;

    if (node->type == AST_ACTION)
    {

        ActionType act = node->subtype.action;
        ASTNode *subj = node->left;                /* SUBJECT            */
        ASTNode *first = subj ? subj->left : NULL; /* PARAM/SRC/DEST */
        SubjectType stype = subj ? subj->subtype.subject : -1;

        /* -------- FILEHASH / FILE / FILENAME ----------------------- */
        if (stype == SUBJECT_FILE)
        {

            ASTNode *param = NULL; /* hash                       */
            ASTNode *dest = NULL;  /* optional TO clause         */

            if (first && first->type == AST_PARAMETERS)
            {
                param = first;
                dest = first->next;
            }
            else if (first && first->type == AST_DESTINATION)
            {
                dest = first; /* hash omitted               */
            }

            /* need the hash for (BLOCK|ALLOW) w/o TO and for GET hash */
            uint8_t hash[32] = {0};
            int have_hash = param && param->value &&
                            parse_hash(param->value, hash);

            /* ----- decide which helper to call -------------------- */
            if (act == ACTION_BLOCK)
            {

                if (dest && dest->subtype.source_dest == SOURCE_REGION)
                {
                    Region r;
                    if (parse_region(dest->value, &r) && have_hash)
                        block_file_by_region(hash, r);
                }
                else if (have_hash)
                {
                    block_file(hash);
                }
            }
            else if (act == ACTION_ALLOW)
            {

                if (dest && dest->subtype.source_dest == SOURCE_REGION)
                {
                    Region r;
                    if (parse_region(dest->value, &r) && have_hash)
                        allow_file_by_region(hash, r);
                }
                else if (have_hash)
                {
                    allow_file(hash);
                }
            }
            else if (act == ACTION_GET)
            {

                if (dest && dest->subtype.source_dest == SOURCE_REGION)
                {
                    Region r;
                    if (parse_region(dest->value, &r))
                    {
                        get_blocked_filehash_by_region(r);
                    }
                }
                else
                {
                    /* plain  GET BLOCKED FILEHASH  (list all)         */
                    get_blocked_file();
                }
            }

            /* -------- CONNECTION / PEER ------------------------------- */
        }
        else if (stype == SUBJECT_CONNECTION)
        {

            ASTNode *src = first; /* may be NULL                     */

            if (act == ACTION_BLOCK || act == ACTION_ALLOW)
            {

                if (src && src->type == AST_SOURCE &&
                    src->subtype.source_dest == SOURCE_REGION)
                {
                    Region r;
                    if (parse_region(src->value, &r))
                    {
                        if (act == ACTION_BLOCK)
                            block_region(r);
                        else
                            allow_region(r);
                    }
                }
                else if (src && src->type == AST_SOURCE &&
                         src->subtype.source_dest == SOURCE_IP)
                {
                    /* you can add  block_ip(src->value)  etc. here    */
                }
            }
            else if (act == ACTION_GET)
            {
                get_blocked_peer();
            }
        }
    }

    /* recurse so that nested action nodes (if any) also execute       */
    execute_ast(node->left, sock, ctx);
    execute_ast(node->right, sock, ctx);
    execute_ast(node->next, sock, ctx);
}

// Unit testing, Comment out when not in use please
int main(void)
{
    const char *tests[] = {
        /* 1  BLOCK file hash  → block_file(hash)              */
        "BLOCK FILEHASH 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef TO REGION RUSSIA",

        /* 2  ALLOW file hash  → allow_file(hash)              */
        "ALLOW FILEHASH fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210 TO REGION RUSSIA",

        /* 3  GET file hash    → block_file(const uint8_t hash[32])    */
        "BLOCK FILEHASH 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",

        /* 4  BLOCK region     → block_region(region)            */
        "BLOCK CONNECTION FROM REGION CHINA",

        /* 5  ALLOW region     → allow_region(region)            */
        "ALLOW CONNECTION FROM REGION IRAN",

        // get_blocked_filehash_by_region()
        "GET BLOCKED FILEHASH TO REGION CHINA",

        // should return all filehash blocked inside the list
        // get_blocked_filehash()
        "GET BLOCKED FILEHASH ",

        // get_blocked_peer()
        "GET BLOCKED PEER", NULL};

    for (int i = 0; tests[i]; ++i)
    {
        ASTNode *root = parse_command(tests[i]);
        if (!root)
        {
            fprintf(stderr, "Parse failed on: %s\n", tests[i]);
            continue;
        }
        execute_ast(root, -1, NULL); /* ‑1 because we don’t use the socket here */
        free_ast(root);
    }
    return 0;
}
