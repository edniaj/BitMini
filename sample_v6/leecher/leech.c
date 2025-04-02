#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include "peerCommunication.h"
#include "leech.h"
#include "meta.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#define STORAGE_DIR "./storage_downloads/"
#define BITFIELD_SIZE 1024

int request_chunk(int sockfd, ssize_t fileID, ssize_t chunkIndex, TransferChunk *outChunk);
uint8_t *request_bitfield(int sockfd, ssize_t fileID);

uint8_t *request_bitfield(int sockfd, ssize_t fileID)
{
    /* We are requesting the seeder's bitfield file because we allow partial seeding
     */

    // 1) Send the header for "request bitfield"
    PeerMessageHeader header;
    printf("📋 Requesting bitfield for fileID: %zd\n", fileID);
    memset(&header, 0, sizeof(header));
    header.type = MSG_REQUEST_BITFIELD;
    header.bodySize = sizeof(BitfieldRequest);

    // Write the complete header in one go
    if (write(sockfd, &header, sizeof(PeerMessageHeader)) != sizeof(PeerMessageHeader))
    {
        perror("ERROR writing bitfield request header to server");
        return NULL;
    }
    printf("✅ Sent bitfield request header to server\n");

    // Create and send the body
    PeerMessageBody body;
    memset(&body, 0, sizeof(PeerMessageBody)); // Changed to clear entire body
    body.bitfieldRequest.fileID = fileID;

    // Write the BitfieldRequest portion of the body
    if (write(sockfd, &body.bitfieldRequest, sizeof(BitfieldRequest)) != sizeof(BitfieldRequest))
    {
        perror("ERROR writing bitfield request body to server");
        return NULL;
    }

    // 2) Read the server's response (which includes type, fileID, etc.)
    PeerMessageHeader responseHeader;
    memset(&responseHeader, 0, sizeof(responseHeader));
    ssize_t nbytes = read(sockfd, &responseHeader, sizeof(PeerMessageHeader));
    if (nbytes <= 0)
    {
        perror("ERROR reading bitfield response header from server");
        return NULL;
    }

    if (responseHeader.type != MSG_ACK_REQUEST_BITFIELD)
    {
        fprintf(stderr, "Expected MSG_ACK_REQUEST_BITFIELD, got %d\n", responseHeader.type);
        return NULL;
    }

    // 3) Now read the actual bitfield data from server:
    // Allocate memory based on responseHeader.bodySize
    uint8_t *bitfield = malloc(responseHeader.bodySize); /* bodySize should indicate the total bytes representation of the bitfield. Not the number of bits. We just want the bitfield*/
    if (!bitfield)
    {
        perror("ERROR allocating bitfield memory");
        return NULL;
    }

    nbytes = read(sockfd, bitfield, responseHeader.bodySize);
    if (nbytes <= 0)
    {
        perror("ERROR reading bitfield response body from server");
        free(bitfield);
        return NULL;
    }

    return bitfield;
}

