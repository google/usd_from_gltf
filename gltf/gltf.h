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

#ifndef GLTF_GLTF_H_
#define GLTF_GLTF_H_

#include <stdint.h>
#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

struct Gltf {
  // Integer identifier used for internal glTF index references.
  enum class Id : uint16_t {
    kNull = 0xffff
  };

  inline static constexpr size_t IdToIndex(Id id) {
    return static_cast<size_t>(id);
  }
  inline static constexpr Id IndexToId(size_t index) {
    return static_cast<Id>(index);
  }

  template <typename T>
  inline static const T* GetById(const std::vector<T>& values, Id id) {
    const size_t index = static_cast<size_t>(id);
    return index < values.size() ? &values[index] : nullptr;
  }

  template <typename T>
  inline static T* GetById(std::vector<T>& values, Id id) {
    const size_t index = static_cast<size_t>(id);
    return index < values.size() ? &values[index] : nullptr;
  }

  template <typename T>
  inline static bool IsValidId(const std::vector<T>& values, Id id) {
    const size_t index = static_cast<size_t>(id);
    return index < values.size();
  }

  // Partial replacement for std::optional (which is not available pre C++17).
  template <typename T>
  class Optional {
   public:
    Optional() : exists_(false) {}
    Optional(const T& value) : exists_(false) { *this = value; }
    Optional(T&& value) : exists_(false) { *this = value; }
    Optional& operator=(std::nullptr_t) { exists_ = false; return *this; }
    Optional& operator=(const T& value) {
      exists_ = true;
      value_ = value;
      return *this;
    }
    Optional& operator=(T&& value) {
      exists_ = true;
      value_ = value;
      return *this;
    }
    T* operator->() { return exists_ ? &value_ : nullptr; }
    const T* operator->() const { return exists_ ? &value_ : nullptr; }
    T& operator*() { return value_; }
    const T& operator*() const { return value_; }
    operator bool() const { return exists_; }
    void Reset() { exists_ = false; }
    void Swap(Optional* other) {
      std::swap(exists_, other->exists_);
      std::swap(value_, other->value_);
    }

   private:
    bool exists_;
    T value_;
  };

  struct Uri {
    // Used for non-data URIs.
    std::string path;

    // Data URI type and binary.
    enum DataType : uint8_t {
      kDataTypeNone,        // No binary data, use path instead.
      kDataTypeBin,         // "data:application/octet-stream"
      kDataTypeImageJpeg,   // "data:image/jpeg"
      kDataTypeImagePng,    // "data:image/png"
      kDataTypeImageBmp,    // "data:image/bmp"
      kDataTypeImageGif,    // "data:image/gif"
      kDataTypeImageOther,  // "data:image/" <other>
      kDataTypeUnknown,     // "data:" <other>
      kDataTypeCount
    } data_type;
    std::vector<uint8_t> data;

    bool IsSet() const { return data_type != kDataTypeNone || !path.empty(); }
  };

  // Known extensions supported by this loader. All other extensions will be
  // skipped (with a warning).
  enum ExtensionId : uint8_t {
    kExtensionUnknown,
    kExtensionUnlit,             // KHR_materials_unlit
    kExtensionSpecGloss,         // KHR_materials_pbrSpecularGlossiness
    kExtensionTextureTransform,  // KHR_texture_transform
    kExtensionDraco,             // KHR_draco_mesh_compression
    kExtensionCount
  };

  // Metadata about the glTF asset.
  struct Asset {
    // [Optional] A copyright message suitable for display to credit the content
    // creator.
    std::string copyright;
    // [Optional] Tool that generated this glTF model. Useful for debugging.
    std::string generator;
    // [Required] The glTF version that this asset targets.
    std::string version;
    // [Optional] The minimum glTF version that this asset targets.
    std::string minVersion;

    void Clear();
    void Swap(Asset* other);
    bool IsSupportedVersion() const;
  };

  // A typed view into a bufferView. A bufferView contains raw binary data. An
  // accessor provides a typed view into a bufferView or a subset of a
  // bufferView similar to how WebGL's `vertexAttribPointer()` defines an
  // attribute in a buffer.
  struct Accessor {
    // [Optional] Accessor name.
    std::string name;
    // [Optional] The index of the bufferView. When not defined, accessor must
    // be initialized with zeros; `sparse` property or extensions could override
    // zeros with actual values.
    Id bufferView = Id::kNull;
    // [Optional] Specifies whether integer data values should be normalized
    // (`true`) to [0, 1] (for unsigned types) or [-1, 1] (for signed types), or
    // converted directly (`false`) when they are accessed. This property is
    // defined only for accessors that contain vertex attributes or animation
    // output data.
    bool normalized = false;
    // [Optional] The offset relative to the start of the bufferView in bytes.
    // This must be a multiple of the size of the component datatype.
    // * Requires bufferView.
    uint32_t byteOffset = 0;

    // [Required] The datatype of components in the attribute. All valid values
    // correspond to WebGL enums. The corresponding typed arrays are
    // `Int8Array`, `Uint8Array`, `Int16Array`, `Uint16Array`, `Uint32Array`,
    // and `Float32Array`, respectively. 5125 (UNSIGNED_INT) is only allowed
    // when the accessor contains indices, i.e., the accessor is only referenced
    // by `primitive.indices`.
    enum ComponentType : uint8_t {
      kComponentUnset,
      kComponentByte,           // "BYTE"=5120
      kComponentUnsignedByte,   // "UNSIGNED_BYTE"=5121
      kComponentShort,          // "SHORT"=5122
      kComponentUnsignedShort,  // "UNSIGNED_SHORT"=5123
      kComponentUnsignedInt,    // "UNSIGNED_INT"=5125
      kComponentFloat,          // "FLOAT"=5126
      kComponentCount
    } componentType = kComponentUnset;

    // [Required] Specifies if the attribute is a scalar, vector, or matrix.
    enum Type : uint8_t {
      kTypeUnset,
      kTypeScalar,  // "SCALAR"
      kTypeVec2,    // "VEC2"
      kTypeVec3,    // "VEC3"
      kTypeVec4,    // "VEC4"
      kTypeMat2,    // "MAT2"
      kTypeMat3,    // "MAT3"
      kTypeMat4,    // "MAT4"
      kTypeCount
    } type = kTypeUnset;

    // [Required] The number of attributes referenced by this accessor, not to
    // be confused with the number of bytes or number of components.
    uint32_t count = 0;

    static constexpr size_t kComponentMax = 16;
    union Value {
      int32_t i[kComponentMax];
      uint32_t u[kComponentMax];
      float f[kComponentMax];
    };
    static const Value kValueZero;

