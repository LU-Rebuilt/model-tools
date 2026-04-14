#pragma once
// zip_extract.h — Minimal ZIP file extraction for LXF files.
// Only supports extracting a single named file from an unencrypted ZIP archive.
// Uses zlib for DEFLATE decompression.

#include <cstdint>
#include <string>
#include <vector>

namespace lxfml_viewer {

// Extract a single file from a ZIP archive by name.
// Returns the decompressed file contents.
// Throws std::runtime_error on failure.
std::vector<uint8_t> zip_extract_file(const std::string& zipPath,
                                       const std::string& entryName);

} // namespace lxfml_viewer
