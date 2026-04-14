#pragma once
// brick_db.h — Brick geometry database.
// Indexes .g files from filesystem directories and brickdb.zip archives.
// Follows the same resolution logic as lu-toolbox's DBFolderReader.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lxfml_viewer {

// Indexes and loads brick .g geometry files from multiple sources:
//   1. brickdb.zip (extracted or read directly)
//   2. brickprimitives/lod0/
//   3. bricks_ldd/brickprimitives/lod0/
//   4. Primitives/LOD0/ (from extracted brickdb)
class BrickDB {
public:
    // Load/index a client res directory or a specific primitives directory.
    // Walks subdirectories for .g files and checks for brickdb.zip.
    void loadFromDirectory(const std::string& path);

    // Load .g entries from a brickdb.zip file directly.
    void loadFromZip(const std::string& zipPath);

    // Look up a .g file by design ID and part index (0=.g, 1=.g1, etc).
    // Returns the file data, or empty vector if not found.
    std::vector<uint8_t> loadGeometry(int designID, int partIndex) const;

    // Number of indexed .g entries.
    int entryCount() const { return static_cast<int>(index_.size()); }

    // Whether any geometry was found.
    bool empty() const { return index_.empty(); }

private:
    struct Entry {
        std::string filePath;      // filesystem path, or ""
        std::string zipPath;       // zip archive path, or ""
        std::string zipEntryName;  // entry name within zip, or ""
    };

    // Key: "designID.g", "designID.g1", etc.
    std::unordered_map<std::string, Entry> index_;

    void indexDirectory(const std::string& dir);
    void indexZip(const std::string& zipPath);
};

} // namespace lxfml_viewer