    // [Optional] Minimum value of each component in this attribute. Array
    // elements must be treated as having the same data type as accessor's
    // `componentType`. Both min and max arrays have the same length. The
    // length is determined by the value of the type property; it can be 1, 2,
    // 3, 4, 9, or 16.
    // `normalized` property has no effect on array values:
    // they always correspond to the actual values stored in the buffer. When
    // accessor is sparse, this property must contain min values of accessor
    // data with sparse substitution applied.
    Value min = kValueZero;
    // [Optional] Maximum value of each component in this attribute. Array
    // elements must be treated as having the same data type as accessor's
    // `componentType`. Both min and max arrays have the same length. The
    // length is determined by the value of the type property; it can be 1, 2,
    // 3, 4, 9, or 16.
    // `normalized` property has no effect on array values:
    // they always correspond to the actual values stored in the buffer. When
    // accessor is sparse, this property must contain max values of accessor
    // data with sparse substitution applied.
    Value max = kValueZero;

    // [Optional] Sparse storage of attributes that deviate from their
    // initialization value.
    // * count is 0 when this field is not present.
    struct Sparse {
      // [Required] The number of attributes encoded in this sparse accessor.
      uint32_t count = 0;

      // [Required] Index array of size `count` that points to those accessor
      // attributes that deviate from their initialization value. Indices must
      // strictly increase.
      struct Indices {
        // [Required] The index of the bufferView with sparse indices.
        // Referenced bufferView can't have ARRAY_BUFFER or ELEMENT_ARRAY_BUFFER
        // target.
        Id bufferView = Id::kNull;
        // [Required] The indices data type. Valid values correspond to WebGL
        // enums: `5121` (UNSIGNED_BYTE), `5123` (UNSIGNED_SHORT),
        // `5125` (UNSIGNED_INT).
        ComponentType componentType = ComponentType::kComponentUnset;
        // [Optional] The offset relative to the start of the bufferView in
        // bytes. Must be aligned.
        uint32_t byteOffset = 0;
      } indices;

      // [Required] Array of size `count` times number of components, storing
      // the displaced accessor attributes pointed by `indices`. Substituted
      // values must have the same `componentType` and number of components as
      // the base accessor.
      struct Values {
        // [Required] The index of the bufferView with sparse values. Referenced
        // bufferView can't have ARRAY_BUFFER or ELEMENT_ARRAY_BUFFER target.
        Id bufferView = Id::kNull;
        // [Optional] The offset relative to the start of the bufferView in
        // bytes. Must be aligned.
        uint32_t byteOffset = 0;
      } values;
    } sparse;
  };

  // A keyframe animation.
  struct Animation {
    // Targets an animation's sampler at a node's property.
    struct Channel {
      // [Required] The index of a sampler in this animation used to compute the
      // value for the target.
      Id sampler = Id::kNull;

      // [Required] The index of the node and TRS property that an animation
      // channel targets.
      struct Target {
        // [Optional] The index of the node to target.
        Id node = Id::kNull;

        // [Required] The name of the node's TRS property to modify.
        enum Path : uint8_t {
          kPathUnset,
          // Translation along the x, y, and z axes.
          kPathTranslation,  // "translation"
          // Quaternion in the order (x, y, z, w), where w is the scalar.
          kPathRotation,  // "rotation"
          // Values are the scaling factors along the x, y, and z axes.
          kPathScale,  // "scale"
          // Used for morph targets.
          kPathWeights,  // "weights"
          kPathCount
        } path = kPathUnset;
      } target;
    };

    // Combines input and output accessors with an interpolation algorithm to
    // define a keyframe graph (but not its target).
    struct Sampler {
      // [Required] The index of an accessor containing keyframe input values,
      // e.g., time. That accessor must have componentType `FLOAT`. The values
      // represent time in seconds with `time[0] >= 0.0`, and strictly
      // increasing values, i.e., `time[n + 1] > time[n]`.
      Id input = Id::kNull;

      // [Optional] Interpolation algorithm.
      enum Interpolation : uint8_t {
        // The animated values are linearly interpolated between keyframes. When
        // targeting a rotation, spherical linear interpolation (slerp) should
        // be used to interpolate quaternions. The number output of elements
        // must equal the number of input elements.
        kInterpolationLinear,  // "LINEAR"
        // The animated values remain constant to the output of the first
        // keyframe, until the next keyframe. The number of output elements must
        // equal the number of input elements.
        kInterpolationStep,  // "STEP"
        // The animation's interpolation is computed using a cubic spline with
        // specified tangents. The number of output elements must equal three
        // times the number of input elements. For each input element, the
        // output stores three elements, an in-tangent, a spline vertex, and an
        // out-tangent. There must be at least two keyframes when using this
        // interpolation.
        kInterpolationCubicSpline,  // "CUBICSPLINE"
        kInterpolationCount
      } interpolation = kInterpolationLinear;

      // [Required] The index of an accessor containing keyframe output values.
      // When targeting translation or scale paths, the `accessor.componentType`
      // of the output values must be `FLOAT`. When targeting rotation or morph
      // weights, the `accessor.componentType` of the output values must be
      // `FLOAT` or normalized integer. For weights, each output element stores
      // `SCALAR` values with a count equal to the number of morph targets.
      Id output = Id::kNull;
    };

    // [Optional] Animation name.
    std::string name;

    // [Required] An array of channels, each of which targets an animation's
    // sampler at a node's property. Different channels of the same animation
    // can't have equal targets.
    std::vector<Channel> channels;

    // [Required] An array of samplers that combines input and output accessors
    // with an interpolation algorithm to define a keyframe graph (but not its
    // target).
    std::vector<Sampler> samplers;
  };

  // A buffer points to binary geometry, animation, or skins.
  struct Buffer {
    // [Optional] The name of the buffer.
    std::string name;
    // [Optional] The uri of the buffer. Relative paths are relative to the
    // .gltf file. Instead of referencing an external file, the uri can also be
    // a data-uri.
    Uri uri;
    // [Required] The length of the buffer in bytes.
    uint32_t byteLength = 0;
  };

