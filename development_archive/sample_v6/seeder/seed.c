#include "seed.h"

int setup_seeder_socket(int port)
{
    int listen_socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socketfd < 0)
    {
        perror("ERROR opening seeder socket");
        return -1;
    }
    int optval = 1;
    setsockopt(listen_socketfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(listen_socketfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR on binding seeder socket");
        close(listen_socketfd);
        return -1;
    }

    if (listen(listen_socketfd, 10) < 0)
    {
        perror("ERROR on listen");
        close(listen_socketfd);
        return -1;
    }
    printf("Seeder listening on port %d for peer connections...\n", port);
    return listen_socketfd;
}
void handle_peer_connection(int listen_fd)
{
    while (1)
    {
        struct sockaddr_in peer_addr;
        socklen_t addr_len = sizeof(peer_addr);
        int peer_fd = accept(listen_fd, (struct sockaddr *)&peer_addr, &addr_len);
        if (peer_fd < 0)
        {
            perror("ERROR accepting peer");
            continue;
        }
        printf("New peer connected.\n");
        handle_peer_request(peer_fd);
        // Serve chunk requests
        // handle_peer_request(peer_fd);
        // closed in handle_peer_request()
    }

    // Cleanup if ever reached
    close(listen_fd);
}

char *find_binary_file_path(ssize_t fileID)
{
    // Convert fileID into a 4-digit prefix
    char prefix[16];
    snprintf(prefix, sizeof(prefix), "%04zd", fileID);

    // Create a regex that matches any filename beginning with that prefix
    // (e.g., "^0036" for fileID=36)
    regex_t regex;
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "^%s", prefix);

    if (regcomp(&regex, pattern, REG_EXTENDED) != 0)
    {
        fprintf(stderr, "Failed to compile regex pattern\n");
        return NULL;
    }

    DIR *dir = opendir(STORAGE_DIR);
    if (!dir)
    {
        perror("ERROR opening storage directory");
        regfree(&regex);
        return NULL;
    }

    char *binary_path = NULL;
    struct dirent *entry;

    // Read each entry in STORAGE_DIR
    while ((entry = readdir(dir)) != NULL)
    {
        // Skip subdirectories
        if (entry->d_type == DT_DIR)
        {
            continue;
        }

        // Check if it starts with the desired prefix
        if (regexec(&regex, entry->d_name, 0, NULL, 0) == 0)
        {
            size_t name_len = strlen(entry->d_name);

            // Skip if ends with ".meta"
            if (name_len > 5 &&
                strcmp(entry->d_name + (name_len - 5), ".meta") == 0)
            {
                continue;
            }

            // Skip if ends with ".bitfield"
            if (name_len > 9 &&
                strcmp(entry->d_name + (name_len - 9), ".bitfield") == 0)
            {
                continue;
            }

            // Otherwise, treat it as the binary file
            size_t fullpath_len = strlen(STORAGE_DIR) + name_len + 1;
            binary_path = (char *)malloc(fullpath_len);
            if (!binary_path)
            {
                fprintf(stderr, "Out of memory allocating path\n");
                break; // will return NULL below
            }
            snprintf(binary_path, fullpath_len, "%s%s", STORAGE_DIR, entry->d_name);
            break; // We found it; no need to keep looking
        }
    }

    closedir(dir);
    regfree(&regex);

    // binary_path will be NULL if nothing matched,
    // or a heap-allocated string containing the full path if we found it.
    printf("binary_path: %s\n", binary_path);
    return binary_path;
}
char *find_bitfield_file_path(ssize_t fileID)
{
    // Create 4-digit prefix
    char prefix[16];
    snprintf(prefix, sizeof(prefix), "%04zd", fileID);

    // Regex pattern to match filenames starting with the prefix
    regex_t regex;
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "^%s", prefix);
    if (regcomp(&regex, pattern, REG_EXTENDED) != 0)
    {
        fprintf(stderr, "Failed to compile regex pattern\n");
        return NULL;
    }

    DIR *dir = opendir(STORAGE_DIR);
    if (!dir)
    {
        perror("ERROR opening storage directory");
        regfree(&regex);
        return NULL;
    }

    char *bitfield_path = NULL;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_DIR)
        {
            continue;
        }

        // Check if filename starts with the prefix
        if (regexec(&regex, entry->d_name, 0, NULL, 0) == 0)
        {
            size_t name_len = strlen(entry->d_name);

            // Check if it ends with ".bitfield"
            if (name_len > 9 &&
                strcmp(entry->d_name + name_len - 9, ".bitfield") == 0)
            {

                size_t fullpath_len = strlen(STORAGE_DIR) + name_len + 1;
                bitfield_path = (char *)malloc(fullpath_len);
                if (!bitfield_path)
                {
                    fprintf(stderr, "Out of memory allocating path\n");
                    break;
                }

                snprintf(bitfield_path, fullpath_len, "%s%s", STORAGE_DIR, entry->d_name);
                break; // Found it
            }
        }
    }

    closedir(dir);
    regfree(&regex);

    return bitfield_path; // NULL if not found
}
char *find_metadata_file_path(ssize_t fileID)
{
    // Create 4-digit prefix
    char prefix[16];
    snprintf(prefix, sizeof(prefix), "%04zd", fileID);

    // Regex pattern to match filenames starting with the prefix
    regex_t regex;
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "^%s", prefix);
    if (regcomp(&regex, pattern, REG_EXTENDED) != 0)
    {
        fprintf(stderr, "Failed to compile regex pattern\n");
        return NULL;
    }

    DIR *dir = opendir(STORAGE_DIR);
    if (!dir)
    {
        perror("ERROR opening storage directory");
        regfree(&regex);
        return NULL;
    }

    char *metadata_path = NULL;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_DIR)
        {
            continue;
        }

        // Check if filename starts with the prefix
        if (regexec(&regex, entry->d_name, 0, NULL, 0) == 0)
        {
            size_t name_len = strlen(entry->d_name);

            // Check if it ends with ".meta"
            if (name_len > 5 &&
                strcmp(entry->d_name + name_len - 5, ".meta") == 0)
            {

                size_t fullpath_len = strlen(STORAGE_DIR) + name_len + 1;
                metadata_path = (char *)malloc(fullpath_len);
                if (!metadata_path)
                {
                    fprintf(stderr, "Out of memory allocating path\n");
                    break;
                }

                snprintf(metadata_path, fullpath_len, "%s%s", STORAGE_DIR, entry->d_name);
                break; // Found it
            }
        }
    }

    closedir(dir);
    regfree(&regex);

    return metadata_path; // NULL if not found
}