int request_chunk(int sockfd, ssize_t fileID, ssize_t chunkIndex, TransferChunk *outChunk)
{
    // 1) Create and send the header.
    PeerMessageHeader header;
    memset(&header, 0, sizeof(header));
    header.type = MSG_REQUEST_CHUNK;
    header.bodySize = sizeof(ChunkRequest);

    if (write(sockfd, &header, sizeof(header)) < 0)
    {
        perror("ERROR writing chunk request header to server");
        return -1;
    }

    // 2) Send the 'ChunkRequest' struct
    PeerMessageBody body;
    memset(&body, 0, sizeof(PeerMessageBody));
    body.chunkRequest.chunkIndex = chunkIndex;
    body.chunkRequest.fileID = fileID;

    if (write(sockfd, &body.chunkRequest, sizeof(ChunkRequest)) < 0)
    {
        perror("ERROR writing chunk request body to server");
        return -1;
    }

    // 3) Read back the TransferChunk
    // First read the response header to check message type
    PeerMessageHeader responseHeader;
    memset(&responseHeader, 0, sizeof(responseHeader));
    ssize_t nbytes = read(sockfd, &responseHeader, sizeof(PeerMessageHeader));
    if (nbytes <= 0)
    {
        perror("ERROR reading chunk response header from server");
        return -1;
    }

    if (responseHeader.type != MSG_ACK_SEND_CHUNK)
    {
        fprintf(stderr, "Expected MSG_ACK_SEND_CHUNK, got %d\n", responseHeader.type);
        return -1;
    }
    /* Write the body */
    PeerMessageBody responseBody;
    memset(&responseBody, 0, sizeof(PeerMessageBody));
    nbytes = read(sockfd, &responseBody, sizeof(PeerMessageBody));
    if (nbytes <= 0)
    {
        perror("ERROR reading chunk response body from server");
        return -1;
    }

    // Copy the chunk data to output parameter
    memcpy(outChunk, &responseBody.transferChunk, sizeof(TransferChunk));

    // (Optional) Validate the chunk hash or do any checks here.

    return 0;
}

static int connect_to_seeder(PeerInfo *seeder)
{
    printf("Connecting to Seeder at %s:%s...\n", seeder->ip_address, seeder->port);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("ERROR opening socket to Seeder");
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(seeder->port));

    if (inet_pton(AF_INET, seeder->ip_address, &serv_addr.sin_addr) <= 0)
    {
        perror("ERROR invalid tracker IP");
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERROR connecting to tracker");
        printf("❌ Connection to Tracker failed. Is it running at %s:%s?\n",
               seeder->ip_address, seeder->port);
        close(sockfd);
        return -1;
    }

    printf("✅ Seeder successfully connected to Tracker at %s:%s\n",
           seeder->ip_address, seeder->port);
    return sockfd;
}

void print_bitfield(const uint8_t *bitfield, size_t bitfield_size, const char *label)
{
    printf("\n%s (showing first 32 bits):\n", label);
    printf("Bits: ");
    // Print first 32 bits for visualization
    for (size_t i = 0; i < 4 && i < bitfield_size; i++)
    { // 4 bytes = 32 bits
        for (int bit = 7; bit >= 0; bit--)
        {
            printf("%d", (bitfield[i] >> bit) & 1);
            if (bit % 4 == 0)
                printf(" "); // Add space every 4 bits for readability
        }
    }
    printf("\n");
}