  // A view into a buffer generally representing a subset of the buffer.
  struct BufferView {
    // [Optional] The name of the buffer view.
    std::string name;
    // [Required] The index of the buffer.
    Id buffer = Id::kNull;
    // [Optional] The offset into the buffer in bytes.
    uint32_t byteOffset = 0;
    // [Required] The total byte length of the buffer view.
    uint32_t byteLength = 0;
    // [Optional] The stride, in bytes, between vertex attributes. When this is
    // not defined, data is tightly packed. When two or more accessors use the
    // same bufferView, this field must be defined.
    uint8_t byteStride = 0;
    // [Optional] The target that the GPU buffer should be bound to.
    enum Target : uint8_t {
      kTargetUnset,
      kTargetArrayBuffer,         // ARRAY_BUFFER=34962
      kTargetElementArrayBuffer,  // ELEMENT_ARRAY_BUFFER=34963
      kTargetCount
    } target = kTargetUnset;
  };

  // A camera's projection. A node can reference a camera to apply a transform
  // to place the camera in the scene.
  struct Camera {
    // [Optional] The name of the camera.
    std::string name;

    // [Required] Specifies if the camera uses a perspective or orthographic
    // projection. Based on this, either the camera's `perspective` or
    // `orthographic` property will be defined.
    enum Type : uint8_t {
      kTypePerspective,   // perspective
      kTypeOrthographic,  // orthographic
      kTypeCount
    } type;

    // An orthographic camera containing properties to create an orthographic
    // projection matrix.
    struct Orthographic {
      // [Required] The floating-point horizontal magnification of the view.
      // Must not be zero.
      float xmag;
      // [Required] The floating-point vertical magnification of the view. Must
      // not be zero.
      float ymag;
      // [Required] The floating-point distance to the far clipping plane.
      // `zfar` must be greater than `znear`.
      float zfar;
      // [Required] The floating-point distance to the near clipping plane.
      float znear;

      static const Orthographic kDefault;
    };

    // A perspective camera containing properties to create a perspective
    // projection matrix.
    struct Perspective {
      // [Optional] The floating-point aspect ratio of the field of view. When
      // this is undefined, the aspect ratio of the canvas is used.
      float aspectRatio;
      // [Required] The floating-point vertical field of view in radians.
      float yfov;
      // [Optional] The floating-point distance to the far clipping plane. When
      // defined, `zfar` must be greater than `znear`. If `zfar` is undefined,
      // runtime must use infinite projection matrix.
      float zfar;
      // [Required] The floating-point distance to the near clipping plane.
      float znear;

      static const Perspective kDefault;
    };

    // [Required] One of these is set, according to type.
    union {
      // An orthographic camera containing properties to create an orthographic
      // projection matrix.
      Orthographic orthographic;
      // A perspective camera containing properties to create a perspective
      // projection matrix.
      Perspective perspective;
    };

    Camera() : type(kTypeOrthographic), orthographic(Orthographic::kDefault) {}
  };

  // Image data used to create a texture. Image can be referenced by URI or
  // `bufferView` index. `mimeType` is required in the latter case.
  struct Image {
    // [Optional] Image name.
    std::string name;
    // [Optional] The uri of the image. Relative paths are relative to the .gltf
    // file. Instead of referencing an external file, the uri can also be a
    // data-uri. The image format must be jpg or png.
    Uri uri;
    // [Optional] The image's MIME type. Required if `bufferView` is defined.
    enum MimeType : uint8_t {
      kMimeUnset,
      kMimeJpeg,   // "image/jpeg"
      kMimePng,    // "image/png"
      kMimeBmp,    // "image/bmp"
      kMimeGif,    // "image/gif"
      kMimeOther,  // "image/" <other>
      kMimeCount
    } mimeType = kMimeUnset;
    // [Optional] The index of the bufferView that contains the image. Use this
    // instead of the image's uri property.
    Id bufferView = Id::kNull;
  };

  // The material appearance of a primitive.
  struct Material {
    // Reference to a texture.
    struct Texture {
      // [Required] The index of the texture.
      Id index = Id::kNull;
      // [Optional] This integer value is used to construct a string in the
      // format `TEXCOORD_<set index>` which is a reference to a key in
      // mesh.primitives.attributes (e.g. A value of `0` corresponds to
      // `TEXCOORD_0`). Mesh must have corresponding texture coordinate
      // attributes for the material to be applicable to it.
      uint8_t texCoord = 0;

      // [Optional] "KHR_texture_transform": glTF extension that enables
      // shifting and scaling UV coordinates on a per-texture basis.
      struct Transform {
        // [Optional] The offset of the UV coordinate origin as a factor of the
        // texture dimensions.
        float offset[2] = {0.0f, 0.0f};
        // [Optional] Rotate the UVs by this many radians counter-clockwise
        // around the origin.
        float rotation = 0.0f;
        // [Optional] The scale factor applied to the components of the UV
        // coordinates.
        float scale[2] = {1.0f, 1.0f};

        bool IsIdentity() const {
          return
              offset[0] == 0.0f && offset[1] == 0.0f &&
              rotation == 0.0f &&
              scale[0] == 1.0f && scale[1] == 1.0f;
        }
      } transform;
    };

    // [Optional] Material name.
    std::string name;

    // [Optional] A set of parameter values that are used to define the
    // metallic-roughness material model from Physically-Based Rendering (PBR)
    // methodology. When not specified, all the default values of
    // `pbrMetallicRoughness` apply.
    struct Pbr {
      // [Optional] The RGBA components of the base color of the material. The
      // fourth component (A) is the alpha coverage of the material. The
      // `alphaMode` property specifies how alpha is interpreted. These values
      // are linear. If a baseColorTexture is specified, this value is
      // multiplied with the texel values.
      float baseColorFactor[4] = {1, 1, 1, 1};
      // [Optional] The base color texture. This texture contains RGB(A)
      // components in sRGB color space. The first three components (RGB)
      // specify the base color of the material. If the fourth component (A) is
      // present, it represents the alpha coverage of the material. Otherwise,
      // an alpha of 1.0 is assumed. The `alphaMode` property specifies how
      // alpha is interpreted. The stored texels must not be premultiplied.
      Texture baseColorTexture;
      // [Optional] The metalness of the material. A value of 1.0 means the
      // material is a metal. A value of 0.0 means the material is a dielectric.
      // Values in between are for blending between metals and dielectrics such
      // as dirty metallic surfaces. This value is linear. If a
      // metallicRoughnessTexture is specified, this value is multiplied with
      // the metallic texel values.
      float metallicFactor = 1.0f;
      // [Optional] The roughness of the material. A value of 1.0 means the
      // material is completely rough. A value of 0.0 means the material is
      // completely smooth. This value is linear. If a metallicRoughnessTexture
      // is specified, this value is multiplied with the roughness texel values.
      float roughnessFactor = 1.0f;
      // [Optional] The metallic-roughness texture. The metalness values are
      // sampled from the B channel. The roughness values are sampled from the G
      // channel. These values are linear. If other channels are present (R or
      // A), they are ignored for metallic-roughness calculations.
      Texture metallicRoughnessTexture;

