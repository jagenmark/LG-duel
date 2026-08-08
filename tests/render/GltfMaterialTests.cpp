#include "render/GltfSkinnedModel.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

int expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
  }
  return 0;
}

constexpr std::string_view workerModelSha256 =
  "368445c36b4c7da7bdec8f677cfecb6c5a34d575caac59ff4b721a5cd85f22db";
constexpr std::string_view workerMissingFirstMaterialSha256 =
  "cabb29210c4b3024701bdbae22e6107e2a4726342f00b91e6aa52ca5c438e3f7";

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

std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {
    std::istreambuf_iterator<char>(input),
    std::istreambuf_iterator<char>()
  };
}

bool replaceOnce(
  std::string& text,
  std::string_view needle,
  std::string_view replacement
) {
  const std::size_t position = text.find(needle);
  if (position == std::string::npos) {
    return false;
  }
  text.replace(position, needle.size(), replacement);
  return true;
}

bool hasFlatAlbedoVertices(const lg::GltfSkinnedModel& model) {
  return std::all_of(
    model.primitives().begin(), model.primitives().end(),
    [](const lg::GltfSkinnedModel::Primitive& primitive) {
      return std::all_of(
        primitive.vertices.begin(), primitive.vertices.end(),
        [](const lg::GltfSkinnedModel::GpuVertex& vertex) {
          return vertex.albedoTextureMode == 0U;
        }
      );
    }
  );
}

bool writeWorkerGlbWithoutFirstMaterial(
  const std::filesystem::path& source,
  const std::filesystem::path& destination
) {
  std::ifstream input(source, std::ios::binary);
  if (!input) {
    return false;
  }
  std::vector<std::uint8_t> bytes;
  char byte = '\0';
  while (input.get(byte)) {
    bytes.push_back(static_cast<std::uint8_t>(
      static_cast<unsigned char>(byte)
    ));
  }
  if (
    bytes.size() < 20U ||
    bytes[16U] != 0x4AU || bytes[17U] != 0x53U ||
    bytes[18U] != 0x4FU || bytes[19U] != 0x4EU
  ) {
    return false;
  }
  const std::size_t jsonLength = static_cast<std::size_t>(bytes[12U]) |
    (static_cast<std::size_t>(bytes[13U]) << 8U) |
    (static_cast<std::size_t>(bytes[14U]) << 16U) |
    (static_cast<std::size_t>(bytes[15U]) << 24U);
  if (jsonLength > bytes.size() - 20U) {
    return false;
  }
  constexpr std::string_view firstMaterial = "\"material\":0";
  const auto jsonBegin = bytes.begin() + 20;
  const auto jsonEnd = jsonBegin + static_cast<std::ptrdiff_t>(jsonLength);
  const auto found = std::search(
    jsonBegin,
    jsonEnd,
    firstMaterial.begin(),
    firstMaterial.end()
  );
  if (found == jsonEnd || std::next(found) == jsonEnd) {
    return false;
  }
  *std::next(found) = static_cast<std::uint8_t>('x');
  std::ofstream output(destination, std::ios::binary);
  output.write(
    reinterpret_cast<const char*>(bytes.data()),
    static_cast<std::streamsize>(bytes.size())
  );
  return static_cast<bool>(output);
}

} // namespace

