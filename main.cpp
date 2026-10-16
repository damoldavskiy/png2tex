#include <nvtt/nvtt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <vector>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static void load(nvtt::Surface& image, const fs::path& path)
{
    std::cerr << "Loading " << path.filename().string() << "..." << std::endl;
    if (!image.load(path.string().c_str()))
        throw std::runtime_error("Cannot decode PNG: " + path.string());
}

static void save(nvtt::Context& context, const nvtt::Surface& image,
                 const fs::path& path, nvtt::Format format)
{
    nvtt::CompressionOptions compression;
    compression.setFormat(format);
    compression.setQuality(nvtt::Quality_Normal);

    nvtt::OutputOptions output;
    output.setContainer(nvtt::Container_DDS); // Legacy header for DX8 loaders.
    output.setSrgbFlag(false); // Legacy DDS has no DXGI sRGB format tag.
    output.setFileName(path.string().c_str());
    std::cerr << "Compressing " << path.filename().string() << " ("
              << image.width() << 'x' << image.height()
              << ", " << (format == nvtt::Format_DXT1 ? "DXT1" : "DXT5")
              << " Normal)..." << std::endl;
    const auto started = std::chrono::steady_clock::now();
    if (!context.outputHeader(image, 1, compression, output)
        || !context.compress(image, 0, 0, compression, output))
        throw std::runtime_error("Cannot write DDS: " + path.string());
    std::cerr << "Compression finished in "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()
              << " seconds." << std::endl;
}

static const std::array<std::string, 3> suffixes = {"_diffuse", "_normal", "_specular"};

static void convert(nvtt::Context& context, const std::array<fs::path, 3>& inputs,
                    const fs::path& outputDirectory, const std::string& outputBase = {})
{
    const bool createDiffuse = !inputs[0].empty();
    const bool createBump = !inputs[1].empty() && !inputs[2].empty();
    if (inputs[1].empty() != inputs[2].empty() || (!createDiffuse && !createBump))
        throw std::runtime_error("Supply diffuse alone, normal + specular, or all three.");
    const std::size_t namingRole = createDiffuse ? 0 : 1;
    std::string base = inputs[namingRole].stem().string();
    base.resize(base.size() - suffixes[namingRole].size());
    if (!outputBase.empty())
        base = outputBase;
    nvtt::Surface diffuse, normal, specular;
    if (createDiffuse)
        load(diffuse, inputs[0]);
    if (createBump) {
        load(normal, inputs[1]);
        load(specular, inputs[2]);
        if (normal.width() != specular.width() || normal.height() != specular.height()
            || (createDiffuse && (diffuse.width() != normal.width() || diffuse.height() != normal.height())))
            throw std::runtime_error("All supplied inputs must have identical dimensions.");
    }
    const nvtt::Surface& reference = createDiffuse ? diffuse : normal;
    const auto validSize = [](int size) { return size >= 4 && (size & (size - 1)) == 0; };
    if (!validSize(reference.width()) || !validSize(reference.height()))
        throw std::runtime_error("For broad DX8 compatibility, dimensions must be powers of two, at least 4.");

    const auto count = static_cast<std::size_t>(reference.width()) * reference.height();
    if (createDiffuse) {
        float* diffuseAlpha = diffuse.channel(3);
        std::fill(diffuseAlpha, diffuseAlpha + count, 1.0f);
        diffuse.setAlphaMode(nvtt::AlphaMode_None);
    }
    if (createBump) {
        float* nx = normal.channel(0);
        float* ny = normal.channel(1);
        float* nz = normal.channel(2);
        float* na = normal.channel(3);
        const float* spec = specular.channel(0);
        for (std::size_t i = 0; i < count; ++i) {
            const float x = nx[i], y = ny[i], z = nz[i];
            nx[i] = spec[i];
            ny[i] = z;
            nz[i] = y;
            na[i] = x;
        }
        // No premultiplication or filtering; alpha contains normal X.
        normal.setAlphaMode(nvtt::AlphaMode_Transparency);
        normal.setNormalMap(false); // Already packed; no normal-specific swizzle.
    }

    fs::create_directories(outputDirectory);
    const fs::path diffuseOutput = outputDirectory / (base + ".dds");
    const fs::path bumpOutput = outputDirectory / (base + "_bump.dds");
    if (createDiffuse) {
        save(context, diffuse, diffuseOutput, nvtt::Format_DXT1);
        std::cout << diffuseOutput.string() << '\n';
    }
    if (createBump) {
        save(context, normal, bumpOutput, nvtt::Format_DXT5);
        std::cout << bumpOutput.string() << '\n';
    }
}

