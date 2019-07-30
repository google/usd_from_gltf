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

#include "args.h"  // NOLINT: Silence relative path warning.

#include "common/common_util.h"
#include "common/logging.h"
#include "gltf/disk_stream.h"
#include "tclap/CmdLine.h"

namespace {
// Get offset from the default conversion settings. This allows us to reference
// members of the default by name in Bind().
size_t GetDefaultOffset(const void* member) {
  const size_t offset =
      static_cast<const char*>(member) -
      reinterpret_cast<const char*>(&ufg::ConvertSettings::kDefault);
  UFG_ASSERT_LOGIC(offset <= sizeof(ufg::ConvertSettings));
  return offset;
}

template <typename T>
std::string ToString(const T& v) {
  return std::to_string(v);
}

const std::string& ToString(const std::string& v) {
  return v;
}

class ArgParser {
 public:
  ArgParser()
      : nousage_(false),
        output_(this),
        cmd_(""),
        paths_("paths",
               "Input glTF (.gltf) and output USD "
               "(.usd, .usdc, .usda, .usdz, or .usd-) pairs.",
               true, "path"),
        nousage_arg_("", "nousage", "Don't print usage on argument error.") {
    cmd_.setOutput(&output_);
    cmd_.setExceptionHandling(false);

    Bind();

    // For some reason TCLAP lists parameters in reverse, so add them in reverse
    // to correct this.
    cmd_.add(nousage_arg_);
    const size_t binder_count = binders_.size();
    for (size_t i = binder_count; i != 0; ) {
      --i;
      binders_[i]->Add(&cmd_);
    }
    cmd_.add(paths_);
  }

  bool Parse(
      int argc, const char* const* argv, Args* out_args, ufg::Logger* logger) {
    try {
      // Convert args to vector, replacing arg[0] with the short exe name.
      // * We also explicitly check for --nousage because we need this argument
      //   before parse completes.
      nousage_ = false;
      argc = std::max(argc, 1);
      std::vector<std::string> arg_vec(argc);
      arg_vec[0] = "usd_from_gltf";
      for (int i = 1; i != argc; ++i) {
        const char* const arg = argv[i];
        if (Gltf::StringEqualCI(arg, "--nousage")) {
          nousage_ = true;
        }
        arg_vec[i] = argv[i];
      }

      // Parse args.
      cmd_.reset();
      cmd_.parse(arg_vec);

      // The unnamed 'paths' argument acts as the catch-all, which unfortunately
      // means mistyped flags will also be treated as paths. Explicitly emit
      // errors for these.
      const std::vector<std::string>& paths = paths_.getValue();
      bool have_unknown_flags = false;
      for (const std::string& path : paths) {
        if (path.compare(0, 2, "--") == 0) {
          ufg::Log<ufg::UFG_ERROR_ARGUMENT_UNKNOWN>(logger, "", path.c_str());
          have_unknown_flags = true;
        }
      }
      if (have_unknown_flags) {
        return false;
      }

      // Get src/dst path pairs.
      if (paths.empty() || (paths.size() % 2) != 0) {
        ufg::Log<ufg::UFG_ERROR_ARGUMENT_PATHS>(logger, "");
        return false;
      }
      const size_t job_count = paths.size() / 2;
      out_args->jobs.resize(job_count);
      for (size_t job_index = 0; job_index != job_count; ++job_index) {
        Args::Job& job = out_args->jobs[job_index];
        job.src = paths[2 * job_index + 0];
        job.dst = paths[2 * job_index + 1];
      }

      // Apply arguments to settings.
      for (const std::unique_ptr<IBinder>& binder : binders_) {
        binder->Apply(out_args);
      }
      return true;
    } catch (const TCLAP::ArgException& e) {
      ufg::Log<ufg::UFG_ERROR_ARGUMENT_EXCEPTION>(
          logger, "", e.argId().c_str(), e.error().c_str());
      return false;
    } catch (const TCLAP::ExitException&) {
      return true;
    }
  }

  void PrintShortUsage() {
    output_.PrintShortUsage();
  }