      // [Optional] "KHR_materials_pbrSpecularGlossiness": glTF extension that
      // defines the specular-glossiness material model from Physically-Based
      // Rendering (PBR) methodology.
      struct SpecGloss {
        // [Optional] The RGBA components of the reflected diffuse color of the
        // material. Metals have a diffuse value of `[0.0, 0.0, 0.0]`. The
        // fourth component (A) is the alpha coverage of the material. The
        // `alphaMode` property specifies how alpha is interpreted. The values
        // are linear.
        float diffuseFactor[4] = {1, 1, 1, 1};
        // [Optional] The diffuse texture. This texture contains RGB(A)
        // components of the reflected diffuse color of the material in sRGB
        // color space. If the fourth component (A) is present, it represents
        // the alpha coverage of the material. Otherwise, an alpha of 1.0 is
        // assumed. The `alphaMode` property specifies how alpha is interpreted.
        // The stored texels must not be premultiplied.
        Texture diffuseTexture;
        // [Optional] The specular RGB color of the material. This value is
        // linear.
        float specularFactor[3] = {1, 1, 1};
        // [Optional] The glossiness or smoothness of the material. A value
        // of 1.0 means the material has full glossiness or is perfectly smooth.
        // A value of 0.0 means the material has no glossiness or is completely
        // rough. This value is linear.
        float glossinessFactor = 1.0f;
        // [Optional] The specular-glossiness texture is RGBA texture,
        // containing the specular color of the material (RGB components) and
        // its glossiness (A component). The values are in sRGB space.
        Texture specularGlossinessTexture;
      };
      Optional<SpecGloss> specGloss;
    } pbr;

    // [Optional] A tangent space normal map. The texture contains RGB
    // components in linear space. Each texel represents the XYZ components of a
    // normal vector in tangent space. Red [0 to 255] maps to X [-1 to 1]. Green
    // [0 to 255] maps to Y [-1 to 1]. Blue [128 to 255] maps to Z [1/255 to 1].
    // The normal vectors use OpenGL conventions where +X is right and +Y is up.
    // +Z points toward the viewer. In GLSL, this vector would be unpacked like
    // so: `float3 normalVector = tex2D(<sampled normal map texture value>,
    // texCoord) * 2 - 1`. Client implementations should normalize the normal
    // vectors before using them in lighting equations.
    struct NormalTexture : Texture {
      // [Optional] The scalar multiplier applied to each normal vector of the
      // texture. This value scales the normal vector using the formula:
      // `scaledNormal =  normalize((<sampled normal texture value> * 2.0 - 1.0)
      // * vec3(<normal scale>, <normal scale>, 1.0))`. This value is ignored if
      // normalTexture is not specified. This value is linear.
      float scale = 1.0f;
    } normalTexture;

    // [Optional] The occlusion map texture. The occlusion values are sampled
    // from the R channel. Higher values indicate areas that should receive full
    // indirect lighting and lower values indicate no indirect lighting. These
    // values are linear. If other channels are present (GBA), they are ignored
    // for occlusion calculations.
    struct OcclusionTexture : Texture {
      // A scalar multiplier controlling the amount of occlusion applied. A
      // value of 0.0 means no occlusion. A value of 1.0 means full occlusion.
      // This value affects the resulting color using the formula:
      // `occludedColor = lerp(color, color * <sampled occlusion texture value>,
      // <occlusion strength>)`. This value is ignored if the corresponding
      // texture is not specified. This value is linear.
      float strength = 1.0f;
    } occlusionTexture;

    // [Optional] The emissive map controls the color and intensity of the light
    // being emitted by the material. This texture contains RGB components in
    // sRGB color space. If a fourth component (A) is present, it is ignored.
    Texture emissiveTexture;

    // [Optional] The RGB components of the emissive color of the material.
    // These values are linear. If an emissiveTexture is specified, this value
    // is multiplied with the texel values.
    float emissiveFactor[3] = {0, 0, 0};

    // [Optional] The material's alpha rendering mode enumeration specifying the
    // interpretation of the alpha value of the main factor and texture.
    enum AlphaMode : uint8_t {
      // The alpha value is ignored and the rendered output is fully opaque.
      kAlphaModeOpaque,  // "OPAQUE"
      // The rendered output is either fully opaque or fully transparent
      // depending on the alpha value and the specified alpha cutoff value.
      kAlphaModeMask,  // "MASK"
      // The alpha value is used to composite the source and destination areas.
      // The rendered output is combined with the background using the normal
      // painting operation (i.e. the Porter and Duff over operator).
      kAlphaModeBlend,  // "BLEND"
      kAlphaModeCount
    } alphaMode = kAlphaModeOpaque;

    // [Optional] Specifies the cutoff threshold when in `MASK` mode. If the
    // alpha value is greater than or equal to this value then it is rendered as
    // fully opaque, otherwise, it is rendered as fully transparent. A value
    // greater than 1.0 will render the entire material as fully transparent.
    // This value is ignored for other modes.
    float alphaCutoff = 0.5f;

    // [Optional] Specifies whether the material is double sided. When this
    // value is false, back-face culling is enabled. When this value is true,
    // back-face culling is disabled and double sided lighting is enabled. The
    // back-face must have its normals reversed before the lighting equation is
    // evaluated.
    bool doubleSided = false;

    // [Optional] "KHR_materials_unlit": glTF extension that defines the unlit
    // material model.
    bool unlit = false;

    // The maximum number of textures in this structure.
    static constexpr size_t kTextureMax = 7;

    size_t GetTextures(const Texture* (&out_textures)[kTextureMax]) const;
  };

  // A set of primitives to be rendered. A node can contain one mesh. A node's
  // transform places the mesh in the scene.
  struct Mesh {
    enum Semantic : uint8_t {
      kSemanticPosition,  // "POSITION"
      kSemanticNormal,    // "NORMAL"
      kSemanticTangent,   // "TANGENT"
      kSemanticTexcoord,  // "TEXCOORD_[n]"
      kSemanticColor,     // "COLOR_[n]"
      kSemanticJoints,    // "JOINTS_[n]"
      kSemanticWeights,   // "WEIGHTS_[n]"
      kSemanticCount
    };

