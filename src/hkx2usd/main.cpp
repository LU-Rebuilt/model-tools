// hkx2usd — Export a LEGO Universe HKX collision file to USD.
//
// Phase 3b of the USD content-authoring bridge. HKX collision shapes become
// UsdGeomMesh prims (purpose = guide) so a model's physics can be inspected in
// Blender/Houdini/Maya or usdview. See lu-assets/src/usd/hkx_to_usd.h.

#include "usd/hkx_to_usd.h"

#include <cstdio>
#include <string>

namespace {

void usage(const char* argv0) {
    std::fprintf(stderr,
        "Usage: %s <input.hkx> [output.usda|.usdc]\n"
        "  Converts HKX collision geometry to USD (guide meshes).\n"
        "  Output defaults to <input>.usda.\n",
        argv0);
}

} // namespace

int main(int argc, char** argv) {
    std::string input, output;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { usage(argv[0]); return 0; }
        else if (input.empty()) input = arg;
        else if (output.empty()) output = arg;
    }
    if (input.empty()) { usage(argv[0]); return 1; }
    if (output.empty()) {
        const size_t dot = input.find_last_of('.');
        const size_t slash = input.find_last_of("/\\");
        const bool has_ext = dot != std::string::npos &&
            (slash == std::string::npos || dot > slash);
        output = (has_ext ? input.substr(0, dot) : input) + ".usda";
    }

    try {
        lu::assets::hkx_to_usd(input, output, input);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    std::printf("Wrote %s\n", output.c_str());
    return 0;
}