 private:
  class IBinder {
   public:
    virtual ~IBinder() {}
    virtual void Add(TCLAP::CmdLine* cmd) = 0;
    virtual void Apply(Args* args) = 0;
  };

  class Output : public TCLAP::StdOutput {
   public:
    explicit Output(ArgParser* parser) : parser_(parser) {}
    void usage(TCLAP::CmdLineInterface& c) override {
      if (parser_->nousage_) {
        return;
      }
      PrintLongUsage();
    }

    void PrintShortUsage() const {
      if (parser_->nousage_) {
        return;
      }
      printf("Usage: \n");
      _shortUsage(parser_->cmd_, std::cout);
    }

    void PrintLongUsage() const {
      if (parser_->nousage_) {
        return;
      }
      printf("usd_from_gltf - Generate USD files from glTF.\n");
      PrintShortUsage();
      printf("Where: \n");
      _longUsage(parser_->cmd_, std::cout);
    }

   private:
    ArgParser* parser_;
  };

  bool nousage_;
  Output output_;
  TCLAP::CmdLine cmd_;
  std::vector<std::unique_ptr<IBinder>> binders_;
  TCLAP::UnlabeledMultiArg<std::string> paths_;
  TCLAP::SwitchArg nousage_arg_;

  // For switches, this adds an inverse 'no' flag (e.g. --all_nodes and
  // --noall_nodes).
  class SwitchBinder : public IBinder {
   public:
    SwitchBinder(const char* name, const char* desc, const bool* def)
        : name_(name),
          desc_(desc),
          offset_(GetDefaultOffset(def)),
          def_(*def) {}
    void Add(TCLAP::CmdLine* cmd) override {
      const std::string on_name = name_;
      const std::string off_name = "no" + on_name;
      const std::string on_desc =
          std::string(desc_) + (def_ ? " [Default]" : "");
      const std::string off_desc =
          "Disable --" + on_name + (!def_ ? ". [Default]" : ".");
      on_ = std::unique_ptr<TCLAP::SwitchArg>(
          new TCLAP::SwitchArg("", on_name, on_desc, def_));
      off_ = std::unique_ptr<TCLAP::SwitchArg>(
          new TCLAP::SwitchArg("", off_name, off_desc, !def_));
      cmd->add(*off_);
      cmd->add(*on_);
    }
    void Apply(Args* args) override {
      bool* const out_value = reinterpret_cast<bool*>(
          reinterpret_cast<char*>(&args->settings) + offset_);
      *out_value = def_ ? !off_->getValue() : on_->getValue();
    }

   private:
    const char* name_;
    const char* desc_;
    size_t offset_;
    bool def_;
    std::unique_ptr<TCLAP::SwitchArg> on_;
    std::unique_ptr<TCLAP::SwitchArg> off_;
  };

  static const char* GetValueTypeName(int) { return "int"; }
  static const char* GetValueTypeName(uint32_t) { return "uint"; }
  static const char* GetValueTypeName(uint8_t) { return "uint"; }
  static const char* GetValueTypeName(float) { return "float"; }
  static const char* GetValueTypeName(const std::string&) { return "string"; }

  template <typename T>
  static bool ValueExists(T) { return true; }
  static bool ValueExists(const std::string& v) { return !v.empty(); }

  template <typename DstType, typename ArgType>
  class ValueBinder : public IBinder {
    using Arg = TCLAP::ValueArg<ArgType>;

   public:
    ValueBinder(const char* name, const char* desc, const DstType* def)
        : name_(name),
          desc_(desc),
          offset_(GetDefaultOffset(def)),
          def_(*def) {}
    void Add(TCLAP::CmdLine* cmd) override {
      const std::string desc =
          std::string(desc_) + " [default=" + ToString(def_) + "]";
      arg_ = std::unique_ptr<Arg>(
          new Arg("", name_, desc, false, def_, GetValueTypeName(def_)));
      cmd->add(*arg_);
    }
    void Apply(Args* args) override {
      DstType* const out_value = reinterpret_cast<DstType*>(
          reinterpret_cast<char*>(&args->settings) + offset_);
      const DstType value = static_cast<DstType>(arg_->getValue());
      if (ValueExists(value)) {
        *out_value = static_cast<DstType>(arg_->getValue());
      }
    }