    struct Attribute {
      using Number = uint8_t;
      Semantic semantic;
      Number number;
      Id accessor;
      explicit Attribute(Semantic semantic = kSemanticCount, Number number = 0,
                         Id accessor = Id::kNull)
          : semantic(semantic), number(number), accessor(accessor) {}
      friend bool operator<(const Attribute& a, const Attribute& b) {
        return a.semantic < b.semantic ||
               (a.semantic == b.semantic && a.number < b.number);
      }
      friend bool operator==(const Attribute& a, const Attribute& b) {
        return a.semantic == b.semantic && a.number == b.number;
      }
      friend bool operator!=(const Attribute& a, const Attribute& b) {
        return !(a == b);
      }
      std::string ToString() const;
    };

    // Convenience constants for common attributes.
    static const Attribute kAttributePosition;
    static const Attribute kAttributeNormal;
    static const Attribute kAttributeTangent;
    static const Attribute kAttributeTexcoord0;
    static const Attribute kAttributeColor0;
    static const Attribute kAttributeJoints0;
    static const Attribute kAttributeWeights0;

    // Mapping from semantic to data accessor.
    using AttributeSet = std::set<Attribute>;

    // Geometry to be rendered with the given material.
    struct Primitive {
      // [Required] A dictionary object, where each key corresponds to mesh
      // attribute semantic and each value is the index of the accessor
      // containing attribute's data.
      AttributeSet attributes;
      // [Optional] The index of the accessor that contains mesh indices. When
      // this is not defined, the primitives should be rendered without indices
      // using `drawArrays()`. When defined, the accessor must contain indices:
      // the `bufferView` referenced by the accessor should have a `target`
      // equal to 34963 (ELEMENT_ARRAY_BUFFER); `componentType` must be 5121
      // (UNSIGNED_BYTE), 5123 (UNSIGNED_SHORT) or 5125 (UNSIGNED_INT), the
      // latter may require enabling additional hardware support; `type` must be
      // `\"SCALAR\"`. For triangle primitives, the front face has a
      // counter-clockwise (CCW) winding order.
      Id indices = Id::kNull;
      // [Optional] The index of the material to apply to this primitive when
      // rendering.
      Id material = Id::kNull;
      // [Optional] The type of primitives to render. All valid values
      // correspond to WebGL enums.
      enum Mode : uint8_t {
        kModePoints,         // POINTS=0
        kModeLines,          // LINES=1
        kModeLineLoop,       // LINE_LOOP=2
        kModeLineStrip,      // LINE_STRIP=3
        kModeTriangles,      // TRIANGLES=4
        kModeTriangleStrip,  // TRIANGLE_STRIP=5
        kModeTriangleFan,    // TRIANGLE_FAN=6
        kModeCount
      } mode = kModeTriangles;
      // [Optional] An array of Morph Targets, each Morph Target is a
      // dictionary mapping attributes (only `POSITION`, `NORMAL`, and `TANGENT`
      // supported) to their deviations in the Morph Target.
      std::vector<AttributeSet> targets;

      // [Optional] "KHR_draco_mesh_compression": This extension defines a
      // schema to use Draco geometry compression (non-normative) libraries in
      // glTF format.
      // * bufferView is Id::kNull when this field is not present.
      struct Draco {
        // [Required] Points to the buffer containing compressed data.
        Id bufferView = Id::kNull;
        // [Required] Attributes defines the attributes stored in the
        // decompressed geometry. Each attribute is associated with an attribute
        // id which is its unique id in the compressed data. The attributes
        // defined in the extension must be a subset of the attributes of the
        // primitive.
        AttributeSet attributes;
      } draco;
    };

    // [Optional] Mesh name.
    std::string name;
    // [Required] An array of primitives, each defining geometry to be rendered
    // with a material.
    std::vector<Primitive> primitives;
    // [Optional] Array of weights to be applied to the Morph Targets.
    std::vector<float> weights;
  };

  // A node in the node hierarchy. When the node contains `skin`, all
  // `mesh.primitives` must contain `JOINTS_0` and `WEIGHTS_0` attributes. A
  // node can have either a `matrix` or any combination of
  // `translation`/`rotation`/`scale` (TRS) properties. TRS properties are
  // converted to matrices and postmultiplied in the `T * R * S` order to
  // compose the transformation matrix; first the scale is applied to the
  // vertices, then the rotation, and then the translation. If none are
  // provided, the transform is the identity. When a node is targeted for
  // animation (referenced by an animation.channel.target), only TRS properties
  // may be present; `matrix` will not be present.
  struct Node {
    // [Optional] Node name.
    std::string name;
    // [Optional] The index of the camera referenced by this node.
    Id camera = Id::kNull;
    // [Optional] The index of the mesh in this node.
    Id mesh = Id::kNull;
    // [Optional] The index of the skin referenced by this node.
    // * If provided, mesh must also be provided.
    Id skin = Id::kNull;

    // [Required] The transform must be set to either a matrix or SRT.
    bool is_matrix;
    union {
      struct {
        // A floating-point 4x4 transformation matrix stored in column-major
        // order.
        float matrix[16];
      };
      struct {
        // The node's non-uniform scale, given as the scaling factors along the
        // x, y, and z axes.
        float scale[3];
        // The node's unit quaternion rotation in the order (x, y, z, w), where
        // w is the scalar.
        float rotation[4];
        // The node's translation along the x, y, and z axes.
        float translation[3];
      };
    };

    // [Optional] The indices of this node's children.
    std::vector<Id> children;
    // [Optional] The weights of the instantiated Morph Target. Number of
    // elements must match number of Morph Targets of used mesh.
    // * If provided, mesh must also be provided.
    std::vector<float> weights;

    Node()
        : is_matrix(false),
          scale{1, 1, 1},
          rotation{0, 0, 0, 1},
          translation{0, 0, 0} {}
  };

  // Texture sampler properties for filtering and wrapping modes.
  struct Sampler {
    // [Optional] Sampler name.
    std::string name;

    // [Optional] Magnification filter. Valid values correspond to WebGL enums:
    // `9728` (NEAREST) and `9729` (LINEAR).
    enum MagFilter : uint8_t {
      kMagFilterUnset,
      kMagFilterNearest,  // "NEAREST"=9728
      kMagFilterLinear,   // "LINEAR"=9729
      kMagFilterCount
    } magFilter = kMagFilterUnset;

    // [Optional] Minification filter. All valid values correspond to WebGL
    // enums.
    enum MinFilter : uint8_t {
      kMinFilterUnset,
      kMinFilterNearest,               // "NEAREST"=9728
      kMinFilterLinear,                // "LINEAR"=9729
      kMinFilterNearestMipmapNearest,  // "NEAREST_MIPMAP_NEAREST"=9984
      kMinFilterLinearMipmapNearest,   // "LINEAR_MIPMAP_NEAREST"=9985
      kMinFilterNearestMipmapLinear,   // "NEAREST_MIPMAP_LINEAR"=9986
      kMinFilterLinearMipmapLinear,    // "LINEAR_MIPMAP_LINEAR"=9987
      kMinFilterCount,
    } minFilter = kMinFilterUnset;