int send_chunk(int sockfd, FILE *data_file_fp, ssize_t fileID, ssize_t chunkIndex)
{
    /* Because we need to send an actual TransferChunk, we first fill it out properly. */
    TransferChunk *chunk = malloc(sizeof(TransferChunk));
    if (!chunk)
    {
        perror("ERROR allocating TransferChunk");
        return 1;
    }
    memset(chunk, 0, sizeof(TransferChunk));

    /* Move the file pointer to the correct chunk index. */
    fseek(data_file_fp, chunkIndex * CHUNK_DATA_SIZE, SEEK_SET);

    chunk->fileID = fileID;
    chunk->chunkIndex = chunkIndex;
    chunk->totalByte = fread(chunk->chunkData, 1, CHUNK_DATA_SIZE, data_file_fp);

    /*
        ssize_t fileID;
        ssize_t chunkIndex;
        ssize_t totalByte;
        char chunkData[CHUNK_DATA_SIZE];
        uint8_t chunkHash[32];
    */

    /* If you want to compute the chunk's hash: */
    create_chunkHash(chunk);

    /* Now send the TransferChunk over the socket. */
    ssize_t sent_bytes = write(sockfd, chunk, sizeof(TransferChunk));
    if (sent_bytes < 0)
    {
        perror("ERROR writing TransferChunk to socket");
        free(chunk);
        return 1;
    }

    free(chunk);
    return 0;
}

int send_bitfield(int sockfd, uint8_t *bitfield, size_t size)
{
    // Send the bitfield data
    ssize_t sent_bytes = write(sockfd, bitfield, size);
    if (sent_bytes < 0)
    {
        perror("ERROR sending bitfield data");
        return 1;
    }
    printf("📤 Sent %zd bytes of bitfield data\n", sent_bytes);
    return 0;
}