   private:
    const char* name_;
    const char* desc_;
    size_t offset_;
    DstType def_;
    std::unique_ptr<Arg> arg_;
  };
  using IntBinder = ValueBinder<int, int>;
  using UintBinder = ValueBinder<uint32_t, int>;
  using Uint8Binder = ValueBinder<uint8_t, int>;
  using FloatBinder = ValueBinder<float, float>;
  using StringBinder = ValueBinder<std::string, std::string>;

  class StringsBinder : public IBinder {
    using Arg = TCLAP::MultiArg<std::string>;

   public:
    StringsBinder(const char* name, const char* desc,
                  const std::vector<std::string>* def)
        : name_(name), desc_(desc), offset_(GetDefaultOffset(def)) {}
    void Add(TCLAP::CmdLine* cmd) override {
      arg_ = std::unique_ptr<Arg>(new Arg("", name_, desc_, false, "string"));
      cmd->add(*arg_);
    }
    void Apply(Args* args) override {
      const std::vector<std::string>& add_strings = arg_->getValue();
      const size_t add_string_count = add_strings.size();
      if (add_string_count > 0) {
        std::vector<std::string>* const out_strings =
            reinterpret_cast<std::vector<std::string>*>(
                reinterpret_cast<char*>(&args->settings) + offset_);
        out_strings->insert(out_strings->end(), add_strings.begin(),
                            add_strings.end());
      }
    }

   private:
    const char* name_;
    const char* desc_;
    size_t offset_;
    std::unique_ptr<Arg> arg_;
  };

