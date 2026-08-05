#include "render/GltfSkinnedModel.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
  }
  return 0;
}

constexpr std::string_view workerModelSha256 =
  "b72bb9287f761550b059f4dffcf721c78ae19d814c0de74633e4cbe18c455c60";

const lg::GltfMaterialBinding* bindingFor(
  const lg::GltfMaterialMetadata& metadata,
  int materialIndex
) {
  const auto found = std::find_if(
    metadata.bindings.begin(),
    metadata.bindings.end(),
    [materialIndex](const lg::GltfMaterialBinding& binding) {
      return binding.materialIndex == materialIndex;
    }
  );
  return found == metadata.bindings.end() ? nullptr : &*found;
}

const lg::GltfSkinnedModel::Primitive* primitiveFor(
  const lg::GltfSkinnedModel& model,
  int materialIndex
) {
  const auto found = std::find_if(
    model.primitives().begin(),
    model.primitives().end(),
    [materialIndex](const lg::GltfSkinnedModel::Primitive& primitive) {
      return primitive.materialIndex == materialIndex;
    }
  );
  return found == model.primitives().end() ? nullptr : &*found;
}

bool hasDuelistFlatTeamTint(const lg::GltfSkinnedModel& model) {
  const lg::GltfSkinnedModel::Primitive* primary = primitiveFor(model, 3);
  const lg::GltfSkinnedModel::Primitive* accent = primitiveFor(model, 4);
  const auto isTinted = [](const lg::GltfSkinnedModel::Primitive* primitive) {
    return primitive != nullptr && std::all_of(
      primitive->vertices.begin(), primitive->vertices.end(),
      [](const lg::GltfSkinnedModel::GpuVertex& vertex) {
        return vertex.tintWeight == 255U && vertex.albedoTextureMode == 0U;
      }
    );
  };
  return isTinted(primary) && isTinted(accent);
}

std::string manifestWithTextures(
  const std::string& albedoPath,
  const std::string& maskPath,
  std::string_view modelSha256 = workerModelSha256
) {
  return R"({
  "schema_version": 1,
  "model": "worker.glb",
  "model_sha256": ")" + std::string(modelSha256) + R"(",
  "opaque": true,
  "uv_mode": "texcoord0",
  "albedo_mode": "replace",
  "mip_policy": "runtime_generate",
  "textures": {
    "albedo": {"path": ")" + albedoPath + R"(", "color_space": "srgb", "width": 512, "height": 512},
    "packed_mask": {"path": ")" + maskPath + R"(", "color_space": "linear", "width": 512, "height": 512}
  },
  "packed_mask_contract": {
    "r": "team_tint_weight",
    "g": "perceptual_roughness",
    "b": "metallic_weight",
    "a": "emissive_weight_reserved_zero"
  },
  "materials": []
})";
}

bool writeText(const std::filesystem::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::binary);
  output << text;
  return static_cast<bool>(output);
}

} // namespace

