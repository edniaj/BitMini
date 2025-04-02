#ifndef BITFIELD_H
#define BITFIELD_H

// Creates a bitfield file from the given metadata file.
// All bits will be set to 1 (indicating all chunks are owned).
char *generate_bitfield_filepath_with_id(ssize_t fileID, const char *binary_filepath);
void create_filled_bitfield(const char *metadata_filepath, const char *bitfield_filepath);

#endif // BITFIELD_H
