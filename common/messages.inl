/*
 * Copyright 2019 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef UFG_MSG0
#define UFG_MSG0(severity, id, format, ...) UFG_MSG(severity, id, format)
#define UFG_MSG1(severity, id, format, ...) UFG_MSG(severity, id, format)
#define UFG_MSG2(severity, id, format, ...) UFG_MSG(severity, id, format)
#define UFG_MSG3(severity, id, format, ...) UFG_MSG(severity, id, format)
#define UFG_MSG4(severity, id, format, ...) UFG_MSG(severity, id, format)
#define UFG_MSG5(severity, id, format, ...) UFG_MSG(severity, id, format)
#define UFG_MSG6(severity, id, format, ...) UFG_MSG(severity, id, format)
#endif  // UFG_MSG0

UFG_MSG3(ERROR, ASSERT                       , "%s(%d) : ASSERT(%s)", const char*, file, int, line, const char*, expression)
UFG_MSG1(ERROR, LOAD_PLUGINS                 , "Unable to load USD plugins. %s", const char*, why)
UFG_MSG1(ERROR, ARGUMENT_UNKNOWN             , "Unknown flag: %s", const char*, text)
UFG_MSG0(ERROR, ARGUMENT_PATHS               , "Non-even number of paths. Expected: src dst [src dst ...].")
UFG_MSG2(ERROR, ARGUMENT_EXCEPTION           , "%s: %s", const char*, id, const char*, err)
UFG_MSG1(ERROR, IO_WRITE_USD                 , "Cannot write USD: \"%s\"", const char*, path)
UFG_MSG1(ERROR, IO_WRITE_IMAGE               , "Cannot write image: \"%s\"", const char*, path)
UFG_MSG1(WARN , IO_DELETE                    , "Cannot delete file: %s", const char*, path)
UFG_MSG1(ERROR, STOMP                        , "Would stomp source file: \"%s\"", const char*, path)
UFG_MSG4(WARN , NON_TRIANGLES                , "Skipping unsupported %s primitive. Mesh: mesh[%zu].primitives[%zu], name=%s", const char*, prim_type, size_t, mesh_i, size_t, prim_i, const char*, name)
UFG_MSG3(WARN , TEXTURE_LIMIT                , "Can't make %zu texture(s) fit in decompressed limit (%zu bytes). Reducing to minimum (%zu bytes).", size_t, count, size_t, decompressed_limit, size_t, decompressed_total)
UFG_MSG2(ERROR, LAYER_CREATE                 , "Cannot create layer '%s' at: %s", const char*, src_name, const char*, dst_path)
UFG_MSG2(ERROR, USD                          , "USD: %s (%s)", const char*, commentary, const char*, function)
UFG_MSG2(WARN , USD                          , "USD: %s (%s)", const char*, commentary, const char*, function)
UFG_MSG2(INFO , USD                          , "USD: %s (%s)", const char*, commentary, const char*, function)
UFG_MSG2(ERROR, USD_FATAL                    , "USD: FATAL: %s (%s)", const char*, commentary, const char*, function)
UFG_MSG0(WARN , ALPHA_MASK_UNSUPPORTED       , "Alpha mask currently unsupported.")
UFG_MSG0(WARN , CAMERAS_UNSUPPORTED          , "Cameras currently unsupported.")
UFG_MSG0(WARN , MORPH_TARGETS_UNSUPPORTED    , "Morph targets currently unsupported.")
UFG_MSG0(WARN , MULTIPLE_UVSETS_UNSUPPORTED  , "Multiple UV sets unsupported on iOS viewer.")
UFG_MSG0(WARN , SECONDARY_UVSET_DISABLED     , "Disabling secondary UV set.")
UFG_MSG0(WARN , SPECULAR_WORKFLOW_UNSUPPORTED, "Specular workflow unsupported on iOS viewer.")
UFG_MSG2(WARN , TEXTURE_WRAP_UNSUPPORTED     , "Texture wrap mode [S=%s, T=%s] unsupported on iOS viewer.", const char*, s_mode, const char*, t_mode)
UFG_MSG2(WARN , TEXTURE_FILTER_UNSUPPORTED   , "Texture filter mode [min=%s, mag=%s] unsupported on iOS viewer.", const char*, min_filter, const char*, mag_filter)
UFG_MSG0(WARN , VERTEX_COLORS_UNSUPPORTED    , "Vertex colors currently unsupported.")
UFG_MSG0(ERROR, IMAGE_FALLBACK_DECODE        , "Failed decoding image with fallback decoder.")
UFG_MSG1(ERROR, GIF_RECORD_TYPE              , "GIF: Failed getting record type. Error code: %d", int, code)
UFG_MSG1(ERROR, GIF_IMAGE_DESC               , "GIF: Failed getting image desc. Error code: %d", int, code)
UFG_MSG1(ERROR, GIF_EXTENSION                , "GIF: Failed getting extension. Error code: %d", int, code)
UFG_MSG0(ERROR, GIF_BAD_GCB                  , "GIF: Invalid Graphics Control Block (GCB).")
UFG_MSG0(ERROR, GIF_NO_RECORDS               , "GIF: No image records.")
UFG_MSG1(ERROR, GIF_OPEN                     , "GIF: Failed opening with error code: %d", int, code)
UFG_MSG2(ERROR, GIF_BAD_SIZE                 , "GIF: Unexpected image size: <%d, %d>", int, width, int, height)
UFG_MSG6(ERROR, GIF_FRAME_BOUNDS             , "GIF: Frame 0 bounds <%u, %u>-<%u, %u> exceeds image size <%u, %u>", uint32_t, x0, uint32_t, y0, uint32_t, x1, uint32_t, y1, uint32_t, width, uint32_t, height)
UFG_MSG1(ERROR, JPG_DECODE_HEADER            , "JPG: Failed decoding header with error: %s", const char*, info)
UFG_MSG1(ERROR, JPG_DECOMPRESS               , "JPG: Failed decompressing with error: %s", const char*, info)
UFG_MSG1(ERROR, JPG_COMPRESS                 , "JPG: Failed compressing with error: %s", const char*, info)
UFG_MSG1(ERROR, PNG_DECODE                   , "PNG: %s", const char*, info)
UFG_MSG1(WARN , PNG_DECODE                   , "PNG: %s", const char*, info)
UFG_MSG1(ERROR, PNG_ENCODE                   , "PNG: %s", const char*, info)
UFG_MSG1(WARN , PNG_ENCODE                   , "PNG: %s", const char*, info)
UFG_MSG0(ERROR, PNG_READ_INIT                , "PNG: Failed to initialize for read.")
UFG_MSG2(ERROR, PNG_READ_SHORT               , "PNG: Attempt to read %zu bytes, only %zu remain.", size_t, size, size_t, remain)
UFG_MSG0(ERROR, PNG_WRITE_INIT               , "PNG: Failed to initialize for write.")
UFG_MSG3(ERROR, DRACO_UNKNOWN                , "Draco: Cannot determine geometry type for mesh: mesh[%zu].primitives[%zu], name=%s", size_t, mesh_i, size_t, prim_i, const char*, name)
UFG_MSG3(ERROR, DRACO_LOAD                   , "Draco: Cannot load data for mesh: mesh[%zu].primitives[%zu], name=%s", size_t, mesh_i, size_t, prim_i, const char*, name)
UFG_MSG3(ERROR, DRACO_DECODE                 , "Draco: Failed to decode mesh: mesh[%zu].primitives[%zu], name=%s", size_t, mesh_i, size_t, prim_i, const char*, name)
UFG_MSG3(ERROR, DRACO_NON_TRIANGLES          , "Draco: Non-triangular mesh: mesh[%zu].primitives[%zu], name=%s", size_t, mesh_i, size_t, prim_i, const char*, name)

#undef UFG_MSG
#undef UFG_MSG0
#undef UFG_MSG1
#undef UFG_MSG2
#undef UFG_MSG3
#undef UFG_MSG4
#undef UFG_MSG5
#undef UFG_MSG6
