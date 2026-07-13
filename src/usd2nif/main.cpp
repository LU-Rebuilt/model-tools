// usd2nif — Import a USD stage back into a LEGO Universe NIF model.
//
// Phase 2 of the USD content-authoring bridge (inverse of nif2usd). Builds a
// NIF the client accepts and renders from a USD stage's meshes and materials.
// Not byte-identical to any original NIF — USD doesn't carry Gamebryo's exact
// block layout — but produces a valid, loadable model. See
// lu-assets/src/usd/usd_to_nif.h for scope.

#include "gamebryo/nif/nif_writer.h"
#include "usd/usd_to_nif.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
    std::fprintf(stderr,
        "Usage: %s <input.usd|.usda|.usdc> [output.nif]\n"
        "  Imports a USD stage into a NIF model.\n"
        "  Output defaults to <input>.nif. --z-up forces Y-up->Z-up rotation.\n",
        argv0);
}

} // namespace

int main(int argc, char** argv) {
    std::string input;
    std::string output;
    lu::assets::UsdToNifOptions options;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--z-up") {
            options.force_z_up_from_y = true;
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
        const size_t dot = input.find_last_of('.');
        const size_t slash = input.find_last_of("/\\");
        const bool has_ext = dot != std::string::npos &&
            (slash == std::string::npos || dot > slash);
        output = (has_ext ? input.substr(0, dot) : input) + ".nif";
    }

    try {
        lu::assets::NifFile nif = lu::assets::usd_to_nif(input, options);
        std::vector<uint8_t> bytes = lu::assets::nif_write(nif);
        std::ofstream out(output, std::ios::binary);
        if (!out) {
            std::fprintf(stderr, "error: cannot write output: %s\n", output.c_str());
            return 1;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::printf("Wrote %s\n", output.c_str());
    return 0;
}
