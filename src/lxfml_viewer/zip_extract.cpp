#include "zip_extract.h"

#include <zlib.h>
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace lxfml_viewer {

namespace {

// Read little-endian integers from buffer
uint16_t read_u16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t read_u32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

} // anonymous namespace

std::vector<uint8_t> zip_extract_file(const std::string& zipPath,
                                       const std::string& entryName) {
    // Read entire ZIP into memory (LXF files are small)
    std::ifstream file(zipPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Cannot open ZIP file: " + zipPath);

    auto size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> zipData(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(zipData.data()), size);

    // Scan for local file headers (PK\x03\x04)
    const uint8_t* data = zipData.data();
    size_t dataSize = zipData.size();
    size_t offset = 0;

    while (offset + 30 <= dataSize) {
        if (read_u32(data + offset) != 0x04034b50) break; // Not a local file header

        uint16_t method     = read_u16(data + offset + 8);
        uint32_t compSize   = read_u32(data + offset + 18);
        uint32_t uncompSize = read_u32(data + offset + 22);
        uint16_t nameLen    = read_u16(data + offset + 26);
        uint16_t extraLen   = read_u16(data + offset + 28);

        size_t nameOffset = offset + 30;
        size_t dataOffset = nameOffset + nameLen + extraLen;

        if (nameOffset + nameLen > dataSize) break;

        std::string name(reinterpret_cast<const char*>(data + nameOffset), nameLen);

        if (name == entryName) {
            if (dataOffset + compSize > dataSize)
                throw std::runtime_error("ZIP entry data exceeds file size");

            if (method == 0) {
                // Stored (no compression)
                return std::vector<uint8_t>(data + dataOffset,
                                             data + dataOffset + compSize);
            } else if (method == 8) {
                // Deflate
                std::vector<uint8_t> result(uncompSize);

                z_stream strm = {};
                strm.next_in = const_cast<Bytef*>(data + dataOffset);
                strm.avail_in = compSize;
                strm.next_out = result.data();
                strm.avail_out = uncompSize;

                // -MAX_WBITS for raw deflate (no zlib/gzip header)
                if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
                    throw std::runtime_error("inflateInit2 failed");

                int ret = inflate(&strm, Z_FINISH);
                inflateEnd(&strm);

                if (ret != Z_STREAM_END)
                    throw std::runtime_error("ZIP decompression failed");

                return result;
            } else {
                throw std::runtime_error("Unsupported ZIP compression method: " +
                                         std::to_string(method));
            }
        }

        offset = dataOffset + compSize;
    }

    throw std::runtime_error("Entry not found in ZIP: " + entryName);
}

} // namespace lxfml_viewer
