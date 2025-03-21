#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "meta.h"  // Assuming meta.h defines `FileMetadata`

/* Function to create a bitfield file with all chunks set to 1 */
void create_bitfield(const char *meta_filename, const char *bitfield_filename) {
    // Open the metadata file
    FILE *meta_fp = fopen(meta_filename, "rb");
    if (!meta_fp) {
        perror("Error opening metadata file");
        exit(EXIT_FAILURE);
    }

    // Read metadata
    FileMetadata fileMetaData;
    if (fread(&fileMetaData, sizeof(FileMetadata), 1, meta_fp) != 1) {
        perror("Error reading metadata file");
        fclose(meta_fp);
        exit(EXIT_FAILURE);
    }
    fclose(meta_fp);

    // Calculate required bitfield size (1 bit per chunk, rounded up to nearest byte)
    size_t bitfield_size = (fileMetaData.totalChunk + 7) / 8;
    
    // Allocate and initialize bitfield (all bits set to 1)
    uint8_t *bitfield = malloc(bitfield_size);
    if (!bitfield) {
        perror("Memory allocation failed for bitfield");
        exit(EXIT_FAILURE);
    }
    memset(bitfield, 0xFF, bitfield_size);  // Set all bits to 1 (owning all chunks)

    // Write bitfield to file
    FILE *bitfield_fp = fopen(bitfield_filename, "wb");
    if (!bitfield_fp) {
        perror("Error opening bitfield file for writing");
        free(bitfield);
        exit(EXIT_FAILURE);
    }
    fwrite(bitfield, 1, bitfield_size, bitfield_fp);
    fclose(bitfield_fp);

    printf("Bitfield file '%s' created with all bits set to 1.\n", bitfield_filename);

    // Cleanup
    free(bitfield);
}

/* Main function */
int main() {
    const char *meta_filename = "gray_cat.meta";
    const char *bitfield_filename = "gray_cat.bitfield";

    create_bitfield(meta_filename, bitfield_filename);
    return 0;
}
