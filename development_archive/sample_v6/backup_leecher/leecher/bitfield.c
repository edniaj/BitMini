#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <sys/types.h> // For ssize_t
#include "meta.h"      // Assuming meta.h defines `FileMetadata`
#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "meta.h"

char *generate_bitfield_filepath_with_id(ssize_t fileID, const char *binary_filepath)
{
    // 1) Find the last slash in the path (if any).
    const char *slash = strrchr(binary_filepath, '/');

    // We'll store directory portion in dirPart, and the filename portion in baseName
    char dirPart[1024];
    char baseName[1024];

    if (slash)
    {
        // The directory is everything up to (and including) the slash
        size_t dirLen = (size_t)(slash - binary_filepath) + 1; // +1 to include slash itself
        if (dirLen >= sizeof(dirPart))
        {
            // Path too long for our buffer – handle error if needed
            return NULL;
        }
        memcpy(dirPart, binary_filepath, dirLen);
        dirPart[dirLen] = '\0';

        // The basename starts after the slash
        strncpy(baseName, slash + 1, sizeof(baseName) - 1);
        baseName[sizeof(baseName) - 1] = '\0';
    }
    else
    {
        // No slash found, so there's no directory portion
        dirPart[0] = '\0';
        // The entire binary_filepath is the base name
        strncpy(baseName, binary_filepath, sizeof(baseName) - 1);
        baseName[sizeof(baseName) - 1] = '\0';
    }

    // 2) Construct something like "./storage_downloads/0021_red_dog.png.meta"
    //    We need enough space for: directory + 4-digit fileID + underscore + basename + ".meta" + null terminator.
    size_t finalSize = strlen(dirPart)    // e.g. "./storage_downloads/"
                       + 5                // 4 digits for fileID + underscore (e.g. "0021_")
                       + strlen(baseName) // e.g. "red_dog.png"
                       + strlen(".bitfield")  // ".bitfield"
                       + 1;               // null terminator

    char *result = (char *)malloc(finalSize);
    if (!result)
        return NULL;

    snprintf(result, finalSize, "%s%04zd_%s.bitfield", // zero-pad fileID to 4 digits
             dirPart, fileID, baseName);

    return result; // caller must free!
}



/* Function to create a bitfield file with all chunks set to 1 */
void create_filled_bitfield(const char *metadata_filepath, const char *bitfield_filepath)
{
    // Open the metadata file
    FILE *meta_fp = fopen(metadata_filepath, "rb");
    if (!meta_fp)
    {
        perror("Error opening metadata file");
        exit(EXIT_FAILURE);
    }

    // Read metadata
    FileMetadata fileMetaData;
    if (fread(&fileMetaData, sizeof(FileMetadata), 1, meta_fp) != 1)
    {
        perror("Error reading metadata file");
        fclose(meta_fp);
        exit(EXIT_FAILURE);
    }
    fclose(meta_fp);

    // Calculate required bitfield size (1 bit per chunk, rounded up to nearest byte)
    size_t bitfield_size = (fileMetaData.totalChunk + 7) / 8;

    // Allocate and initialize bitfield (all bits set to 1)
    uint8_t *bitfield = malloc(bitfield_size);
    if (!bitfield)
    {
        perror("Memory allocation failed for bitfield");
        exit(EXIT_FAILURE);
    }
    memset(bitfield, 0xFF, bitfield_size); // Set all bits to 1 (owning all chunks)

    // Write bitfield to file
    FILE *bitfield_fp = fopen(bitfield_filepath, "wb");
    if (!bitfield_fp)
    {
        perror("Error opening bitfield file for writing");
        free(bitfield);
        exit(EXIT_FAILURE);
    }
    fwrite(bitfield, 1, bitfield_size, bitfield_fp);
    fclose(bitfield_fp);

    printf("Bitfield file '%s' created with all bits set to 1.\n", bitfield_filepath);

    // Cleanup
    free(bitfield);
}

/* Function to create a bitfield file with all chunks set to 0 */
void create_empty_bitfield(const char *metadata_filepath, const char *bitfield_filepath)
{
    // Open the metadata file
    FILE *meta_fp = fopen(metadata_filepath, "rb");
    if (!meta_fp)
    {
        perror("Error opening metadata file");
        exit(EXIT_FAILURE);
    }

    // Read metadata
    FileMetadata fileMetaData;
    if (fread(&fileMetaData, sizeof(FileMetadata), 1, meta_fp) != 1)
    {
        perror("Error reading metadata file");
        fclose(meta_fp);
        exit(EXIT_FAILURE);
    }
    fclose(meta_fp);

    // Calculate required bitfield size (1 bit per chunk, rounded up to nearest byte)
    size_t bitfield_size = (fileMetaData.totalChunk + 7) / 8;

    // Allocate and initialize bitfield (all bits set to 0)
    uint8_t *bitfield = malloc(bitfield_size);
    if (!bitfield)
    {
        perror("Memory allocation failed for bitfield");
        exit(EXIT_FAILURE);
    }
    memset(bitfield, 0x00, bitfield_size); // Set all bits to 0 (owning no chunks)

    // Write bitfield to file
    FILE *bitfield_fp = fopen(bitfield_filepath, "wb");
    if (!bitfield_fp)
    {
        perror("Error opening bitfield file for writing");
        free(bitfield);
        exit(EXIT_FAILURE);
    }
    fwrite(bitfield, 1, bitfield_size, bitfield_fp);
    fclose(bitfield_fp);

    printf("Bitfield file '%s' created with all bits set to 0.\n", bitfield_filepath);

    // Cleanup
    free(bitfield);
}

/* Main function */
// int main() {
//     const char *metadata_filepath = "gray_cat.meta";
//     const char *bitfield_filepath = "gray_cat.bitfield";

//     create_filled_bitfield(metadata_filepath, bitfield_filepath);
//     return 0;
// }
