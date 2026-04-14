#include "brick_db.h"
#include "zip_extract.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>

#include <zlib.h>
#include <fstream>
#include <algorithm>

namespace lxfml_viewer {

namespace {

bool ends_with_g(const std::string& name) {
    // Match .g, .g1, .g2, .g3, .g4
    auto dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    auto ext = name.substr(dot);
    if (ext == ".g") return true;
    if (ext.size() == 3 && ext[0] == '.' && ext[1] == 'g' && ext[2] >= '1' && ext[2] <= '9')
        return true;
    return false;
}

// Extract just the filename (without path) from a path string.
std::string filename_from_path(const std::string& path) {
    auto sep = path.find_last_of("/\\");
    return (sep == std::string::npos) ? path : path.substr(sep + 1);
}

} // anonymous namespace

void BrickDB::loadFromDirectory(const std::string& path) {
    QDir dir(QString::fromStdString(path));
    if (!dir.exists()) return;

    // Check for brickdb.zip and index it
    QString brickdbZip = dir.absoluteFilePath("brickdb.zip");
    if (QFile::exists(brickdbZip)) {
        indexZip(brickdbZip.toStdString());
    }

    // Walk subdirectories looking for .g files in known locations
    // Priority order (later overwrites earlier for same filename):
    static const char* searchDirs[] = {
        "Primitives/LOD0",           // Extracted brickdb
        "brickprimitives/lod0",      // LU client
        "bricks_ldd/brickprimitives/lod0",  // LDD extras
    };

    for (const auto& subdir : searchDirs) {
        QDir sub(dir.absoluteFilePath(subdir));
        if (sub.exists()) {
            indexDirectory(sub.absolutePath().toStdString());
        }
    }

    // Also walk the given path itself if it looks like a primitives dir
    // (user might point directly to a lod0 folder)
    QDirIterator it(dir.absolutePath(), {"*.g", "*.g1", "*.g2", "*.g3", "*.g4"},
                    QDir::Files, QDirIterator::NoIteratorFlags);
    if (it.hasNext()) {
        indexDirectory(dir.absolutePath().toStdString());
    }
}

void BrickDB::loadFromZip(const std::string& zipPath) {
    indexZip(zipPath);
}

void BrickDB::indexDirectory(const std::string& dir) {
    QDirIterator it(QString::fromStdString(dir),
                    {"*.g", "*.g1", "*.g2", "*.g3", "*.g4"},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        std::string fullPath = it.filePath().toStdString();
        std::string name = it.fileName().toStdString();
        // Store by filename for lookup
        Entry entry;
        entry.filePath = fullPath;
        index_[name] = entry;
    }
}

void BrickDB::indexZip(const std::string& zipPath) {
    // Read ZIP central directory to index .g entries.
    // We read the whole ZIP into memory since brickdb.zip is small.
    std::ifstream file(zipPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return;

    auto size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);

    // Scan local file headers
    size_t offset = 0;
    while (offset + 30 <= data.size()) {
        uint32_t sig = data[offset] | (data[offset+1]<<8) |
                       (data[offset+2]<<16) | (static_cast<uint32_t>(data[offset+3])<<24);
        if (sig != 0x04034b50) break;

        uint32_t compSize   = data[offset+18] | (data[offset+19]<<8) |
                              (data[offset+20]<<16) | (static_cast<uint32_t>(data[offset+21])<<24);
        uint16_t nameLen    = data[offset+26] | (data[offset+27]<<8);
        uint16_t extraLen   = data[offset+28] | (data[offset+29]<<8);

        if (offset + 30 + nameLen > data.size()) break;

        std::string entryName(reinterpret_cast<const char*>(data.data() + offset + 30), nameLen);
        std::string fname = filename_from_path(entryName);

        if (ends_with_g(fname)) {
            Entry entry;
            entry.zipPath = zipPath;
            entry.zipEntryName = entryName;
            // Only add if not already present from filesystem
            if (index_.find(fname) == index_.end()) {
                index_[fname] = entry;
            }
        }

        offset = offset + 30 + nameLen + extraLen + compSize;
    }
}

std::vector<uint8_t> BrickDB::loadGeometry(int designID, int partIndex) const {
    std::string filename = std::to_string(designID);
    if (partIndex == 0) filename += ".g";
    else filename += ".g" + std::to_string(partIndex);

    auto it = index_.find(filename);
    if (it == index_.end()) return {};

    const auto& entry = it->second;

    if (!entry.filePath.empty()) {
        // Load from filesystem
        QFile f(QString::fromStdString(entry.filePath));
        if (!f.open(QIODevice::ReadOnly)) return {};
        QByteArray raw = f.readAll();
        return std::vector<uint8_t>(raw.begin(), raw.end());
    }

    if (!entry.zipPath.empty() && !entry.zipEntryName.empty()) {
        // Load from ZIP
        try {
            return zip_extract_file(entry.zipPath, entry.zipEntryName);
        } catch (...) {
            return {};
        }
    }

    return {};
}

} // namespace lxfml_viewer
