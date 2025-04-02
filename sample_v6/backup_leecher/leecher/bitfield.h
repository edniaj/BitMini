#ifndef BITFIELD_H
#define BITFIELD_H

char *generate_bitfield_filepath_with_id(ssize_t fileID, const char *binary_filepath);
void create_filled_bitfield(const char *metadata_filepath, const char *bitfield_filepath);
void create_empty_bitfield(const char *metadata_filepath, const char *bitfield_filepath);

#endif // BITFIELD_H
