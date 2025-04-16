#include "peerCommunication.h"
void create_chunkHash(TransferChunk *chunk)
{
    SHA256_CTX sha256;
    SHA256_Init(&sha256); // Initialize SHA-256 context

    SHA256_Update(&sha256, chunk->chunkData, chunk->totalByte); // Update hash with chunk
    SHA256_Final(chunk->chunkHash, &sha256);
}

int has_chunk(uint8_t *bitfield, ssize_t index)
{
    // Step 1: Calculate which byte in the array contains our bit
    size_t byte_position = index / 8;

    // Step 2: Calculate which bit within that byte we need to check
    int bit_position = index % 8;

    // Step 3: Create a mask with only the target bit set to 1
    uint8_t bit_mask = 1 << bit_position;

    // Step 4: Apply the mask to the byte using bitwise AND
    uint8_t result = bitfield[byte_position] & bit_mask;

    // Step 5: Check if the result is non-zero (meaning the bit was set)
    return result != 0;
}