void leech_from_seeder(PeerInfo seeder, char *bitfield_filepath, char *binary_filepath, ssize_t totalChunk, ssize_t fileID)
{
    printf("\n🔄 Starting to leech from seeder %s:%s\n", seeder.ip_address, seeder.port);

    int seeder_fd = connect_to_seeder(&seeder);
    // Calculate proper bitfield size in bytes
    size_t bitfield_size = (totalChunk + 7) / 8;
    if (seeder_fd < 0)
    {
        perror("ERROR connecting to seeder");
        return;
    }

    printf("📖 Reading local bitfield from: %s\n", bitfield_filepath);
    FILE *local_bitfield_fp = fopen(bitfield_filepath, "rb");
    if (!local_bitfield_fp)
    {
        perror("ERROR opening bitfield file");
        close(seeder_fd);
        return;
    }

    // Read local bitfield
    uint8_t *local_bitfield = malloc(bitfield_size);
    if (!local_bitfield)
    {
        perror("ERROR allocating memory for local bitfield");
        fclose(local_bitfield_fp);
        close(seeder_fd);
        return;
    }

    memset(local_bitfield, 0, bitfield_size);
    if (fread(local_bitfield, 1, bitfield_size, local_bitfield_fp) <= 0)
    {
        perror("ERROR reading local bitfield");
        fclose(local_bitfield_fp);
        free(local_bitfield);
        close(seeder_fd);
        return;
    }
    fclose(local_bitfield_fp);
    print_bitfield(local_bitfield, bitfield_size, "💾 Local bitfield");

    printf("🔍 Analyzing which chunks to request...\n");
    uint8_t *seeder_bitfield = request_bitfield(seeder_fd, fileID);
    print_bitfield(seeder_bitfield, bitfield_size, " Seeder's bitfield");
    // Loop through each chunk index
    for (ssize_t chunkIndex = 0; chunkIndex < totalChunk; chunkIndex++)
    {

        if (!seeder_bitfield)
        {
            perror("ERROR getting seeder bitfield");
            close(seeder_fd);
            return;
        }
        printf("✅ Successfully received seeder's bitfield\n");

        // Check if local bit is 0 (don't have chunk) and seeder bit is 1 (has chunk)
        if (!has_chunk(local_bitfield, chunkIndex) &&
            has_chunk(seeder_bitfield, chunkIndex))
        {
            printf("📥 Requesting chunk %zd from seeder\n", chunkIndex);

            // Request the chunk from the seeder
            int i = request_chunk(seeder_fd, fileID, chunkIndex, NULL);
            // if (chunk)
            // {
            //     printf("✅ Received chunk %zd (%zu bytes)\n", chunkIndex, chunk->totalByte);

            //     // Write the chunk data to our local file
            //     if (write_chunk_to_file(binary_filepath, chunk))
            //     {
            //         fprintf(stderr, "❌ Failed to write chunk %zd to file\n", chunkIndex);
            //     }
            //     else
            //     {
            //         // Update our local bitfield to mark that we now have this chunk
            //         set_chunk(local_bitfield, chunkIndex);

            //         printf("🧩 Progress: %d/%ld chunks (%d%%)\n",
            //                chunkIndex + 1, totalChunk,
            //                ((chunkIndex + 1) * 100) / totalChunk);
            //     }
            //     free(chunk);
        }
        else
        {
            fprintf(stderr, "❌ Failed to get chunk %zd from seeder\n", chunkIndex);
        }
    }
    printf("🏁 Finished leeching session with seeder %s:%s\n", seeder.ip_address, seeder.port);
    free(local_bitfield);
    free(seeder_bitfield);
    close(seeder_fd);
}

int leeching(PeerInfo *seeder_list, size_t num_seeders, char *metadata_filepath, char *bitfield_filepath, char *binary_filepath)
{
    printf("\n📦 Starting leeching process\n");
    printf("📄 Metadata file: %s\n", metadata_filepath);
    printf("🔑 Bitfield file: %s\n", bitfield_filepath);
    printf("💾 Binary file: %s\n", binary_filepath);
    printf("👥 Number of seeders: %zu\n\n", num_seeders);

    int index = 0;
    FileMetadata *fileMetaData = malloc(sizeof(FileMetadata));

    read_metadata(metadata_filepath, fileMetaData);
    printf("📊 File metadata loaded - Total chunks: %zd, FileID: %zd\n",
           fileMetaData->totalChunk, fileMetaData->fileID);

    // Read bitfield from the bitfield_filepath

    // We should write a binary file to the disk based on the total bitsize in the metadata file.
    FILE *metadata_fp = fopen(metadata_filepath, "rb");
    if (!metadata_fp)
    {
        perror("ERROR opening metadata file");
        return 1;
    }

    for (index = 0; index < num_seeders; index++)
    {
        printf("\n🔄 Attempting to leech from seeder %d of %zu\n", index + 1, num_seeders);
        leech_from_seeder(seeder_list[index], bitfield_filepath, binary_filepath, fileMetaData->totalChunk, fileMetaData->fileID);
        // if all chunks are downloaded then we break
        // else we continue
        break;
    }

    // We will see if we completed all bits in the bitfield.

    // We will run hash check on the binary file and see if it matches

    printf("\n✨ Leeching process completed\n");
    free(fileMetaData);

    return 0;
}