    enum WrapMode : uint8_t {
      kWrapUnset,
      kWrapClamp,   // "CLAMP_TO_EDGE"=33071
      kWrapMirror,  // "MIRRORED_REPEAT"=33648
      kWrapRepeat,  // "REPEAT"=10497
      kWrapCount
    };

    // [Optional] S (U) wrapping mode. All valid values correspond to WebGL
    // enums.
    WrapMode wrapS = kWrapUnset;

    // [Optional] T (V) wrapping mode. All valid values correspond to WebGL
    // enums.
    WrapMode wrapT = kWrapUnset;
  };

  // The root nodes of a scene.
  struct Scene {
    // [Optional] Scene name.
    std::string name;
    // [Optional] The indices of each root node.
    std::vector<Id> nodes;
  };

  // Joints and matrices defining a skin.
  struct Skin {
    // [Optional] Skin name.
    std::string name;
    // [Optional] The index of the accessor containing the floating-point 4x4
    // inverse-bind matrices. The default is that each matrix is a 4x4 identity
    // matrix, which implies that inverse-bind matrices were pre-applied.
    Id inverseBindMatrices = Id::kNull;
    // [Optional] The index of the node used as a skeleton root. When undefined,
    // joints transforms resolve to scene root.
    Id skeleton = Id::kNull;
    // [Required] Indices of skeleton nodes, used as joints in this skin. The
    // array length must be the same as the `count` property of the
    // `inverseBindMatrices` accessor (when defined).
    std::vector<Id> joints;
  };

  // A texture and its sampler.
  struct Texture {
    // [Optional] Texture name.
    std::string name;
    // [Optional] The index of the sampler used by this texture. When undefined,
    // a sampler with repeat wrapping and auto filtering should be used.
    Id sampler = Id::kNull;
    // [Optional] The index of the image used by this texture.
    Id source = Id::kNull;
  };

  // [Required] Metadata about the glTF asset.
  Asset asset;
  // [Optional] The index of the default scene.
  Id scene = Id::kNull;
  // [Optional] Names of glTF extensions used somewhere in this asset.
  std::vector<ExtensionId> extensionsUsed;
  // [Optional] Names of glTF extensions required to properly load this asset.
  std::vector<ExtensionId> extensionsRequired;
  // [Optional] An array of accessors. An accessor is a typed view into a
  // bufferView.
  std::vector<Accessor> accessors;
  // [Optional] An array of keyframe animations.
  std::vector<Animation> animations;
  // [Optional] An array of buffers. A buffer points to binary geometry,
  // animation, or skins.
  std::vector<Buffer> buffers;
  // [Optional] An array of bufferViews. A bufferView is a view into a buffer
  // generally representing a subset of the buffer.
  std::vector<BufferView> bufferViews;
  // [Optional] An array of cameras. A camera defines a projection matrix.
  std::vector<Camera> cameras;
  // [Optional] An array of images. An image defines data used to create a
  // texture.
  std::vector<Image> images;
  // [Optional] An array of materials. A material defines the appearance of a
  // primitive.
  std::vector<Material> materials;
  // [Optional] An array of meshes. A mesh is a set of primitives to be
  // rendered.
  std::vector<Mesh> meshes;
  // [Optional] An array of nodes.
  std::vector<Node> nodes;
  // [Optional] An array of samplers. A sampler contains properties for texture
  // filtering and wrapping modes.
  std::vector<Sampler> samplers;
  // [Optional] An array of scenes.
  std::vector<Scene> scenes;
  // [Optional] An array of skins. A skin is defined by joints and matrices.
  std::vector<Skin> skins;
  // [Optional] An array of textures.
  std::vector<Texture> textures;

  void Clear();
  void Swap(Gltf* other);

  // Get the name of a glTF object, generating it from ID if necessary.
  template <typename Value>
  static std::string GetName(
      const std::vector<Value>& values, Id id, const char* prefix) {
    const Value* const value = GetById(values, id);
    if (!value) {
      return std::string();
    }
    if (!value->name.empty()) {
      return value->name;
    }
    return std::string(prefix) + std::to_string(IdToIndex(id));
  }

  template <typename Enum>
  static bool IsUnsetOrDefault(Enum e);
  template <typename Enum>
  static Enum GetDefaultIfUnset(Enum e);

  // ExtensionId enum info.
  static const char* const kExtensionIdNames[kExtensionCount];
  static const char* const* GetEnumNames(ExtensionId, size_t* out_count) {
    *out_count = kExtensionCount;
    return kExtensionIdNames;
  }

  // Accessor::ComponentType enum info.
  enum ComponentFormat : uint8_t {
    kComponentFormatSignedInt,
    kComponentFormatUnsignedInt,
    kComponentFormatFloat,
    kComponentFormatCount
  };
  static const uint16_t kAccessorComponentTypeValues[Accessor::kComponentCount];
  static const char* const
      kAccessorComponentTypeNames[Accessor::kComponentCount];
  static const ComponentFormat
      kAccessorComponentTypeFormats[Accessor::kComponentCount];
  static const uint8_t kAccessorComponentTypeSizes[Accessor::kComponentCount];
  static const char* const* GetEnumNames(Accessor::ComponentType,
                                         size_t* out_count) {
    *out_count = Accessor::kComponentCount;
    return kAccessorComponentTypeNames;
  }
  static ComponentFormat GetComponentFormat(
      Accessor::ComponentType component_type) {
    return static_cast<size_t>(component_type) < Accessor::kComponentCount
               ? kAccessorComponentTypeFormats[component_type]
               : kComponentFormatCount;
  }
  static size_t GetComponentSize(Accessor::ComponentType component_type) {
    return static_cast<size_t>(component_type) < Accessor::kComponentCount
               ? kAccessorComponentTypeSizes[component_type]
               : 0;
  }
  static bool IsComponentUnsigned(Accessor::ComponentType component_type) {
    return component_type == Accessor::kComponentUnsignedByte ||
           component_type == Accessor::kComponentUnsignedShort ||
           component_type == Accessor::kComponentUnsignedInt;
  }