int main() {
  int failures = 0;
  const lg::GltfSkinnedModel& worker = lg::workerPlayerModel();
  const lg::GltfMaterialMetadata& workerMetadata = worker.materialMetadata();
  const lg::GltfMaterialBinding* yellow = bindingFor(workerMetadata, 1);
  const lg::GltfMaterialBinding* vest = bindingFor(workerMetadata, 2);
  const lg::GltfMaterialBinding* yellowDuplicate = bindingFor(workerMetadata, 8);
  const lg::GltfSkinnedModel::Primitive* yellowPrimitive = primitiveFor(worker, 1);
  const lg::GltfSkinnedModel::Primitive* vestPrimitive = primitiveFor(worker, 2);
  const lg::GltfSkinnedModel::Primitive* yellowDuplicatePrimitive = primitiveFor(worker, 8);
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
  // Manifest weights are floats; the model loader truncates 0.92 * 255 to 234.
  failures += expect(
    yellow != nullptr && vest != nullptr && yellowDuplicate != nullptr &&
      yellow->expectedName == "Worker_Yellow" &&
      vest->expectedName == "Worker_Vest" &&
      yellowDuplicate->expectedName == "Worker_Yellow.001" &&
      yellow->flatTintWeight == 234U &&
      vest->flatTintWeight == 255U &&
      yellowDuplicate->flatTintWeight == 234U &&
      std::all_of(
        workerMetadata.bindings.begin(), workerMetadata.bindings.end(),
        [](const lg::GltfMaterialBinding& binding) {
          return binding.materialIndex == 1 || binding.materialIndex == 2 ||
            binding.materialIndex == 8 || binding.flatTintWeight == 0U;
        }
      ) &&
      std::abs(yellow->atlasU - 0.375F) < 0.0001F &&
      std::abs(vest->atlasU - 0.625F) < 0.0001F &&
      yellowPrimitive != nullptr && vestPrimitive != nullptr &&
      yellowDuplicatePrimitive != nullptr && !yellowPrimitive->vertices.empty() &&
      !vestPrimitive->vertices.empty() && !yellowDuplicatePrimitive->vertices.empty() &&
      std::all_of(
        yellowPrimitive->vertices.begin(), yellowPrimitive->vertices.end(),
        [](const lg::GltfSkinnedModel::GpuVertex& vertex) {
          return std::abs(vertex.u - 0.375F) < 0.0001F &&
            std::abs(vertex.v - 0.125F) < 0.0001F &&
            vertex.tintWeight == 234U && vertex.albedoTextureMode == 255U;
        }
      ) &&
      std::all_of(
        vestPrimitive->vertices.begin(), vestPrimitive->vertices.end(),
        [](const lg::GltfSkinnedModel::GpuVertex& vertex) {
          return std::abs(vertex.u - 0.625F) < 0.0001F &&
            std::abs(vertex.v - 0.125F) < 0.0001F &&
            vertex.tintWeight == 255U && vertex.albedoTextureMode == 255U;
        }
      ) &&
      std::all_of(
        yellowDuplicatePrimitive->vertices.begin(),
        yellowDuplicatePrimitive->vertices.end(),
        [](const lg::GltfSkinnedModel::GpuVertex& vertex) {
          return std::abs(vertex.u - 0.375F) < 0.0001F &&
            std::abs(vertex.v - 0.125F) < 0.0001F &&
            vertex.tintWeight == 234U && vertex.albedoTextureMode == 255U;
        }
      ),
    "Worker metadata should set only the approved flat tint bytes without renderer name rules"
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
      !workerQuality0.appliesAuthoredTeamTint && !workerQuality0.roughnessHighlights &&
      !workerQuality0.metallicResponse && !workerQuality0.environmentResponse &&
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
  "model_sha256": "368445c36b4c7da7bdec8f677cfecb6c5a34d575caac59ff4b721a5cd85f22db",
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
    failures += expect(
      partialMaterialCells.load(fixtureModel.string()) &&
        partialMaterialCells.materialMetadata().status ==
          lg::GltfMaterialManifestStatus::Invalid &&
        !partialMaterialCells.materialMetadata().hasAuthoredTextures() &&
        std::all_of(
          partialMaterialCells.primitives().begin(),
          partialMaterialCells.primitives().end(),
          [](const lg::GltfSkinnedModel::Primitive& primitive) {
            return std::all_of(
              primitive.vertices.begin(), primitive.vertices.end(),
              [](const lg::GltfSkinnedModel::GpuVertex& vertex) {
                return vertex.albedoTextureMode == 0U;
              }
            );
          }
        ),
      "partial material-cell manifests should reject authored texture sampling"
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
        R"({"schema_version":1,"model":"worker.glb","model_sha256":"368445c36b4c7da7bdec8f677cfecb6c5a34d575caac59ff4b721a5cd85f22db","materials":[{"index":99,"name":"bad"}]})"
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

    std::string validWorkerManifest = readText(
      std::filesystem::absolute(std::filesystem::path(workerMetadata.manifestPath))
    );
    const bool retargetedFixtureManifest = replaceOnce(
      validWorkerManifest,
      R"("model": "quaternius_worker.glb")",
      R"("model": "worker.glb")"
    );
    const auto rejectsToFlatFallback = [&fixtureManifest, &fixtureModel](
                                        const std::string& manifest
                                      ) {
      lg::GltfSkinnedModel model;
      return writeText(fixtureManifest, manifest) &&
        model.load(fixtureModel.string()) &&
        model.materialMetadata().status == lg::GltfMaterialManifestStatus::Invalid &&
        !model.materialMetadata().hasAuthoredTextures() &&
        model.materialMetadata().albedo.path.empty() &&
        model.materialMetadata().packedMask.path.empty() &&
        hasFlatAlbedoVertices(model);
    };
    failures += expect(
      retargetedFixtureManifest &&
        rejectsToFlatFallback(validWorkerManifest + "\ntrailing"),
      "trailing text after a material manifest should reject metadata and use flat vertices"
    );

    std::string unsupportedStringEscape = validWorkerManifest;
    const bool changedUnsupportedStringEscape = replaceOnce(
      unsupportedStringEscape,
      R"("albedo_mode": "replace")",
      R"("albedo_mode": "re\place")"
    );
    failures += expect(
      changedUnsupportedStringEscape &&
        rejectsToFlatFallback(unsupportedStringEscape),
      "unsupported material manifest string escapes should reject metadata and use flat vertices"
    );

    std::string rawStringControl = validWorkerManifest;
    const std::string rawControlAlbedoMode =
      std::string("\"albedo_mode\": \"re") +
      static_cast<char>(0x01) + "place\"";
    const bool changedRawStringControl = replaceOnce(
      rawStringControl,
      R"("albedo_mode": "replace")",
      rawControlAlbedoMode
    );
    failures += expect(
      changedRawStringControl && rejectsToFlatFallback(rawStringControl),
      "raw control characters in material manifest strings should reject metadata and use flat vertices"
    );

    std::string unterminatedString = validWorkerManifest;
    const std::size_t finalStringQuote = unterminatedString.find_last_of('"');
    const bool removedFinalStringQuote =
      finalStringQuote != std::string::npos;
    if (removedFinalStringQuote) {
      unterminatedString.erase(finalStringQuote, 1U);
    }
    failures += expect(
      removedFinalStringQuote && rejectsToFlatFallback(unterminatedString),
      "unterminated material manifest strings should reject metadata and use flat vertices"
    );

    std::string unicodeStringEscape = validWorkerManifest;
    const bool changedUnicodeStringEscape = replaceOnce(
      unicodeStringEscape,
      R"("albedo_mode": "replace")",
      R"("albedo_mode": "\u0072eplace")"
    );
    failures += expect(
      changedUnicodeStringEscape && rejectsToFlatFallback(unicodeStringEscape),
      "unicode material manifest string escapes should reject metadata and use flat vertices"
    );

    std::string finalTrailingComma = validWorkerManifest;
    const std::size_t finalClosingObject = finalTrailingComma.find_last_of('}');
    const bool addedFinalTrailingComma =
      finalClosingObject != std::string::npos;
    if (addedFinalTrailingComma) {
      finalTrailingComma.insert(finalClosingObject, 1U, ',');
    }
    failures += expect(
      addedFinalTrailingComma && rejectsToFlatFallback(finalTrailingComma),
      "a final material manifest trailing comma should reject metadata and use flat vertices"
    );

    std::string finalCommaWithoutClose = validWorkerManifest;
    const std::size_t finalObjectWithoutClose = finalCommaWithoutClose.find_last_of('}');
    const bool removedFinalClose =
      finalObjectWithoutClose != std::string::npos;
    if (removedFinalClose) {
      finalCommaWithoutClose.replace(finalObjectWithoutClose, 1U, ",");
    }
    failures += expect(
      removedFinalClose && rejectsToFlatFallback(finalCommaWithoutClose),
      "a final material manifest comma without a closing object should reject metadata and use flat vertices"
    );

    std::string incompleteSchemaExponent = validWorkerManifest;
    const bool changedIncompleteSchemaExponent = replaceOnce(
      incompleteSchemaExponent,
      R"("schema_version": 1)",
      R"("schema_version": 1e)"
    );
    failures += expect(
      changedIncompleteSchemaExponent &&
        rejectsToFlatFallback(incompleteSchemaExponent),
      "an incomplete material manifest schema exponent should reject metadata and use flat vertices"
    );

    std::string fractionalSchema = validWorkerManifest;
    const bool changedFractionalSchema = replaceOnce(
      fractionalSchema, R"("schema_version": 1)", R"("schema_version": 1.5)"
    );
    failures += expect(
      changedFractionalSchema && rejectsToFlatFallback(fractionalSchema),
      "fractional material manifest schema versions should reject metadata and use flat vertices"
    );

    std::string fractionalIndex = validWorkerManifest;
    const bool changedFractionalIndex = replaceOnce(
      fractionalIndex,
      R"({"index": 1, "name": "Worker_Yellow")",
      R"({"index": 1.5, "name": "Worker_Yellow")"
    );
    failures += expect(
      changedFractionalIndex && rejectsToFlatFallback(fractionalIndex),
      "fractional material indices should reject metadata and use flat vertices"
    );

    std::string fractionalCell = validWorkerManifest;
    const bool changedFractionalCell = replaceOnce(
      fractionalCell,
      R"({"index": 0, "name": "Skin", "cell": [0, 0])",
      R"({"index": 0, "name": "Skin", "cell": [-0.5, 0])"
    );
    failures += expect(
      changedFractionalCell && rejectsToFlatFallback(fractionalCell),
      "fractional material cells should reject metadata and use flat vertices"
    );

    std::string outOfRangeIndex = validWorkerManifest;
    const bool changedOutOfRangeIndex = replaceOnce(
      outOfRangeIndex,
      R"({"index": 1, "name": "Worker_Yellow")",
      R"({"index": 2147483648, "name": "Worker_Yellow")"
    );
    failures += expect(
      changedOutOfRangeIndex && rejectsToFlatFallback(outOfRangeIndex),
      "out-of-range material indices should reject metadata and use flat vertices"
    );
  }
  std::filesystem::remove_all(fixtureRoot, error);

  const std::filesystem::path missingMaterialFixtureRoot =
    std::filesystem::current_path() / "gltf-missing-material-fixture";
  error.clear();
  std::filesystem::remove_all(missingMaterialFixtureRoot, error);
  std::filesystem::create_directories(missingMaterialFixtureRoot, error);
  const std::filesystem::path missingMaterialFixtureModel =
    missingMaterialFixtureRoot / "quaternius_worker.glb";
  const std::filesystem::path missingMaterialFixtureManifest =
    missingMaterialFixtureRoot / "material-manifest.json";
  std::string missingMaterialManifest = readText(
    std::filesystem::absolute(std::filesystem::path(workerMetadata.manifestPath))
  );
  const std::size_t workerHashPosition = missingMaterialManifest.find(
    workerModelSha256
  );
  const bool hasWorkerHash = workerHashPosition != std::string::npos;
  if (hasWorkerHash) {
    missingMaterialManifest.replace(
      workerHashPosition,
      workerModelSha256.size(),
      workerMissingFirstMaterialSha256
    );
  }
  const bool wroteMissingMaterialFixture =
    !error && hasWorkerHash &&
    writeWorkerGlbWithoutFirstMaterial(workerSource, missingMaterialFixtureModel) &&
    writeText(missingMaterialFixtureManifest, missingMaterialManifest);
  failures += expect(
    wroteMissingMaterialFixture,
    "material manifest test fixture should remove a Worker primitive material"
  );
  if (wroteMissingMaterialFixture) {
    lg::GltfSkinnedModel missingPrimitiveMaterial;
    failures += expect(
      missingPrimitiveMaterial.load(missingMaterialFixtureModel.string()) &&
        missingPrimitiveMaterial.materialMetadata().status ==
          lg::GltfMaterialManifestStatus::Invalid &&
        missingPrimitiveMaterial.materialMetadata().diagnostic ==
          "material-cell manifest requires every renderable primitive to bind a material" &&
        !missingPrimitiveMaterial.materialMetadata().hasAuthoredTextures() &&
        primitiveFor(missingPrimitiveMaterial, -1) != nullptr &&
        std::all_of(
          missingPrimitiveMaterial.primitives().begin(),
          missingPrimitiveMaterial.primitives().end(),
          [](const lg::GltfSkinnedModel::Primitive& primitive) {
            return std::all_of(
              primitive.vertices.begin(), primitive.vertices.end(),
              [](const lg::GltfSkinnedModel::GpuVertex& vertex) {
                return vertex.albedoTextureMode == 0U;
              }
            );
          }
        ),
      "material-cell metadata should reject unbound Worker primitives and use flat vertices"
    );
  }
  std::filesystem::remove_all(missingMaterialFixtureRoot, error);

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
