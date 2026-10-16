# png2tex

Minimal C++17 / NVTT 3 CLI. Supply diffuse only, normal + specular, or all three (any order):

```sh
png2tex brick_diffuse.png
png2tex brick_normal.png brick_specular.png
png2tex brick_diffuse.png brick_normal.png brick_specular.png
```

With one _diffuse.png input, only <name>.dds is generated. No normal or
specular files are searched for or loaded; an existing bump DDS is left alone.
With normal + specular inputs, only <name>_bump.dds is generated; diffuse is
not loaded or written, and any existing diffuse DDS is left alone.
A normal or specular input always requires its partner. Single normal/specular
inputs and diffuse + only one of those inputs are rejected.

All inputs must share a basename, directory and dimensions. Filename suffixes
and extensions are matched case-insensitively. Width and height must be
powers of two, at least 4, for broad DX8 hardware compatibility; inputs are
never resized automatically.

Outputs are saved beside the inputs, using the diffuse basename's spelling when supplied, otherwise the normal's:

| Output | Compression | R | G | B | A |
| --- | --- | --- | --- | --- | --- |
| brick.dds | DXT1 opaque (4 bpp) | diffuse R | diffuse G | diffuse B | 1.0 |
| brick_bump.dds | DXT5 (8 bpp) | specular R | normal B (Z) | normal G (Y) | normal R (X) |

The normal input uses DirectX tangent-space conventions. Its encoded channel
values are copied at the base level without flipping Y or renormalizing. Specular is treated as
linear scalar data taken from the input's red channel (use a grayscale PNG).
The diffuse input is assumed to contain sRGB color values. Base-level RGB
is unchanged before compression; smaller mips are filtered in linear light
and encoded back to sRGB. Legacy DDS does not carry a DXGI sRGB format tag;
the renderer controls diffuse color-space handling. Bump channels are linear data.

CUDA acceleration requested, with CPU fallback; NVTT Normal quality.
Legacy DDS headers (DXT1/DXT5 FourCC), with full mip chains down to 1x1.
Loading, each mip index/dimensions and per-file compression times are printed
to stderr. No base-level resizing or alpha premultiplication. DXT1's opaque encoding
returns alpha 1. DXT5 preserves all four packed channels, subject to compression
loss. These formats target DX8; the renderer must check device format support.
Compression is lossy; "copied" describes values supplied to the compressor.

Existing outputs are overwritten. A failed conversion can leave a partial
output or only the first DDS. Exit status is 0 on success, 1 on failure.
Paths must be representable by the platform's narrow filename API.

## Mipmap generation

Every output contains the full mip chain, including 2x2 and 1x1 levels.
For example, 2048x2048 produces 12 levels, including the original.
Each level is compressed from uncompressed working data, never from a
previously compressed DDS level.

| Data | Filter | Processing |
| --- | --- | --- |
| Diffuse RGB | Kaiser | sRGB to linear, filter, return to sRGB |
| Diffuse A | Constant | Force 1 at every level |
| Bump R (specular) | Box | Filter linear scalar data; clamp to [0,1] |
| Bump G/B/A (normal Z/Y/X) | Box | Decode vectors, average, normalize, encode |

Normal and specular images stay separate while filtering. Each bump mip is
packed afterward with the same RGBA layout as the base. DirectX Y direction
is preserved. A zero-length averaged normal falls back to (0,0,1). Source
normal vectors are assumed to be valid unit vectors; base-level normal values
are retained. Each smaller level is generated from the previous uncompressed
level. NVTT's default filter parameters and edge addressing are used.

## Directory mode

```bash
./build/Release/png2tex.exe "C:/textures/dir"
./build/Release/png2tex.exe "C:/textures/dir_raw"
```

A single directory argument recursively finds every `<name>_diffuse.png`.
For each diffuse it writes `<name>.dds`. If matching `<name>_normal.png` and
`<name>_specular.png` both exist in that same source folder, it also writes
`<name>_bump.dds`. Suffixes, PNG extensions and companion names are matched
case-insensitively. Orphan normal/specular pairs without a diffuse are ignored
in directory mode. The explicit two-file CLI mode remains available for them.

| Source root | Output root |
| --- | --- |
| C:/textures/dir | C:/textures/dir_output |
| C:/textures/dir_raw | C:/textures/dir |

Only the final directory name is changed. `_raw` is stripped only when a
nonempty name precedes it. The relative folder hierarchy is preserved:
`dir_raw/weapons/brick_diffuse.png` produces `dir/weapons/brick.dds` and,
when both companions exist, `dir/weapons/brick_bump.dds`.

Output directories are created as needed. Inputs are left unchanged and
existing DDS outputs are overwritten. Symbolic links are skipped. Ambiguous
source PNG paths differing only in case are rejected.

Diffuse and bump are converted independently, so a failed bump does not prevent
a valid diffuse from being saved. The normal and specular dimensions must
match each other; in directory mode their size may differ from the diffuse.
The existing power-of-two and minimum-size requirements apply to each output.
A missing companion skips bump generation; invalid textures are reported and
processing continues. The final count reports successful diffuse/bump outputs
and failures. Exit status is 1 if any conversion failed, otherwise 0. Empty
input trees report zero matches and return 0 without creating an output folder.

Explicit PNG input modes still save beside their source PNGs.

## Windows build (Git Bash)

Install CMake 3.20+, Visual Studio with C++ support, and the NVTT 3 SDK.
For the installation used in this project:

```bash
cmake -S . -B build -A x64 \
  -DNVTT_INCLUDE_DIR="C:/Program Files/NVIDIA Corporation/NVIDIA Texture Tools/include" \
  -DNVTT_LIBRARY="C:/Program Files/NVIDIA Corporation/NVIDIA Texture Tools/lib/x64-v142/nvtt30205.lib"
cmake --build build --config Release
export PATH="/c/Program Files/NVIDIA Corporation/NVIDIA Texture Tools:$PATH"
./build/Release/png2tex.exe "C:/textures/brick_diffuse.png" "C:/textures/brick_normal.png" "C:/textures/brick_specular.png"
```

The export assumes the SDK DLLs are in that installation directory. If they are
elsewhere, add their actual directory to PATH or copy them beside the EXE.
If already configured, replace main.cpp and run only the build command.

## Other SDK installations

Download [NVTT 3](https://developer.nvidia.com/gpu-accelerated-texture-compression).
Pass the directory containing nvtt/nvtt.h as NVTT_INCLUDE_DIR and the actual
SDK import/shared library as NVTT_LIBRARY. SDK binaries are not included.
On Linux use a compatible C++17 compiler, omit -A x64, link the SDK .so, and
make its dependencies available to the dynamic loader.

## Validation

Application checks passed using an NVTT test double: full mip count and indices,
rectangular tails down to 1x1, Kaiser diffuse / Box normal and specular selection, linear-light diffuse
processing, normal vector normalization and zero-vector fallback, specular
clamping, channel packing, all explicit CLI modes and recursive directory mode.
Previous directory naming, companion matching and input validation checks also
passed before this mipmap change.

The test double models downsampling with averaging; it does not validate NVTT's
actual Kaiser kernel, compression quality, DDS headers, or binary compatibility.
The mip-writing API follows NVIDIA's NVTT 3 mipmap sample. The proprietary SDK
is not installed in the authoring environment, so an actual SDK build and
end-to-end DDS conversion still need verification on the target machine.