  // Accessor::Type enum info.
  static const char* const kAccessorTypeNames[Accessor::kTypeCount];
  static const uint8_t kAccessorTypeComponentCounts[Accessor::kTypeCount];
  static const uint8_t kAccessorTypeCounts[Accessor::kTypeCount];
  static const char* const* GetEnumNames(Accessor::Type, size_t* out_count) {
    *out_count = Accessor::kTypeCount;
    return kAccessorTypeNames;
  }
  static size_t GetComponentCount(Accessor::Type type) {
    return type < Accessor::kTypeCount ? kAccessorTypeComponentCounts[type] : 0;
  }

  // Animation::Channel::Target::Path enum info.
  static const char* const
      kAnimationChannelTargetPathNames[Animation::Channel::Target::kPathCount];
  static const char* const* GetEnumNames(Animation::Channel::Target::Path,
                                         size_t* out_count) {
    *out_count = Animation::Channel::Target::kPathCount;
    return kAnimationChannelTargetPathNames;
  }

  // Animation::Sampler::Interpolation enum info.
  static const char* const kAnimationSamplerInterpolationNames
      [Animation::Sampler::kInterpolationCount];
  static const char* const* GetEnumNames(Animation::Sampler::Interpolation,
                                         size_t* out_count) {
    *out_count = Animation::Sampler::kInterpolationCount;
    return kAnimationSamplerInterpolationNames;
  }

  // BufferView::Target enum info.
  static const uint16_t kBufferViewTargetValues[BufferView::kTargetCount];
  static const char* const kBufferViewTargetNames[BufferView::kTargetCount];
  static const char* const* GetEnumNames(BufferView::Target,
                                         size_t* out_count) {
    *out_count = BufferView::kTargetCount;
    return kBufferViewTargetNames;
  }

  // Camera::Type enum info.
  static const char* const kCameraTypeNames[Camera::kTypeCount];
  static const char* const* GetEnumNames(Camera::Type, size_t* out_count) {
    *out_count = Camera::kTypeCount;
    return kCameraTypeNames;
  }

  // Image::MimeType enum info.
  static const char* const kImageMimeTypeNames[Image::kMimeCount];
  static const char* const kImageMimeTypeExtensions[Image::kMimeCount];
  static const char* const* GetEnumNames(Image::MimeType, size_t* out_count) {
    *out_count = Image::kMimeCount;
    return kImageMimeTypeNames;
  }
  static const char* GetMimeTypeExtension(Image::MimeType mime_type) {
    return mime_type < Image::kMimeCount ? kImageMimeTypeExtensions[mime_type]
                                         : nullptr;
  }
  static Image::MimeType FindImageMimeTypeByExtension(const char* ext);
  static Image::MimeType FindImageMimeTypeByPath(const std::string& path);
  static Image::MimeType FindImageMimeTypeByUri(const Uri& uri);

  // Material::AlphaMode enum info.
  static const char* const kMaterialAlphaModeNames[Material::kAlphaModeCount];
  static const char* const* GetEnumNames(Material::AlphaMode,
                                         size_t* out_count) {
    *out_count = Material::kAlphaModeCount;
    return kMaterialAlphaModeNames;
  }

  // Mesh::Semantic enum info.
  struct SemanticInfo {
    const char* prefix;
    uint8_t prefix_len;
    bool has_numeric_suffix;
    uint8_t component_min;
    uint8_t component_max;
  };
  static const SemanticInfo kSemanticInfos[Mesh::kSemanticCount];

  // Mesh::Primitive::Mode enum info.
  static const uint8_t kMeshPrimitiveModeValues[Mesh::Primitive::kModeCount];
  static const char* const kMeshPrimitiveModeNames[Mesh::Primitive::kModeCount];
  static const char* const* GetEnumNames(Mesh::Primitive::Mode,
                                         size_t* out_count) {
    *out_count = Mesh::Primitive::kModeCount;
    return kMeshPrimitiveModeNames;
  }
  static bool HasTriangles(Mesh::Primitive::Mode primitive);

  // Sampler::MagFilter enum info.
  static const uint16_t kSamplerMagFilterValues[Sampler::kMagFilterCount];
  static const char* const kSamplerMagFilterNames[Sampler::kMagFilterCount];
  static const char* const* GetEnumNames(Sampler::MagFilter,
                                         size_t* out_count) {
    *out_count = Sampler::kMagFilterCount;
    return kSamplerMagFilterNames;
  }

  // Sampler::MinFilter enum info.
  static const uint16_t kSamplerMinFilterValues[Sampler::kMinFilterCount];
  static const char* const kSamplerMinFilterNames[Sampler::kMinFilterCount];
  static const char* const* GetEnumNames(Sampler::MinFilter,
                                         size_t* out_count) {
    *out_count = Sampler::kMinFilterCount;
    return kSamplerMinFilterNames;
  }

  // Sampler::WrapMode enum info.
  static const uint16_t kSamplerWrapModeValues[Sampler::kWrapCount];
  static const char* const kSamplerWrapModeNames[Sampler::kWrapCount];
  static const char* const* GetEnumNames(Sampler::WrapMode, size_t* out_count) {
    *out_count = Sampler::kWrapCount;
    return kSamplerWrapModeNames;
  }

  template <typename Enum>
  static const char* GetEnumName(Enum e) {
    size_t count;
    const char* const* const names = GetEnumNames(Enum(), &count);
    return static_cast<size_t>(e) < count ? names[e] : nullptr;
  }

  template <typename Enum>
  static const char* GetEnumNameOrDefault(Enum e) {
    return GetEnumName(GetDefaultIfUnset(e));
  }

  static const Image::MimeType kUriDataImageMimeTypes[Uri::kDataTypeCount];
  static Image::MimeType GetUriDataImageMimeType(Uri::DataType data_type) {
    return data_type < Uri::kDataTypeCount ? kUriDataImageMimeTypes[data_type]
                                           : Image::kMimeUnset;
  }

  static Uri::DataType FindUriDataType(const char* name, size_t name_len);

  // Perform ordered comparison useful for map keys.
  static int Compare(const Material::Texture::Transform& a,
                     const Material::Texture::Transform& b);
  static int Compare(const Material::Texture& a,
                     const Material::Texture& b);
  static int Compare(const Material::Pbr::SpecGloss& a,
                     const Material::Pbr::SpecGloss& b);
  static int Compare(const Material::Pbr& a,
                     const Material::Pbr& b);
  static int Compare(const Material::NormalTexture& a,
                     const Material::NormalTexture& b);
  static int Compare(const Material::OcclusionTexture& a,
                     const Material::OcclusionTexture& b);
  static int Compare(const Material& a,
                     const Material& b);

  template <typename T>
  static int Compare(const T& a, const T& b) {
    return a == b ? 0 : (a < b ? -1 : 1);
  }

