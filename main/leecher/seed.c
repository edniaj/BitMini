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
int handle_peer_connection(int listen_fd)
{
    while (1)
    {
        struct sockaddr_in peer_addr;
        socklen_t addr_len = sizeof(peer_addr);
        int peer_fd = accept(listen_fd, (struct sockaddr *)&peer_addr, &addr_len);
        if (peer_fd < 0)
        {
            perror("ERROR accepting peer");
            return 1;
        }
        printf("New peer connected.\n");

        // handle peer is supposed to be a while loop
        // if it returns 2, then we should break and listen for new peers
        // if it returns 1, then we should return 1 and stop the loop
        int return_status = handle_peer_request(peer_fd);
        
        return return_status;

        // disconnection from peers should return 2 
    }

    return 0;
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

    if (regcomp(&regex, pattern, REG_EXTENDED) != 0) {
        fprintf(stderr, "Failed to compile regex pattern\n");
        return NULL;
    }

    DIR *dir = opendir(STORAGE_DIR);
    if (!dir) {
        perror("ERROR opening storage directory");
        regfree(&regex);
        return NULL;
    }

    char *binary_path = NULL;
    struct dirent *entry;

    // Read each entry in STORAGE_DIR
    while ((entry = readdir(dir)) != NULL) {
        // Skip subdirectories
        if (entry->d_type == DT_DIR) {
            continue;
        }

        // Check if it starts with the desired prefix
        if (regexec(&regex, entry->d_name, 0, NULL, 0) == 0) {
            size_t name_len = strlen(entry->d_name);

            // Skip if ends with ".meta"
            if (name_len > 5 &&
                strcmp(entry->d_name + (name_len - 5), ".meta") == 0) {
                continue;
            }

            // Skip if ends with ".bitfield"
            if (name_len > 9 &&
                strcmp(entry->d_name + (name_len - 9), ".bitfield") == 0) {
                continue;
            }

            // Otherwise, treat it as the binary file
            size_t fullpath_len = strlen(STORAGE_DIR) + name_len + 1;
            binary_path = (char *)malloc(fullpath_len);
            if (!binary_path) {
                fprintf(stderr, "Out of memory allocating path\n");
                break; // will return NULL below
            }
            snprintf(binary_path, fullpath_len, "%s%s", STORAGE_DIR, entry->d_name);
            break;  // We found it; no need to keep looking
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
    if (regcomp(&regex, pattern, REG_EXTENDED) != 0) {
        fprintf(stderr, "Failed to compile regex pattern\n");
        return NULL;
    }

    DIR *dir = opendir(STORAGE_DIR);
    if (!dir) {
        perror("ERROR opening storage directory");
        regfree(&regex);
        return NULL;
    }

    char *bitfield_path = NULL;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            continue;
        }

        // Check if filename starts with the prefix
        if (regexec(&regex, entry->d_name, 0, NULL, 0) == 0) {
            size_t name_len = strlen(entry->d_name);

            // Check if it ends with ".bitfield"
            if (name_len > 9 &&
                strcmp(entry->d_name + name_len - 9, ".bitfield") == 0) {
                
                size_t fullpath_len = strlen(STORAGE_DIR) + name_len + 1;
                bitfield_path = (char *)malloc(fullpath_len);
                if (!bitfield_path) {
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

    return bitfield_path;  // NULL if not found
}

int send_chunk(int sockfd, FILE *data_file_fp, struct FileMetaData *fileMetaData, ssize_t chunkIndex)
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

    chunk->fileID = fileMetaData->fileID;
    chunk->chunkIndex = chunkIndex;
    chunk->totalByte = fread(chunk->chunkData, 1, CHUNK_DATA_SIZE, data_file_fp);

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

int send_bitfield(int sockfd, uint8_t *bitfield, size_t size);

// For interacting with other peers
int handle_peer_request(int client_socketfd)
{
    printf("\n🔄 Starting to handle peer requests on socket %d\n", client_socketfd);
    
    while (1)
    {
        printf("\n📥 Waiting for next peer message...\n");
        
        // 1. First read just the header
        PeerMessageHeader header;
        memset(&header, 0, sizeof(header));
        
        ssize_t nbytes = read(client_socketfd, &header, sizeof(PeerMessageHeader));
        if (nbytes <= 0) {
            
            if (errno == ECONNRESET) {
                printf("Detected ECONNRESET: Peer disconnected abruptly.\n");
                return 2;
            }

            perror("ERROR reading message header from peer");
            return 1;
        }

        if (nbytes == 0) {
            printf("abrupt disconnection from peer");
            return 2;
        }
        printf("✅ Received message header - Type: %d, Body size: %zu\n", header.type, header.bodySize);

        // 2. Now read the body based on bodySize from header
        char *body_buffer = malloc(header.bodySize);
        if (!body_buffer) {
            perror("ERROR allocating body buffer");
            break;
        }
        
        nbytes = read(client_socketfd, body_buffer, header.bodySize);
        if (nbytes <= 0) {
            
            if (errno == ECONNRESET) {
                printf("Detected ECONNRESET: Peer disconnected abruptly.\n");
                return 2;
            }
            free(body_buffer);
            perror("ERROR reading message body from peer");
            return 1;
        }
        
        if (nbytes == 0) {
            printf("abrupt disconnection from peer");
            return 2;
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
                if (!bitfield_path) {
                    printf("❌ Could not find bitfield file for FileID %zd\n", fileID);
                    break;
                }
                printf("✅ Found bitfield file: %s\n", bitfield_path);

                // Create response header
                PeerMessageHeader resp_header;
                memset(&resp_header, 0, sizeof(resp_header));
                resp_header.type = MSG_ACK_REQUEST_BITFIELD;
                resp_header.bodySize = BITFIELD_SIZE;
                
                printf("📤 Sending bitfield response header (type=%d, size=%zu)\n", 
                       resp_header.type, resp_header.bodySize);
                
                // Send header and then bitfield
                if (write(client_socketfd, &resp_header, sizeof(resp_header)) < 0) {
                    perror("ERROR sending bitfield response header");
                    free(bitfield_path);
                    break;
                }
                
                printf("🔄 Preparing to send bitfield data...\n");
                // send_bitfield(client_socketfd, my_bitfield, bitfield_size);
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
                
                // Create response header
                PeerMessageHeader resp_header;
                memset(&resp_header, 0, sizeof(resp_header));
                resp_header.type = MSG_ACK_REQUEST_CHUNK;
                resp_header.bodySize = sizeof(TransferChunk);
                
                printf("📤 Sending chunk response header (type=%d, size=%zu)\n",
                       resp_header.type, resp_header.bodySize);
                
                // Send header and then chunk
                if (write(client_socketfd, &resp_header, sizeof(resp_header)) < 0) {
                    perror("ERROR sending chunk response header");
                    break;
                }
                // send_chunk(client_socketfd, file_fp, metadata, chunk_req->chunkIndex);
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
