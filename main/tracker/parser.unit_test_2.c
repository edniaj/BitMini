#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parser.unit_test_2.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
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

char *region_name[] = {
    "CHINA",
    "RUSSIA",
    "IRAN"
    
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

int list_block_region[3]; // currently we only have 3 regions to block
int list_block_region_count = 0;

/* Helper functions*/

void print_hash_hex(const uint8_t hash[32])
{
    if (!hash)
    {
        fputs("(null)", stdout);
        return;
    }
    for (int i = 0; i < 32; ++i)
        printf("%02x", hash[i]);
}

int hash_equal(const uint8_t a[32], const uint8_t b[32])
{
    return memcmp(a, b, 32) == 0;
}

bool is_region_blocked(Region region)
{
    for (int i = 0; i < list_block_region_count; ++i)
        if (list_block_region[i] == region)
            return true;
    return false;
}

void block_ip(const char *ip)
{
    for (int i = 0; i < list_blocked_ip_count; ++i)
        if (strcmp(list_blocked_ip[i], ip) == 0)
            return;

    if (list_blocked_ip_count >= MAX_BLOCKED_IPS)
        return;
    strncpy(list_blocked_ip[list_blocked_ip_count++], ip, 63);
}

void allow_ip(const char *ip)
{
    for (int i = 0; i < list_blocked_ip_count; ++i)
        if (strcmp(list_blocked_ip[i], ip) == 0)
        {
            list_blocked_ip[i][0] =
                list_blocked_ip[--list_blocked_ip_count][0];
            strcpy(list_blocked_ip[i], list_blocked_ip[list_blocked_ip_count]);
            return;
        }
}

/* Parser functions*/
void block_file_by_region(const uint8_t hash[32], Region region)
{
    /* Check if it is already present */
    for (int i = 0; i < list_blocked_filehash_to_region_count; ++i)
        if (list_blocked_filehash_to_region[i].region == region &&
            hash_equal(list_blocked_filehash_to_region[i].filehash, hash))
        {
            printf("\n--- Block filehash for region: %s ---\n", region_name[region]);
            printf("Hash already exists:\n");
            printf("1. ");
            print_hash_hex(hash);
            printf("\nTotal blocked filehash for region %s : %d\n", region_name[region], list_blocked_filehash_to_region_count);
            printf("\n---- End of list ----\n\n");
            return;
        }

    if (list_blocked_filehash_to_region_count >= 2048)
    {
        fprintf(stderr, "⚠ list_blocked_filehash_to_region full (2048)\n");
        return;
    }

    list_blocked_filehash_to_region[list_blocked_filehash_to_region_count].region = region;
    memcpy(list_blocked_filehash_to_region[list_blocked_filehash_to_region_count].filehash,
           hash, 32);
    list_blocked_filehash_to_region_count++;

    printf("\n--- Added filehash into [blocked list by filehash for %s] ---\n\n", region_name[region]);
    print_hash_hex(hash);
    printf("\n\nTotal blocked filehash for region %s : %d\n", region_name[region], list_blocked_filehash_to_region_count);
    printf("\n---- End of list ----\n\n");
}

void allow_file_by_region(const uint8_t hash[32], Region region)
{
    for (int i = 0; i < list_blocked_filehash_to_region_count; ++i)
        if (list_blocked_filehash_to_region[i].region == region &&
            hash_equal(list_blocked_filehash_to_region[i].filehash, hash))
        {
            list_blocked_filehash_to_region[i] =
                list_blocked_filehash_to_region[--list_blocked_filehash_to_region_count];

            printf("\n--- Allow filehash from region: %s ---\n", region_name[region]);
            printf("Removed:\n1. ");
            print_hash_hex(hash);
            printf("\nRemaining blocked filehash for region %s : %d\n", region_name[region], list_blocked_filehash_to_region_count);
            printf("\n---- End of list ----\n\n");
            return;
        }

    printf("\n--- Allow filehash from region: %s ---\n", region_name[region]);
    printf("Hash not found.\n");
    printf("\n---- End of list ----\n\n");
}
void block_file(const uint8_t hash[32])
{

    // check if the filehash is already in the list - othewrise it becomes buggy since we only remove once
    for (int i = 0; i < list_blocked_filehash_count; ++i)
        if (hash_equal(list_blocked_filehash[i], hash))
        {
            printf("[BLOCK_FILE] already exists → ");
            print_hash_hex(hash);
            putchar('\n');
            return;
        }

    memcpy(list_blocked_filehash[list_blocked_filehash_count++], hash, 32);
    printf("\n--- Add new filehash into the blocked list by filehash ---\n");
    print_hash_hex(hash);
    printf("into the blocked list\n Total files blocked by filehash : ");
    printf("  (total %d)\n", list_blocked_filehash_count);
    printf("\n--- End of list ---\n");
}

void allow_file(const uint8_t hash[32])
{
    printf("\n--- Allow filehash from the blocked list by filehash ---\n");
    for (int i = 0; i < list_blocked_filehash_count; ++i)
        if (hash_equal(list_blocked_filehash[i], hash))
        {
            memcpy(list_blocked_filehash[i],
                   list_blocked_filehash[--list_blocked_filehash_count], 32);

            printf("Removed filehash from blocked list - ");
            print_hash_hex(hash);
            printf("\nUpdated count inside blocked list by filehash (total %d)\n", list_blocked_filehash_count);
            printf("\n--- End of list ---\n\n");
            return;
        }

    printf("[ALLOW_FILE] hash not found → ");
    print_hash_hex(hash);
    printf("\n--- End of list ---\n");
}

void get_blocked_file()
{
    puts("BLOCKED BY FILEHASH LIST : \n");

    for (int i = 0; i < list_blocked_filehash_count; i++)
    {
        printf("%d. ", i + 1);
        print_hash_hex(list_blocked_filehash[i]);
        printf("\n");
    }
    printf("\n--- End of list ---\n\n");
}

void get_blocked_filehash_by_region(Region region)
{
    printf("--- Blocked filehash for region :  %s---\n", region_name[region]);

    int printed = 0;
    for (int i = 0; i < list_blocked_filehash_to_region_count; i++)
    {
        if (list_blocked_filehash_to_region[i].region != region)
            continue;

        printf("%d. ", ++printed);
        print_hash_hex(list_blocked_filehash_to_region[i].filehash);
        printf("\n");
    }
    printf("Total blocked filehash for region %s : %d\n---- End of list ----\n\n", region_name[region], printed);
}

void get_blocked_peer()
{
    puts("[GET_BLOCKED_PEER]");

    for (int i = 0; i < list_blocked_ip_count; i++)
    {
        printf("%d. %s\n", i + 1, list_blocked_ip[i]);
    }
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
        // If you ever have a PARAMETERS or COMPARATOR enum, handle here
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
        // 4 quoted strings
        t->pos++;
        start = t->pos;
        while (t->input[t->pos] && t->input[t->pos] != '"')
            t->pos++;
        if (t->input[t->pos] == '"')
            t->pos++;
    }
    else
    {
        // 4 unquoted tokens
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
    char *tok = NULL;
    size_t save = 0;

    /* ---------- ACTION ------------------------------------------- */
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

    /* consume optional word “BLOCKED” after GET */
    if (action == ACTION_GET)
    {
        save = t.pos;
        tok = next_token(&t);
        if (tok && strcasecmp(tok, "BLOCKED") != 0)
            t.pos = save;
        free(tok);
    }

    /* ---------- SUBJECT ------------------------------------------ */
    tok = next_token(&t);
    if (!tok)
    {
        free_ast(root);
        return NULL;
    }

    /* -- ✱ NEW shorthand: IP / REGION as first word --------------- */
    if (!strcasecmp(tok, "IP") || !strcasecmp(tok, "REGION"))
    {
        SubjectType subject = SUBJECT_CONNECTION;
        SourceDestType src_type = !strcasecmp(tok, "IP") ? SOURCE_IP : SOURCE_REGION;
        free(tok);

        char *val = next_token(&t); /* concrete addr */
        if (!val)
        {
            free_ast(root);
            return NULL;
        }

        ASTNode *subj = create_node(AST_SUBJECT, subject, NULL);
        root->left = subj;
        ASTNode *src = create_node(AST_SOURCE, src_type, val);
        subj->left = src;
        free(val);
        return root; /* → done */
    }

    /* normal subject tokens */
    SubjectType subject;
    if (!strcasecmp(tok, "FILEHASH") ||
        !strcasecmp(tok, "FILENAME") ||
        !strcasecmp(tok, "FILE"))
        subject = SUBJECT_FILE;
    else if (!strcasecmp(tok, "CONNECTION") ||
             !strcasecmp(tok, "PEER")) || !strcasecmp(tok,"IP"))
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

    /* ---------- FILE subject branch ------------------------------ */
    if (subject == SUBJECT_FILE)
    {
        /* optional hash param */
        save = t.pos;
        tok = next_token(&t);
        int have_hash = 0;
        if (tok &&
            (action != ACTION_GET ||
             (strcasecmp(tok, "TO") && strcasecmp(tok, "FROM"))))
        {
            ASTNode *p = create_node(AST_PARAMETERS, 0, tok);
            subj->left = p;
            have_hash = 1;
        }
        else
        {
            t.pos = save;
        }
        free(tok);

        /* optional “TO IP|REGION value” */
        save = t.pos;
        tok = next_token(&t);
        if (tok && !strcasecmp(tok, "TO"))
        {
            free(tok);
            tok = next_token(&t); /* qualifier */
            if (!tok)
                goto fail;

            SourceDestType sd;
            if (!strcasecmp(tok, "IP"))
                sd = SOURCE_IP;
            else if (!strcasecmp(tok, "REGION"))
                sd = SOURCE_REGION;
            else
                goto fail;
            free(tok);

            tok = next_token(&t); /* value */
            if (!tok)
                goto fail;

            ASTNode *dest = create_node(AST_DESTINATION, sd, tok);
            if (subj->left)
                subj->left->next = dest;
            else
                subj->left = dest;
        }
        else if (tok)
        {
            t.pos = save;
            free(tok);
        }
    }
    /* ---------- CONNECTION subject branch ------------------------ */
    else
    {
        /* optional “FROM IP|REGION value” */
        save = t.pos;
        tok = next_token(&t);
        if (tok && !strcasecmp(tok, "FROM"))
        {
            free(tok);
            tok = next_token(&t); /* qualifier */
            if (!tok)
                goto fail;

            SourceDestType sd;
            if (!strcasecmp(tok, "IP"))
                sd = SOURCE_IP;
            else if (!strcasecmp(tok, "REGION"))
                sd = SOURCE_REGION;
            else
                goto fail;
            free(tok);

            tok = next_token(&t); /* value */
            if (!tok)
                goto fail;

            ASTNode *src = create_node(AST_SOURCE, sd, tok);
            subj->left = src;
        }
        else if (tok)
        {
            t.pos = save;
            free(tok);
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
/* ===================================================================
   execute_ast – walks AST and calls the right helper functions
   =================================================================== */
void execute_ast(ASTNode *node, int sock, void *ctx)
{
    if (!node)
        return;

    if (node->type == AST_ACTION)
    {
        ActionType act = node->subtype.action;
        ASTNode *subj = node->left;
        SubjectType stype = subj ? subj->subtype.subject : -1;
        ASTNode *first = subj ? subj->left : NULL; /* param/src/dest */

        /* ------------------------------------------------ FILE ------ */
        if (stype == SUBJECT_FILE)
        {
            ASTNode *param = NULL, *dest = NULL;
            if (first && first->type == AST_PARAMETERS)
            {
                param = first;
                dest = first->next;
            }
            else if (first && first->type == AST_DESTINATION)
            {
                dest = first;
            }

            uint8_t hash[32] = {0};
            int have_hash = param && parse_hash(param->value, hash);

            if (act == ACTION_BLOCK)
            {
                if (dest && dest->subtype.source_dest == SOURCE_REGION)
                {
                    Region r;
                    if (have_hash && parse_region(dest->value, &r))
                        block_file_by_region(hash, r);
                }
                else if (have_hash)
                    block_file(hash);
            }
            else if (act == ACTION_ALLOW)
            {
                if (dest && dest->subtype.source_dest == SOURCE_REGION)
                {
                    Region r;
                    if (have_hash && parse_region(dest->value, &r))
                        allow_file_by_region(hash, r);
                }
                else if (have_hash)
                    allow_file(hash);
            }
            else if (act == ACTION_GET)
            {
                if (dest && dest->subtype.source_dest == SOURCE_REGION)
                {
                    Region r;
                    if (parse_region(dest->value, &r))
                        get_blocked_filehash_by_region(r);
                }
                else
                    get_blocked_file();
            }
        }
        /* ------------------------------------------ CONNECTION ------ */
        else if (stype == SUBJECT_CONNECTION)
        {
            ASTNode *src = first; /* may be NULL */

            if (src && src->type == AST_SOURCE &&
                src->subtype.source_dest == SOURCE_IP)
            {
                if (act == ACTION_BLOCK)
                    block_ip(src->value);
                else if (act == ACTION_ALLOW)
                    allow_ip(src->value);
                else if (act == ACTION_GET)
                    get_blocked_peer();
            }
            else if (src && src->type == AST_SOURCE &&
                     src->subtype.source_dest == SOURCE_REGION)
            {
                Region r;
                if (parse_region(src->value, &r))
                {
                    if (act == ACTION_GET)
                        get_blocked_peer();
                }
            }
            else if (act == ACTION_GET)
                get_blocked_peer();
        }
    }

    /* recurse */
    execute_ast(node->left, sock, ctx);
    execute_ast(node->right, sock, ctx);
    execute_ast(node->next, sock, ctx);
}

// Unit testing, Comment out when not in use please
int main(void)
{
    const char *tests[] = {
        /* 1  BLOCK file hash  → block_file(hash)              */
        "BLOCK FILEHASH 1111156789abcdef0123456789abcdef0123456789abcdef0123456789111111 TO REGION CHINA",
        "BLOCK FILEHASH 2222256789abcdef0123456789abcdef0123456789abcdef0123456789222222 TO REGION CHINA",
        /* 3  GET file hash    → block_file(const uint8_t hash[32])    */
        "BLOCK FILEHASH 3333356789abcdef0123456789abcdef0123456789abcdef0123456789333333",
        "BLOCK FILEHASH 4444446789abcdef0123456789abcdef0123456789abcdef0123456789444444",
        "BLOCK IP 192.168.1.1",
        // get_blocked_filehash_by_region(region) //list_blocked_filehash_to_region
        "GET BLOCKED FILEHASH TO REGION CHINA",
        // get_blocked_filehash() // list_blocked_filehash
        "GET BLOCKED FILEHASH ",
        // get_blocked_peer() list_blocked_ip
        "GET BLOCKED PEER",
        /* 2  ALLOW file hash  → allow_file(hash)              */
        "ALLOW FILEHASH 2222256789abcdef0123456789abcdef0123456789abcdef0123456789222222 TO REGION CHINA",
        "ALLOW FILEHASH 3333356789abcdef0123456789abcdef0123456789abcdef0123456789333333",

        NULL};

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