  template <typename T>
  static int Compare(const Optional<T> &a, const Optional<T> &b) {
    const bool a_exists = !a;
    const bool b_exists = !b;
    if (a_exists != b_exists) {
      return a_exists < b_exists ? -1 : 1;
    }
    return a_exists ? 0 : Compare(*a, *b);
  }

  template <size_t kCount>
  static int Compare(const float (&a)[kCount], const float (&b)[kCount]) {
    for (size_t i = 0; i != kCount; ++i) {
      if (a[i] != b[i]) {
        return a[i] < b[i] ? -1 : 1;
      }
    }
    return 0;
  }

  static void StringToLower(char* text) {
    for (char* s = text; *s; ++s) {
      *s = tolower(*s);
    }
  }

  static bool StringEqualCI(const char* text0, size_t len0,
                            const char* text1, size_t len1) {
    if (len0 != len1) {
      return false;
    }
    for (size_t i = 0; i != len0; ++i) {
      const int c0 = tolower(text0[i]);
      const int c1 = tolower(text1[i]);
      if (c0 != c1) {
        return false;
      }
    }
    return true;
  }

  static bool StringEqualCI(const char* text0, const char* text1) {
    for (size_t i = 0; ; ++i) {
      const int c0 = tolower(text0[i]);
      const int c1 = tolower(text1[i]);
      if (c0 != c1) {
        return false;
      }
      if (c0 == 0) {
        return true;
      }
    }
  }

  static bool StringBeginsWith(const char* text, size_t text_len,
                               const char* prefix, size_t prefix_len) {
    return prefix_len <= text_len && memcmp(text, prefix, prefix_len) == 0;
  }

  template <size_t kPrefixSize>
  static bool StringBeginsWith(const char* text, size_t text_len,
                               const char (&prefix)[kPrefixSize]) {
    return StringBeginsWith(text, text_len, prefix, kPrefixSize - 1);
  }

  static bool StringBeginsWithCI(const char* text, size_t text_len,
                                 const char* prefix, size_t prefix_len) {
    return prefix_len <= text_len &&
           StringEqualCI(text, prefix_len, prefix, prefix_len);
  }

  template <size_t kPrefixSize>
  static bool StringBeginsWithCI(const char* text, size_t text_len,
                                 const char (&prefix)[kPrefixSize]) {
    return StringBeginsWithCI(text, text_len, prefix, kPrefixSize - 1);
  }

  static bool StringEndsWithCI(const char* text, size_t text_len,
                               const char* suffix, size_t suffix_len) {
    return text_len >= suffix_len &&
        StringEqualCI(text + text_len - suffix_len, suffix_len,
                      suffix, suffix_len);
  }

  template <size_t kSuffixSize>
  static bool StringEndsWithCI(const char* text, size_t text_len,
                               const char (&suffix)[kSuffixSize]) {
    return StringEndsWithCI(text, text_len, suffix, kSuffixSize - 1);
  }

  static bool StringEndsWithCI(const char* text, const char* suffix) {
    return StringEndsWithCI(text, strlen(text), suffix, strlen(suffix));
  }

  static bool StringBeginsWithAny(
      const char* text, size_t text_len,
      const std::vector<std::string>& prefixes) {
    for (const std::string& prefix : prefixes) {
      if (Gltf::StringBeginsWith(
          text, text_len, prefix.c_str(), prefix.length())) {
        return true;
      }
    }
    return false;
  }

  static bool StringBeginsWithAnyCI(
      const char* text, size_t text_len,
      const std::vector<std::string>& prefixes) {
    for (const std::string& prefix : prefixes) {
      if (Gltf::StringBeginsWithCI(
          text, text_len, prefix.c_str(), prefix.length())) {
        return true;
      }
    }
    return false;
  }

  // Sanitize a path by replacing non-portable characters with '_'.
  // * Returns true if the path is changed.
  static bool SanitizePath(char* path);

  static std::string GetSanitizedPath(const char* path);

  inline static std::string JoinPath(const std::string& a,
                                     const std::string& b) {
    if (a.empty()) {
      return b;
    }
    std::string j = a;
    if (j.back() != '/' && j.back() != '\\') {
      j += '/';
    }
    j += b;
    return j;
  }

  inline static void SplitPath(
      const std::string& path, std::string* out_dir, std::string* out_name) {
    const size_t last_slash_pos = path.find_last_of("\\/");
    if (last_slash_pos == std::string::npos) {
      out_dir->clear();
      *out_name = path;
    } else {
      const size_t dir_len = last_slash_pos + 1;
      out_dir->assign(path.c_str(), dir_len);
      *out_name = path.c_str() + dir_len;
    }
  }

  // Gather the set of all URI paths referenced by this glTF file.
  // This does not include embedded data URIs or GLB blocks.
  std::vector<const char*> GetReferencedUriPaths() const;

  // Get extensions referenced in the glTF (excluding those in extensionsUsed or
  // extensionsRequired).
  const std::vector<Gltf::ExtensionId> GetReferencedExtensions() const;
};

template <typename Enum> struct GltfEnumInfo;

template <>
struct GltfEnumInfo<Gltf::Sampler::WrapMode> {
  static constexpr Gltf::Sampler::WrapMode kUnset =
      Gltf::Sampler::kWrapUnset;
  static constexpr Gltf::Sampler::WrapMode kDefault =
      Gltf::Sampler::kWrapRepeat;
};
template <>
struct GltfEnumInfo<Gltf::Sampler::MagFilter> {
  static constexpr Gltf::Sampler::MagFilter kUnset =
      Gltf::Sampler::kMagFilterUnset;
  static constexpr Gltf::Sampler::MagFilter kDefault =
      Gltf::Sampler::kMagFilterLinear;
};
template <>
struct GltfEnumInfo<Gltf::Sampler::MinFilter> {
  static constexpr Gltf::Sampler::MinFilter kUnset =
      Gltf::Sampler::kMinFilterUnset;
  static constexpr Gltf::Sampler::MinFilter kDefault =
      Gltf::Sampler::kMinFilterLinearMipmapLinear;
};

template <typename Enum>
inline bool Gltf::IsUnsetOrDefault(Enum e) {
  return e == GltfEnumInfo<Enum>::kUnset || e == GltfEnumInfo<Enum>::kDefault;
}

template <typename Enum>
inline Enum Gltf::GetDefaultIfUnset(Enum e) {
  return e == GltfEnumInfo<Enum>::kUnset ? GltfEnumInfo<Enum>::kDefault : e;
}

#endif  // GLTF_GLTF_H_