void handle_peer_request(int client_socketfd)
{
    printf("\n🔄 Starting to handle peer requests on socket %d\n", client_socketfd);

    /* This is cached so that we don't have to keep opening and closing the SAME file when we are seeding*/
    FILE *current_binary_file_fp = NULL;
    ssize_t current_fileID = 0;

    while (1)
    {
        printf("\n📥 Waiting for next peer message...\n");

        // 1. First read just the header
        PeerMessageHeader header;
        memset(&header, 0, sizeof(header));

        ssize_t nbytes = read(client_socketfd, &header, sizeof(PeerMessageHeader));
        if (nbytes <= 0)
        {
            perror("ERROR reading message header from peer");
            break;
        }
        printf("✅ Received message header - Type: %d, Body size: %zu\n", header.type, header.bodySize);

        // 2. Now read the body based on bodySize from header
        char *body_buffer = malloc(header.bodySize);
        if (!body_buffer)
        {
            perror("ERROR allocating body buffer");
            break;
        }

        nbytes = read(client_socketfd, body_buffer, header.bodySize);
        if (nbytes <= 0)
        {
            free(body_buffer);
            perror("ERROR reading message body from peer");
            break;
        }
        printf("✅ Received message body of %zd bytes\n", nbytes);

        // 3. Handle different message types
        switch (header.type)
        {
        case MSG_REQUEST_BITFIELD:
        {
            printf("\n📋 Processing BITFIELD REQUEST\n");
            BitfieldRequest *req = (BitfieldRequest *)body_buffer;
            ssize_t fileID = req->fileID;
            printf("🔍 Looking for bitfield file with FileID: %zd\n", fileID);

            char *bitfield_path = find_bitfield_file_path(fileID);
            if (!bitfield_path)
            {
                printf("❌ Could not find bitfield file for FileID %zd\n", fileID);
                break;
            }
            printf("✅ Found bitfield file: %s\n", bitfield_path);

            // Read bitfield file into buffer and determine size
            FILE *bitfield_fp = fopen(bitfield_path, "rb");
            if (!bitfield_fp)
            {
                perror("ERROR opening bitfield file");
                free(bitfield_path);
                break;
            }

            // Get file size
            fseek(bitfield_fp, 0, SEEK_END);
            size_t bitfield_size = ftell(bitfield_fp);
            fseek(bitfield_fp, 0, SEEK_SET);

            // Allocate buffer and read file
            uint8_t *bitfield_buffer = malloc(bitfield_size);
            if (!bitfield_buffer)
            {
                perror("ERROR allocating bitfield buffer");
                fclose(bitfield_fp);
                free(bitfield_path);
                break;
            }

            size_t bytes_read = fread(bitfield_buffer, 1, bitfield_size, bitfield_fp);
            fclose(bitfield_fp);

            printf("📊 Bitfield contents:\n");
            for (size_t i = 0; i < bitfield_size; i++)
            {
                for (int j = 7; j >= 0; j--)
                {
                    printf("%d", (bitfield_buffer[i] >> j) & 1);
                }
                printf(" ");
                if ((i + 1) % 8 == 0)
                    printf("\n");
            }

            printf("\n");
            if (bytes_read != bitfield_size)
            {
                perror("ERROR reading bitfield file");
                free(bitfield_buffer);
                free(bitfield_path);
                break;
            }

            // Create response header with actual size
            PeerMessageHeader resp_header;
            memset(&resp_header, 0, sizeof(resp_header));
            resp_header.type = MSG_ACK_REQUEST_BITFIELD;
            resp_header.bodySize = bitfield_size;

            printf("📤 Sending bitfield response header (type=%d, size=%zu)\n",
                   resp_header.type, resp_header.bodySize);

            // Send header and then bitfield
            if (write(client_socketfd, &resp_header, sizeof(resp_header)) < 0)
            {
                perror("ERROR sending bitfield response header");
                free(bitfield_buffer);
                free(bitfield_path);
                break;
            }

            printf("🔄 Preparing to send bitfield data of size %zu bytes...\n", bitfield_size);
            send_bitfield(client_socketfd, bitfield_buffer, bitfield_size);

            free(bitfield_buffer);
            free(bitfield_path);
            printf("✅ Bitfield request handled successfully\n");
        }
        break;

        case MSG_REQUEST_CHUNK:
        {
            printf("\n📦 Processing CHUNK REQUEST\n");
            ChunkRequest *chunk_req = (ChunkRequest *)body_buffer;
            printf("🔍 Request for chunk %zd of file %zd\n",
                   chunk_req->chunkIndex, chunk_req->fileID);

            if (chunk_req->fileID != current_fileID)
            {
                current_fileID = chunk_req->fileID;
                current_binary_file_fp = fopen(find_binary_file_path(current_fileID), "rb");
            }

            // Initialize the TransferChunk structure
            TransferChunk chunk;
            memset(&chunk, 0, sizeof(TransferChunk));
            chunk.fileID = chunk_req->fileID;
            chunk.chunkIndex = chunk_req->chunkIndex;

            // Read just the chunk data
            chunk.totalByte = fread(chunk.chunkData, 1, CHUNK_DATA_SIZE, current_binary_file_fp);

            // Optionally compute chunk hash if needed
            create_chunkHash(&chunk);

            // Create response header
            PeerMessageHeader resp_header;
            memset(&resp_header, 0, sizeof(resp_header));
            resp_header.type = MSG_ACK_REQUEST_CHUNK;
            resp_header.bodySize = sizeof(TransferChunk);

            printf("📤 Sending chunk response header (type=%d, size=%zu)\n",
                   resp_header.type, resp_header.bodySize);

            // Send header and then chunk
            if (write(client_socketfd, &resp_header, sizeof(resp_header)) < 0)
            {
                perror("ERROR sending chunk response header");
                break;
            }
            send_chunk(client_socketfd, current_binary_file_fp, current_fileID, chunk_req->chunkIndex);
            printf("✅ Chunk request handled successfully\n");
        }
        break;

        default:
            fprintf(stderr, "❌ Unknown message type: %d\n", header.type);
            break;
        }

        free(body_buffer);
    }

    printf("👋 Closing peer connection on socket %d\n", client_socketfd);
}