  void Bind() {
    const ufg::ConvertSettings& def = ufg::ConvertSettings::kDefault;
    binders_.emplace_back(new SwitchBinder("all_nodes",
        "Export all nodes rather than a single scene.",
        &def.all_nodes));
    binders_.emplace_back(new IntBinder   ("scene",
        "Override default scene specified by glTF.",
        &def.scene_index));
    binders_.emplace_back(new IntBinder   ("anim",
        "Set the animation to export.",
        &def.anim_index));
    binders_.emplace_back(new FloatBinder ("root_scale",
        "Scale applied to the model root.",
        &def.root_scale));
    binders_.emplace_back(new FloatBinder ("limit_bounds",
        "Adjust scale to limit bounding-box size.",
        &def.limit_bounds));
    binders_.emplace_back(new Uint8Binder ("jpg_quality",
        "JPG compression quality [1=worst, 100=best].",
        &def.jpg_quality));
    binders_.emplace_back(new Uint8Binder ("jpg_subsamp",
        "JPG chroma subsampling method [0=best, 2=worst].",
        &def.jpg_subsamp));
    binders_.emplace_back(new Uint8Binder ("png_level",
        "PNG compression level [0=fastest, 9=smallest].",
        &def.png_level));
    binders_.emplace_back(new SwitchBinder("image_power_of_2",
        "Force images to power-of-2 in size when resizing.",
        &def.image_resize.force_power_of_2));
    binders_.emplace_back(new FloatBinder ("image_scale",
        "Global image resize scale.",
        &def.image_resize.scale));
    binders_.emplace_back(new UintBinder  ("image_size_min",
        "Minimum image size when resizing.",
        &def.image_resize.size_min));
    binders_.emplace_back(new UintBinder  ("image_size_max",
        "Maximum image size when resizing.",
        &def.image_resize.size_max));
    binders_.emplace_back(new UintBinder  ("image_limit_total",
        "Limit the total size of all images after decompression.",
        &def.limit_total_image_decompressed_size));
    binders_.emplace_back(new FloatBinder ("image_limit_step",
        "Step used when limiting total image size.",
        &def.limit_total_image_scale_step));
    binders_.emplace_back(new SwitchBinder("add_debug_bone_meshes",
        "Add debug meshes to each transform node to visualize animation.",
        &def.add_debug_bone_meshes));
    binders_.emplace_back(new SwitchBinder("bake_texture_color_scale_bias",
        "Bake texture color scale and bias into the texture.",
        &def.bake_texture_color_scale_bias));
    binders_.emplace_back(new SwitchBinder("bake_alpha_cutoff",
        "Bake alpha cutoff into textures.",
        &def.bake_alpha_cutoff));
    binders_.emplace_back(new SwitchBinder("bake_skin_normals",
        "Bake skinned vertex normals to the first frame of animation.",
        &def.bake_skin_normals));
    binders_.emplace_back(new SwitchBinder("black_occlusion_is_white",
        "If the occlusion channel is pure black, replace it with pure white.",
        &def.black_occlusion_is_white));
    binders_.emplace_back(new SwitchBinder("delete_unused",
        "Delete unused intermediate output files.",
        &def.delete_unused));
    binders_.emplace_back(new SwitchBinder("delete_generated",
        "Delete all generated files except the output usda/usdz file.",
        &def.delete_generated));
    binders_.emplace_back(new SwitchBinder("disable_multiple_uvsets",
        "Replace textures referencing secondary UV sets with a solid color.",
        &def.disable_multiple_uvsets));
    binders_.emplace_back(new SwitchBinder("emissive_is_linear",
        "Output emissive textures in linear space.",
        &def.emissive_is_linear));
    binders_.emplace_back(new SwitchBinder("emulate_double_sided",
        "Emulate double-sided geometry by duplicating single-sided geometry.",
        &def.emulate_double_sided));
    binders_.emplace_back(new SwitchBinder("emulate_specular_workflow",
        "Convert diffuse+specular+glossiness to albedo+metallic+roughness.",
        &def.emulate_specular_workflow));
    binders_.emplace_back(new SwitchBinder("fix_accidental_alpha",
        "Fix accidental alpha by ignoring edge pixels.",
        &def.fix_accidental_alpha));
    binders_.emplace_back(new SwitchBinder("fix_skinned_normals",
        "Work around iOS viewer not skinning normals.",
        &def.fix_skinned_normals));
    binders_.emplace_back(new SwitchBinder("merge_identical_materials",
        "Merge materials with identical parameters, irrespective of name.",
        &def.merge_identical_materials));
    binders_.emplace_back(new SwitchBinder("merge_skeletons",
        "Merge multiple skeletons into one.",
        &def.merge_skeletons));
    binders_.emplace_back(new SwitchBinder("normalize_normals",
        "Normalize normal map vectors.",
        &def.normalize_normals));
    binders_.emplace_back(new SwitchBinder("normalize_skin_scale",
        "Normalize the skin root joint scale to 1.0.",
        &def.normalize_skin_scale));
    binders_.emplace_back(new SwitchBinder("prefer_jpeg",
        "Prefer saving images as jpeg.",
        &def.prefer_jpeg));
    binders_.emplace_back(new SwitchBinder("print_timing",
        "Print conversion time stats.",
        &def.print_timing));
    binders_.emplace_back(new SwitchBinder("remove_invisible",
        "Remove geometry that's invisible due to material state.",
        &def.remove_invisible));
    binders_.emplace_back(new StringsBinder("remove_node_prefix",
        "Remove nodes matching any of these prefixes (case insensitive).",
        &def.remove_node_prefixes));
    binders_.emplace_back(new SwitchBinder("reverse_culling_for_inverse_scale",
        "Reverse polygon winding during conversion for inverse scale.",
        &def.reverse_culling_for_inverse_scale));
    binders_.emplace_back(new SwitchBinder("warn_ios_incompat",
        "Emit warnings for features that are incompatible with the iOS viewer.",
        &def.warn_ios_incompat));
    binders_.emplace_back(new StringsBinder("nowarn_extension",
        "Disable warnings for unrecognized glTF extensions.",
        &def.gltf_load_settings.nowarn_extension_prefixes));
    binders_.emplace_back(new StringBinder("plugin_path",
        "Paths to USD plugins (may contain wildcards).",
        &def.plugin_path));
  }
};
}  // namespace

bool ParseArgs(
    int argc, const char* const* argv, Args* out_args, ufg::Logger* logger) {
  ArgParser parser;
  const bool success = parser.Parse(argc, argv, out_args, logger);
  if (!success) {
    parser.PrintShortUsage();
  }
  return success;
}