static int convertDirectory(const fs::path& requested)
{
    const fs::path root = fs::canonical(requested);
    const std::string name = root.filename().string();
    if (name.empty())
        throw std::runtime_error("Supply a named directory, not a filesystem root.");
    const bool raw = name.size() > 4 && lower(name.substr(name.size() - 4)) == "_raw";
    const fs::path outputRoot = root.parent_path()
        / (raw ? name.substr(0, name.size() - 4) : name + "_output");

    // Snapshot PNG paths before creating outputs. Do not follow symbolic links.
    std::map<std::string, fs::path> pngs;
    std::vector<fs::path> diffuseFiles;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_symlink() || !entry.is_regular_file()
            || lower(entry.path().extension().string()) != ".png")
            continue;
        const fs::path relative = entry.path().lexically_relative(root);
        const std::string key = lower(relative.generic_string());
        if (!pngs.emplace(key, entry.path()).second)
            throw std::runtime_error("Ambiguous PNG paths differing only in case: " + relative.string());
        const std::string stem = lower(entry.path().stem().string());
        if (stem.size() > suffixes[0].size()
            && stem.compare(stem.size() - suffixes[0].size(), suffixes[0].size(), suffixes[0]) == 0)
            diffuseFiles.push_back(entry.path());
    }
    std::sort(diffuseFiles.begin(), diffuseFiles.end());
    std::cerr << "Found " << diffuseFiles.size() << " diffuse PNG(s). Output: "
              << outputRoot.string() << std::endl;
    if (diffuseFiles.empty())
        return 0;

    std::cerr << "Initializing NVTT (CUDA requested; CPU fallback available)..." << std::endl;
    nvtt::Context context(true);
    std::size_t diffuseCount = 0, bumpCount = 0, failures = 0;
    for (const auto& diffuse : diffuseFiles) {
        const fs::path relative = diffuse.lexically_relative(root);
        const fs::path destination = outputRoot / relative.parent_path();
        try {
            convert(context, {diffuse, fs::path{}, fs::path{}}, destination);
            ++diffuseCount;
        } catch (const std::exception& error) {
            std::cerr << "png2tex: " << diffuse.string() << ": " << error.what() << std::endl;
            ++failures;
            continue;
        }
        const std::string stem = relative.stem().string();
        const std::string base = stem.substr(0, stem.size() - suffixes[0].size());
        const auto companion = [&](const std::string& suffix) {
            const std::string key = lower((relative.parent_path() / (base + suffix + ".png")).generic_string());
            const auto found = pngs.find(key);
            return found == pngs.end() ? fs::path{} : found->second;
        };
        const fs::path normal = companion(suffixes[1]);
        const fs::path specular = companion(suffixes[2]);
        if (normal.empty() || specular.empty()) {
            if (normal.empty() != specular.empty())
                std::cerr << "Skipping bump for " << relative.string()
                          << ": both normal and specular PNGs are required." << std::endl;
            continue;
        }
        try {
            convert(context, {fs::path{}, normal, specular}, destination, base);
            ++bumpCount;
        } catch (const std::exception& error) {
            std::cerr << "png2tex: bump for " << diffuse.string() << ": " << error.what() << std::endl;
            ++failures;
        }
    }
    std::cerr << "Finished: " << diffuseCount << " diffuse, " << bumpCount
              << " bump, " << failures << " failure(s)." << std::endl;
    return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: png2tex <name_diffuse.png>\n"
                  << "   or: png2tex <directory>\n"
                  << "   or: png2tex <name_normal.png> <name_specular.png>\n"
                  << "   or: png2tex <name_diffuse.png> <name_normal.png> <name_specular.png>\n";
        return 1;
    }

    try {
        if (argc == 2 && fs::is_directory(argv[1]))
            return convertDirectory(argv[1]);
        std::array<fs::path, 3> inputs;
        std::string base;
        std::string baseKey;
        fs::path directory;

        // Match roles by suffix, allowing any argument order.
        for (int i = 1; i < argc; ++i) {
            const fs::path path = fs::absolute(argv[i]).lexically_normal();
            if (lower(path.extension().string()) != ".png" || !fs::is_regular_file(path))
                throw std::runtime_error("Expected an existing PNG: " + path.string());
            const std::string stem = path.stem().string();
            const std::string key = lower(stem);
            std::size_t role = 0;
            for (; role < suffixes.size(); ++role) {
                const auto& suffix = suffixes[role];
                if (key.size() > suffix.size()
                    && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0)
                    break;
            }
            if (role == suffixes.size())
                throw std::runtime_error("Expected _diffuse, _normal or _specular suffix: " + path.string());
            if (!inputs[role].empty())
                throw std::runtime_error("Duplicate input role: " + suffixes[role]);

            const std::string name = stem.substr(0, stem.size() - suffixes[role].size());
            if (base.empty()) {
                base = name;
                baseKey = lower(name);
                directory = path.parent_path();
            } else if (lower(name) != baseKey || !fs::equivalent(directory, path.parent_path())) {
                throw std::runtime_error("Inputs must share a basename and directory.");
            }
            inputs[role] = path;
        }

        std::cerr << "Initializing NVTT (CUDA requested; CPU fallback available)..." << std::endl;
        nvtt::Context context(true);
        convert(context, inputs, directory);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "png2tex: " << error.what() << '\n';
        return 1;
    }
}
