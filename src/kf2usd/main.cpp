// kf2usd — Export a LEGO Universe KF animation to a UsdSkel animation stage.
//
// Phase 3 of the USD content-authoring bridge. A .kf file's clips become
// UsdSkelAnimations under a skeleton, so the motion can be inspected/edited in
// Blender/Houdini/Maya or previewed in usdview. See lu-assets/src/usd/kf_to_usd.h.

#include "gamebryo/nif/nif_reader.h"
#include "usd/kf_to_usd.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
    std::fprintf(stderr,
        "Usage: %s <input.kf> [output.usda|.usdc]\n"
        "  Converts a KF animation to a UsdSkel animation stage.\n"
        "  Output defaults to <input>.usda. --fps N sets time codes per second\n"
        "  (default 30). --clip N exports only clip index N.\n",
        argv0);
}

} // namespace

int main(int argc, char** argv) {
    std::string input, output;
    lu::assets::KfToUsdOptions options;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--fps" && i + 1 < argc) {
            options.time_codes_per_second = std::stod(argv[++i]);
        } else if (arg == "--clip" && i + 1 < argc) {
            options.clip_index = std::stoi(argv[++i]);
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
        output = (has_ext ? input.substr(0, dot) : input) + ".usda";
    }

    std::ifstream f(input, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "error: cannot open input: %s\n", input.c_str());
        return 1;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());

    try {
        lu::assets::NifFile kf = lu::assets::nif_parse(
            std::span<const uint8_t>(data.data(), data.size()));
        lu::assets::kf_to_usd(kf, output, input, options);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::printf("Wrote %s\n", output.c_str());
    return 0;
}