int main() {
  int failures = 0;
  const lg::GltfSkinnedModel& worker = lg::workerPlayerModel();
  const lg::GltfMaterialMetadata& workerMetadata = worker.materialMetadata();
  const lg::GltfMaterialBinding* yellow = bindingFor(workerMetadata, 1);
  const lg::GltfMaterialBinding* vest = bindingFor(workerMetadata, 2);
  const lg::GltfSkinnedModel::Primitive* yellowPrimitive = primitiveFor(worker, 1);
  const lg::GltfSkinnedModel::Primitive* vestPrimitive = primitiveFor(worker, 2);
  failures += expect(
    worker.loaded() &&
      worker.materialNames().size() == 13U &&
      workerMetadata.valid() &&
      workerMetadata.hasAuthoredTextures() &&
      workerMetadata.authoredTextureFilesAvailable() &&
      workerMetadata.materialCells &&
      workerMetadata.albedo.colorSpace == lg::GltfTextureColorSpace::Srgb &&
      workerMetadata.packedMask.colorSpace == lg::GltfTextureColorSpace::Linear &&
      workerMetadata.albedo.width == 512U &&
      workerMetadata.albedo.height == 512U &&
      workerMetadata.packedMask.width == 512U &&
      workerMetadata.packedMask.height == 512U &&
      std::all_of(
        worker.primitives().begin(), worker.primitives().end(),
        [](const lg::GltfSkinnedModel::Primitive& primitive) {
          return primitive.opaque && primitive.color.alpha == 255U;
        }
      ),
    "Worker should discover a valid local sRGB albedo and linear packed mask"
  );
  failures += expect(
    yellow != nullptr && vest != nullptr &&
      yellow->expectedName == "Worker_Yellow" &&
      vest->expectedName == "Worker_Vest" &&
      std::abs(yellow->atlasU - 0.375F) < 0.0001F &&
      std::abs(vest->atlasU - 0.625F) < 0.0001F &&
      yellowPrimitive != nullptr && vestPrimitive != nullptr &&
      !yellowPrimitive->vertices.empty() && !vestPrimitive->vertices.empty() &&
      std::all_of(
        yellowPrimitive->vertices.begin(), yellowPrimitive->vertices.end(),
        [](const lg::GltfSkinnedModel::GpuVertex& vertex) {
          return std::abs(vertex.u - 0.375F) < 0.0001F &&
            std::abs(vertex.v - 0.125F) < 0.0001F &&
            vertex.tintWeight == 0U && vertex.albedoTextureMode == 255U;
        }
      ) &&
      std::all_of(
        vestPrimitive->vertices.begin(), vestPrimitive->vertices.end(),
        [](const lg::GltfSkinnedModel::GpuVertex& vertex) {
          return std::abs(vertex.u - 0.625F) < 0.0001F &&
            std::abs(vertex.v - 0.125F) < 0.0001F &&
            vertex.tintWeight == 0U && vertex.albedoTextureMode == 255U;
        }
      ),
    "Worker material cells should drive the authored texture path without name-based tinting"
  );
  const lg::GltfMaterialQualityPlan workerQuality0 =
    lg::gltfMaterialQualityPlan(0, workerMetadata.authoredTextureFilesAvailable());
  const lg::GltfMaterialQualityPlan workerQuality1 =
    lg::gltfMaterialQualityPlan(1, workerMetadata.authoredTextureFilesAvailable());
  const lg::GltfMaterialQualityPlan workerQuality2 =
    lg::gltfMaterialQualityPlan(2, workerMetadata.authoredTextureFilesAvailable());
  const lg::GltfMaterialResourcePlan workerResources =
    lg::gltfMaterialResourcePlan(workerMetadata);
  failures += expect(
    workerQuality0.flatFallback &&
      !workerQuality0.samplesAlbedo && !workerQuality0.samplesPackedMask &&
      workerQuality1.samplesAlbedo && workerQuality1.samplesPackedMask &&
      workerQuality1.appliesAuthoredTeamTint && workerQuality1.roughnessHighlights &&
      !workerQuality1.metallicResponse && workerQuality2.samplesAlbedo &&
      workerQuality2.metallicResponse &&
      !workerQuality2.environmentResponse &&
      lg::gltfTextureMipLevels(512U, 512U) == 10U &&
      lg::gltfTextureMipLevels(512U, 256U) == 10U &&
      lg::gltfRgbaMipBytes(512U, 256U) == 699052U &&
      lg::gltfTextureMipLevels(512U, 1U) == 10U &&
      lg::gltfRgbaMipBytes(512U, 1U) == 4092U &&
      workerResources.sharedTextureAllocations == 2U &&
      workerResources.textureBytes == 2796200U &&
      workerResources.perInstanceTextureAllocations == 0U &&
      workerResources.perFrameTextureUploadBytes == 0U,
    "quality plans should retain a texture-free quality zero and shared Worker resources"
  );

  const lg::GltfSkinnedModel& duelist = lg::duelistMaleModel();
  const lg::GltfMaterialMetadata& duelistMetadata = duelist.materialMetadata();
  failures += expect(
    duelist.loaded() && duelistMetadata.valid() &&
      !duelistMetadata.hasAuthoredTextures() &&
      hasDuelistFlatTeamTint(duelist) &&
      lg::gltfMaterialQualityPlan(2, false).flatFallback,
    "texture-less Duelist should retain flat-material team tint"
  );

  const std::filesystem::path fixtureRoot =
    std::filesystem::current_path() / "gltf-material-manifest-fixture";
  std::error_code error;
  std::filesystem::remove_all(fixtureRoot, error);
  std::filesystem::create_directories(fixtureRoot, error);
  const std::filesystem::path fixtureModel = fixtureRoot / "worker.glb";
  const std::filesystem::path workerSource =
    std::filesystem::absolute(std::filesystem::path(worker.sourcePath()));
  std::filesystem::copy_file(
    workerSource,
    fixtureModel,
    std::filesystem::copy_options::overwrite_existing,
    error
  );
  failures += expect(!error, "material manifest test fixture should copy the Worker GLB");
  if (!error) {
    const std::filesystem::path fixtureManifest = fixtureRoot / "material-manifest.json";
    lg::GltfSkinnedModel noMetadata;
    failures += expect(
      noMetadata.load(fixtureModel.string()) &&
        noMetadata.materialMetadata().status == lg::GltfMaterialManifestStatus::NotFound &&
        !noMetadata.materialMetadata().hasAuthoredTextures() &&
        std::all_of(
          noMetadata.primitives().begin(), noMetadata.primitives().end(),
          [](const lg::GltfSkinnedModel::Primitive& primitive) {
            return std::all_of(
              primitive.vertices.begin(), primitive.vertices.end(),
              [](const lg::GltfSkinnedModel::GpuVertex& vertex) {
                return vertex.albedoTextureMode == 0U;
              }
            );
          }
        ),
      "a model without material metadata should retain the flat path"
    );
    failures += expect(
      writeText(
        fixtureManifest,
        R"({"schema_version":1,"model":"worker.glb","materials":[]})"
      ),
      "material manifest test fixture should write a missing-hash manifest"
    );
    lg::GltfSkinnedModel missingHash;
    failures += expect(
      missingHash.load(fixtureModel.string()) &&
        missingHash.materialMetadata().status == lg::GltfMaterialManifestStatus::Invalid &&
        !missingHash.materialMetadata().hasAuthoredTextures(),
      "a missing model_sha256 should reject material metadata safely"
    );
    failures += expect(
      writeText(
        fixtureManifest,
        manifestWithTextures(
          "missing-albedo.png",
          "missing-mask.png",
          "0000000000000000000000000000000000000000000000000000000000000000"
        )
      ),
      "material manifest test fixture should write a wrong-hash manifest"
    );
    lg::GltfSkinnedModel wrongHash;
    failures += expect(
      wrongHash.load(fixtureModel.string()) &&
        wrongHash.materialMetadata().status == lg::GltfMaterialManifestStatus::Invalid &&
        !wrongHash.materialMetadata().hasAuthoredTextures(),
      "a model_sha256 mismatch should reject material metadata safely"
    );
    failures += expect(
      writeText(fixtureManifest, manifestWithTextures("missing-albedo.png", "missing-mask.png")),
      "material manifest test fixture should write a missing-texture manifest"
    );
    lg::GltfSkinnedModel missingTextures;
    failures += expect(
      missingTextures.load(fixtureModel.string()) &&
        missingTextures.materialMetadata().valid() &&
        !missingTextures.materialMetadata().authoredTextureFilesAvailable() &&
        lg::gltfMaterialQualityPlan(
          1,
          missingTextures.materialMetadata().authoredTextureFilesAvailable()
        ).flatFallback,
      "missing Worker material textures should retain a safe flat fallback"
    );
    failures += expect(
      writeText(
        fixtureManifest,
        R"({
  "schema_version": 1,
  "model": "worker.glb",
  "model_sha256": "b72bb9287f761550b059f4dffcf721c78ae19d814c0de74633e4cbe18c455c60",
  "uv_mode": "material_cell",
  "atlas": {"columns": 4, "rows": 4},
  "albedo_mode": "replace",
  "mip_policy": "runtime_generate",
  "textures": {
    "albedo": {"path": "missing-albedo.png", "color_space": "srgb", "width": 512, "height": 512},
    "packed_mask": {"path": "missing-mask.png", "color_space": "linear", "width": 512, "height": 512}
  },
  "packed_mask_contract": {
    "r": "team_tint_weight",
    "g": "perceptual_roughness",
    "b": "metallic_weight",
    "a": "emissive_weight_reserved_zero"
  },
  "materials": [
    {"index": 1, "name": "Worker_Yellow", "cell": [1, 0]}
  ]
})"
      ),
      "material manifest test fixture should write a partial material-cell manifest"
    );
    lg::GltfSkinnedModel partialMaterialCells;
    const lg::GltfSkinnedModel::Primitive* partialYellow = nullptr;
    const lg::GltfSkinnedModel::Primitive* partialVest = nullptr;
    failures += expect(
      partialMaterialCells.load(fixtureModel.string()) &&
        partialMaterialCells.materialMetadata().valid() &&
        partialMaterialCells.materialMetadata().materialCells &&
        (partialYellow = primitiveFor(partialMaterialCells, 1)) != nullptr &&
        (partialVest = primitiveFor(partialMaterialCells, 2)) != nullptr &&
        std::all_of(
          partialYellow->vertices.begin(), partialYellow->vertices.end(),
          [](const lg::GltfSkinnedModel::GpuVertex& vertex) {
            return std::abs(vertex.u - 0.375F) < 0.0001F &&
              std::abs(vertex.v - 0.125F) < 0.0001F &&
              vertex.albedoTextureMode == 255U;
          }
        ) &&
        std::all_of(
          partialVest->vertices.begin(), partialVest->vertices.end(),
          [](const lg::GltfSkinnedModel::GpuVertex& vertex) {
            return vertex.albedoTextureMode == 0U;
          }
        ),
      "unbound material-cell materials should retain the flat path"
    );
    failures += expect(
      writeText(fixtureManifest, manifestWithTextures("../escape.png", "missing-mask.png")),
      "material manifest test fixture should write an invalid-path manifest"
    );
    lg::GltfSkinnedModel escapedPath;
    failures += expect(
      escapedPath.load(fixtureModel.string()) &&
        escapedPath.materialMetadata().status == lg::GltfMaterialManifestStatus::Invalid &&
        !escapedPath.materialMetadata().hasAuthoredTextures(),
      "material asset paths must not escape a model directory"
    );
    std::string invalidDimensions = manifestWithTextures(
      "missing-albedo.png", "missing-mask.png"
    );
    const std::string expectedDimension = "\"width\": 512";
    const std::size_t firstDimension = invalidDimensions.find(expectedDimension);
    if (firstDimension != std::string::npos) {
      invalidDimensions.replace(firstDimension, expectedDimension.size(), "\"width\": 513");
    }
    failures += expect(
      writeText(fixtureManifest, invalidDimensions),
      "material manifest test fixture should write an invalid-dimension manifest"
    );
    lg::GltfSkinnedModel invalidDimensionsModel;
    failures += expect(
      invalidDimensionsModel.load(fixtureModel.string()) &&
        invalidDimensionsModel.materialMetadata().status ==
          lg::GltfMaterialManifestStatus::Invalid,
      "non-power-of-two material dimensions should reject metadata safely"
    );
    failures += expect(
      writeText(
        fixtureManifest,
        R"({"schema_version":1,"model":"worker.glb","model_sha256":"b72bb9287f761550b059f4dffcf721c78ae19d814c0de74633e4cbe18c455c60","materials":[{"index":99,"name":"bad"}]})"
      ),
      "material manifest test fixture should write an invalid-index manifest"
    );
    lg::GltfSkinnedModel invalidIndex;
    failures += expect(
      invalidIndex.load(fixtureModel.string()) &&
        invalidIndex.materialMetadata().status == lg::GltfMaterialManifestStatus::Invalid &&
        invalidIndex.primitives().size() == worker.primitives().size(),
      "invalid material indices should reject metadata without hiding the model"
    );
  }
  std::filesystem::remove_all(fixtureRoot, error);

  const std::filesystem::path duelistFixtureRoot =
    std::filesystem::current_path() / "gltf-duelist-material-manifest-fixture";
  error.clear();
  std::filesystem::remove_all(duelistFixtureRoot, error);
  std::filesystem::create_directories(duelistFixtureRoot, error);
  const std::filesystem::path duelistFixtureModel = duelistFixtureRoot / "duelist.glb";
  const std::filesystem::path duelistSource =
    std::filesystem::absolute(std::filesystem::path(duelist.sourcePath()));
  std::filesystem::copy_file(
    duelistSource,
    duelistFixtureModel,
    std::filesystem::copy_options::overwrite_existing,
    error
  );
  failures += expect(!error, "material manifest test fixture should copy the Duelist GLB");
  if (!error) {
    lg::GltfSkinnedModel noDuelistMetadata;
    failures += expect(
      noDuelistMetadata.load(duelistFixtureModel.string()) &&
        noDuelistMetadata.materialMetadata().status == lg::GltfMaterialManifestStatus::NotFound &&
        hasDuelistFlatTeamTint(noDuelistMetadata),
      "Duelist should retain flat team tint without a local manifest"
    );
    failures += expect(
      writeText(
        duelistFixtureRoot / "material-manifest.json",
        R"({"schema_version":1,"model":"duelist.glb","model_sha256":"not-a-valid-sha256","materials":[]})"
      ),
      "material manifest test fixture should write an invalid Duelist manifest"
    );
    lg::GltfSkinnedModel invalidDuelistMetadata;
    failures += expect(
      invalidDuelistMetadata.load(duelistFixtureModel.string()) &&
        invalidDuelistMetadata.materialMetadata().status == lg::GltfMaterialManifestStatus::Invalid &&
        hasDuelistFlatTeamTint(invalidDuelistMetadata),
      "Duelist should retain flat team tint when a local manifest is invalid"
    );
  }
  std::filesystem::remove_all(duelistFixtureRoot, error);
  return failures == 0 ? 0 : 1;
}
