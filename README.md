# USD from glTF

Library, command-line tool, and import plugin for converting [glTF](https://www.khronos.org/gltf/) models to [Pixar's USD format](https://graphics.pixar.com/usd/docs/index.html), compatible with [AR Quick Look](https://developer.apple.com/arkit/gallery) on iOS.

*Please note that this is not an officially supported Google product.*

This is a C++ native library that serves as an alternative to existing scripted solutions. Its main benefits are improved compatibility and conversion speed (see [Compatibility](#compatibility) and [Performance](#performance)).

TLDR: [Install](#installation-steps) it, then [convert](#using-the-command-line-tool) with: `usd_from_gltf <source.gltf> <destination.usdz>`

## Installation Steps

*   Download and build [USD](https://github.com/PixarAnimationStudios/USD). See the associated README for prerequisites and build steps. Refer to USD installation directory as `{USD}`.
*   Install [NASM](https://www.nasm.us).
    *   *(Linux)* `sudo apt-get install nasm`
    *   *(OSX)* `brew install nasm` (requires [Homebrew](https://brew.sh))
    *   *(Windows)* Use the installer for the latest stable release.
*   Download `usd_from_gltf` source to `{UFG_SRC}`.
*   Install to `{UFG_BUILD}` (with optional test data):

        python {UFG_SRC}/tools/ufginstall/ufginstall.py {UFG_BUILD} {USD} --testdata
*   *(Linux/OSX)* Set `LD_LIBRARY_PATH` to the USD and usd_from_gltf `lib` directories. See ufginstall script output for the paths.
*   *(Optional)* Add executable to `PATH`. See ufginstall script output for the exe path.
*   *(Optional)* Build test data. See ufginstall script output for the ufgtest.py command.
*   *(Optional)* Set `PXR_PLUGINPATH_NAME` so the glTF import plugin is available in Usdview. See ufginstall script output for the path.


## Using the Command-Line Tool

The command-line tool is called `usd_from_gltf` and is in the `{UFG_BUILD}/bin` directory. Run it with (use --help for full documentation):

    usd_from_gltf <source.gltf> <destination.usdz>


## Batch Converting and Testing

The library contains `{UFG_SRC}/tools/ufgbatch/ufgbatch.py` to facilitate batch conversion. Run it with (use --help for full documentation):

    python {UFG_SRC}/tools/ufgbatch/ufgbatch.py my_tests.csv --exe "{UFG_BUILD}/bin/usd_from_gltf"

Each input CSV contains a list of conversion tasks of the form:

    name,path/to/input.gltf,dir/to/output/usd[, optional usd_from_gltf flags]

The library also contains `{UFG_SRC}/tools/ufgbatch/ufgtest.py` to facilitate testing, and preview deployment. Run it with (use --help for full documentation):

    python {UFG_SRC}/tools/ufgbatch/ufgtest.py my_tests.csv --exe "{UFG_BUILD}/bin/usd_from_gltf"

For development and testing, the `ufgtest.py` has a couple additional features:

*   Golden file diffs. After a build completes, the tool compares built files against files in a known-good 'golden' directory. This is useful for determining if changes to the library affect generated data. This can be disabled with --nodiff.
*   Preview web site deployment. This copies changed USDZ files (different from golden) to a directory and generates an index.html to view the listing in a browser, compatible with QuickLook on iOS. This can be disabled with --nodeploy.

## Using the Library

The converter can be linked with other applications using the libraries in `{UFG_BUILD}/lib/ufg`. Call `ufg::ConvertGltfToUsd` to convert a glTF file to USD.

## Using the Import Plugin

The plugin isn't necessary for conversion, but it's useful for previewing glTF files in UsdView.

To use it, set the `PXR_PLUGINPATH_NAME` environment variable to the directory containing `plugInfo.json`. See ufginstall script output for the path.

## Compatibility

While USD is a general-purpose format, this library focuses on compatibility with [AR Quick Look](https://developer.apple.com/arkit/gallery). The [AR Quick Look](https://developer.apple.com/arkit/gallery) renderer only supports a subset of the [glTF 2.0 specification](https://github.com/KhronosGroup/glTF) though, so there are several limitations. Where reasonable, missing features are emulated.

### Key Features
*   Reads both text (glTF) and binary (GLB) input files.
*   Generates USDA and/or USDZ output files.
*   Reads embedded binary data and [Draco](https://github.com/google/draco)-compressed meshes.
*   Rigid and skinned animation.
*   Untextured, textured, unlit and, PBR lit materials.
*   glTF extensions: [KHR_materials_unlit](https://github.com/KhronosGroup/glTF/tree/master/extensions/2.0/Khronos/KHR_materials_unlit), [KHR_materials_pbrSpecularGlossiness](https://github.com/KhronosGroup/glTF/tree/master/extensions/2.0/Khronos/KHR_materials_pbrSpecularGlossiness), [KHR_texture_transform](https://github.com/KhronosGroup/glTF/tree/master/extensions/2.0/Khronos/KHR_texture_transform), [KHR_draco_mesh_compression](https://github.com/KhronosGroup/glTF/tree/master/extensions/2.0/Khronos/KHR_draco_mesh_compression)
*   It is expected to convert all well-formed glTF files, although some features may be missing or emulated. It is known to build all Khronos glTF [sample](https://github.com/KhronosGroup/glTF-Sample-Models) and [reference](https://github.com/KhronosGroup/glTF-Asset-Generator) models.

### Emulated Functionality for [AR Quick Look](https://developer.apple.com/arkit/gallery)
Several rendering features of glTF are not currently supported in [AR Quick Look](https://developer.apple.com/arkit/gallery), but they are emulated where reasonable. The emulated features are:

*   Texture channel references. The [USD preview surface](https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html) supports this, but [AR Quick Look](https://developer.apple.com/arkit/gallery) requires distinct textures for the roughness, metallic, and occlusion channels. The converter splits channels into separate textures and recompresses them as necessary.
*   Texture color scale and offset. These are emulated by baking the scale and offset into the texture. This potentially increases the output size if a texture is referenced multiple times with different settings.
*   Texture UV transforms. These are emulated by baking transforms into vertex UVs.
*   Specular workflow. [USD preview surface](https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html) supports specular workflow, but [AR Quick Look](https://developer.apple.com/arkit/gallery) does not. The converter with generate metallic+roughness textures as an approximation.
*   Arbitrary asset sizes. [AR Quick Look](https://developer.apple.com/arkit/gallery) has some limit (empirically, around 200MB) to the decompressed size, and will fail to load models larger than this. The converter works around this by globally resizing textures to fit within the configured limit.
*   Unlit materials. The converter emulates these with a pure emissive material. This mostly works, but there are some differences due a rim light factor in the [AR Quick Look](https://developer.apple.com/arkit/gallery) renderer.
*   sRGB emissive texture. [AR Quick Look](https://developer.apple.com/arkit/gallery) incorrectly treats the emissive texture as linear rather than sRGB, so the converter works around this by converting to linear.
*   Alpha cutoff. The converter works around this by baking alpha values to 0 or 1 for alpha cutoff materials. This will increase the output size if the texture is referenced by materials with different cutoff state. Also, due to the lack of transparency sorting, alpha cutoff materials may exhibit sorting errors.
*   Double-sided geometry. The converter works around this by duplicating geometry.
*   Normal-map normalization. [AR Quick Look](https://developer.apple.com/arkit/gallery) does not normalize normal-map normals, causing incorrect lighting for some textures. The converter explicitly renormalizes normal-map textures to work around this.
*   Inverted transforms. [AR Quick Look](https://developer.apple.com/arkit/gallery) will incorrectly face-cull for inverted geometry, so the converter works around this by baking the reversed polygon winding into the mesh where necessary.
*   Quaternion-based rigid animation. [USD preview surface](https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html) does not support quaternions for rigid animations. The converter works around this by converting to Euler, which may suffer from [Gimbal lock](https://en.wikipedia.org/wiki/Gimbal_lock) issues. To reduce error, the converter bakes Euler keys at a higher frequency, which can increase animation size.
*   Spherical linear ([slerp](https://en.wikipedia.org/wiki/Slerp)) interpolation for rotations. All interpolation is linear, so blends between matrix or quaternion keys are incorrect and can induce scale changes. The converter works around this by converting to Euler for rigid animation, and by baking quaternion keys at higher frequency for skinned animation.
*   Per-joint animation channels. Skinning for [USD preview surface](https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html) is a special-case that does not make use of independent animation channels, so the converter expands source channels to a grid of (joints * keys) elements. Animations will be significantly larger than their glTF source for complex skeletons.
*   Multiple skeletons. [AR Quick Look](https://developer.apple.com/arkit/gallery) only supports a single skeleton, so the converter emulates this by merging multiple skeletons into one (at some cost to animation size).
*   Step and cubic animation interpolation modes. The converter emulates these by baking them to linear (again, at a cost of animation size).
*   Vertex quantization/compression. All vertex components are converted to full float precision, and [Draco](https://github.com/KhronosGroup/glTF/tree/master/extensions/2.0/Khronos/KHR_draco_mesh_compression)-compressed meshes are decompressed.

### Features Unsupported by [AR Quick Look](https://developer.apple.com/arkit/gallery)
These features are not supported in [AR Quick Look](https://developer.apple.com/arkit/gallery), and cannot be reasonably supported by the converter.

*   Vertex colors.
*   Morph targets and vertex animation.
*   Texture filter modes. All textures are sampled linearly, with mipmapping.
*   Clamp and mirror texture wrap modes. All textures use repeat mode.
*   Cameras.
*   Shadow animation. Shadows are generated from the first frame.
*   Transparent shadows. This is particularly noticable for transparent geometry near the ground, which will appear very dark due to shadow falloff.
*   Multiple UV sets. The converter works around this by disabling textures using secondary UV sets, which works best for the most common-use case: AO.
*   Multiple animations. The converter just exports a single animation.
*   Multiple scenes. The converter just exports a single scene.
*   Transparency sorting. Overlapping transparent surfaces are likely to look incorrect.
*   Skinned animation for vertex normals. Lighting will look incorrect for skinned models, which is especially noticeable for highly reflective surfaces (it will have a painted-on appearance). The converter attempts to mitigate this by baking normals to the first frame of animation, but it will still look incorrect.

### Rendering Differences Between glTF and [AR Quick Look](https://developer.apple.com/arkit/gallery)
The [AR Quick Look](https://developer.apple.com/arkit/gallery) renderer does not precisely match the rendering model described by the glTF spec, but it is reasonably close. There are a few exceptions:

*   Occlusion (AO) is applied to the output color rather than ambient, so shadowed areas look a lot darker than in glTF. In some cases, this can cause the model to show up completely black, so the converter works around this by disabling all-black occlusion.
*   Transparent areas appear dark and washed-out, seemingly due to misapplication of premultiplied alpha.
*   Z-fighting with shadow geometry at zero height.

### Potential solutions to unsupported issues

*   Cameras, vertex animation, and vertex colors are supported by the [USD preview surface](https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html) spec, just not currently by [AR Quick Look](https://developer.apple.com/arkit/gallery). These should be added in the interest of completeness and future-proofing.
*   Emulate texture mirror wrap mode by mirroring the texture. This is simple, but can increase texture size up to 4x.
*   Emulate texture clamp wrap mode by clipping UVs. This involves relatively complicated clipping, but should not have a significant impact on model size.
*   Emulate vertex colors by baking them into the texture. This is difficult to do generally because it may involve re-uv-mapping the model - something better left to content authors. It can be simplified for certain special cases, though (e.g. untextured models with only vertex colors can use a simple color atlas).
*   Combine multiple UV sets. This is difficult to do because it requires re-uv-mapping the model - something better left to content authors.

## Performance

usd_from_gltf is roughly 10-15x faster than current alternatives.

The bulk of the conversion time is spent in image processing and recompression, necessary for emulating otherwise unsupported functionality in [AR Quick Look](https://developer.apple.com/arkit/gallery).

### Primary Optimizations
*   Implemented in native C++.
*   Supports multi-process model conversion through the ufgbatch.py script.
*   Can generate both USDA and USDZ files in a single pass.

### Benchmarks
Each benchmark was run 3 times on a Xeon E5-1650 @ 3.50GHz with 6 cores, 2x hyperthreaded for 12 hardware threads.

Converting 55 [glTF sample models](https://github.com/KhronosGroup/glTF-Sample-Models) to USDZ:

*   usd_from_gltf, 1 process: 20.1, 19.9, 19.9 (average: **20 seconds**)
*   usd_from_gltf, 12 processes: 6.9, 6.9, 6.7 (average: **6.8 seconds**)

Converting 28 complex skinned and animated glTF models to USDZ:

*   usd_from_gltf, 1 process: 22.6, 22.5, 22.6 (average: **22.6 seconds**)
*   usd_from_gltf, 12 processes: 5.0, 5.3, 5.1 (average: **5.2 seconds**)
