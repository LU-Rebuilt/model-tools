// nif2usd — Convert a LEGO Universe NIF model to a USD stage (.usda/.usdc).
//
// Phase 1 of the USD content-authoring bridge: exports mesh geometry,
// materials, textures and vertex colors so LU models open in Blender/Houdini/
// usdview. See lu-assets/usd/nif_to_usd.h for what is/isn't exported yet.

#include "gamebryo/nif/nif_reader.h"
#include "usd/nif_to_usd.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
    std::fprintf(stderr,
        "Usage: %s <input.nif> [output.usd|.usda|.usdc]\n"
        "  Converts a NIF model to a USD stage.\n"
        "  Output defaults to <input>.usda (text). Use a .usdc extension for\n"
        "  a binary crate. --y-up bakes NIF Z-up into USD Y-up.\n",
        argv0);
}

} // namespace

int main(int argc, char** argv) {
    std::string input;
    std::string output;
    lu::assets::NifToUsdOptions options;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--y-up") {
            options.convert_to_y_up = true;
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        } else if (input.empty()) {
            input = arg;
        } else if (output.empty()) {
            output = arg;
        }
    }

    if (input.empty()) {
        usage(argv[0]);
        return 1;
    }

    if (output.empty()) {
        // Replace/append extension with .usda.
        const size_t dot = input.find_last_of('.');
        const size_t slash = input.find_last_of("/\\");
        const bool has_ext = dot != std::string::npos &&
            (slash == std::string::npos || dot > slash);
        output = (has_ext ? input.substr(0, dot) : input) + ".usda";
    }
    options.binary = output.size() >= 5 && output.substr(output.size() - 5) == ".usdc";

    std::ifstream f(input, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "error: cannot open input: %s\n", input.c_str());
        return 1;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());

    try {
        lu::assets::NifFile nif = lu::assets::nif_parse(
            std::span<const uint8_t>(data.data(), data.size()));
        lu::assets::nif_to_usd(nif, output, input, options);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::printf("Wrote %s\n", output.c_str());
    return 0;
}
