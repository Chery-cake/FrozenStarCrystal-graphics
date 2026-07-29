module;

#include <slang-com-ptr.h>
#include <slang.h>

module graphics.vulkan.shaders;

namespace graphics::vulkan::shaders {

namespace {

SlangStage vkStageBitToSlangStage(vk::ShaderStageFlagBits stage) {
  switch (stage) {
  case vk::ShaderStageFlagBits::eVertex:
    return SlangStage::SLANG_STAGE_VERTEX;
  case vk::ShaderStageFlagBits::eFragment:
    return SlangStage::SLANG_STAGE_FRAGMENT;
  case vk::ShaderStageFlagBits::eGeometry:
    return SlangStage::SLANG_STAGE_GEOMETRY;
  case vk::ShaderStageFlagBits::eCompute:
    return SlangStage::SLANG_STAGE_COMPUTE;
  case vk::ShaderStageFlagBits::eTessellationControl:
    return SlangStage::SLANG_STAGE_HULL;
  case vk::ShaderStageFlagBits::eTessellationEvaluation:
    return SlangStage::SLANG_STAGE_DOMAIN;
  default:
    return SlangStage::SLANG_STAGE_VERTEX;
  }
}

std::filesystem::path getExecutableDir() {
#if defined(__linux__) || defined(__APPLE__)
  return std::filesystem::canonical("/proc/self/exe").parent_path();
#elif defined(_WIN32)
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return std::filesystem::path(path).parent_path();
#else
  static_assert(false, "Unsupported platform");
#endif
}

slang::SessionDesc
setSessionDesc(std::vector<std::filesystem::path> &includePaths,
               const Slang::ComPtr<slang::IGlobalSession> &globalSession) {
  auto paths =
      includePaths |
      std::views::transform([](const std::string &s) { return s.c_str(); }) |
      std::ranges::to<std::vector<const char *>>();

  slang::TargetDesc targetDesc;
  targetDesc.format = SLANG_SPIRV;
  targetDesc.profile = globalSession->findProfile("spirv_1_5");
  targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

  slang::SessionDesc sessionDesc;
  sessionDesc.targets = &targetDesc;
  sessionDesc.targetCount = 1;
  sessionDesc.searchPaths = paths.data();
  sessionDesc.searchPathCount = static_cast<SlangInt>(includePaths.size());
  sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

  return sessionDesc;
}

} // namespace

Compiler::Compiler() {

  globalSession();

  auto folder = std::filesystem::path(getExecutableDir() / "assets/shaders");

  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  if (ec) {
    throw std::runtime_error("Couldn't create shaders folder: " +
                             folder.string() + " - " + ec.message());
  }

  includePaths_.push_back(folder.lexically_normal());
}

Slang::ComPtr<slang::IGlobalSession> &Compiler::globalSession() {
  static Slang::ComPtr<slang::IGlobalSession> session;
  static std::once_flag once;
  std::call_once(once,
                 []() { slang::createGlobalSession(session.writeRef()); });
  return session;
}

// TODO make it better for multi-thread
std::expected<std::string, Compiler::CompilerError>
Compiler::compile(const Shader &shader) {

  std::unique_lock lock(mtx_);

  Slang::ComPtr<slang::ISession> session;

  slang::SessionDesc sessionDesc =
      setSessionDesc(includePaths_, globalSession());

  globalSession()->createSession(sessionDesc, session.writeRef());

  if (session.get() == nullptr) {
    return std::unexpected<CompilerError>(
        {.code = CompilerError::Code::sessionNotCreated,
         .message = "Slang session wasn't able to be created \n"});
  }

  if (!std::filesystem::exists(shader.sourcePath)) {
    return std::unexpected<CompilerError>(
        {.code = CompilerError::Code::fileNotFound,
         .message =
             "Shader file doesn't exist on path: " + shader.sourcePath + '\n'});
  }
  std::string pathNormalized =
      std::filesystem::path(shader.sourcePath).lexically_normal();

  Slang::ComPtr<slang::IBlob> diagnosticsBlob;
  Slang::ComPtr<slang::IModule> module;
  module.attach(
      session->loadModule(pathNormalized.c_str(), diagnosticsBlob.writeRef()));

  if (module.get() == nullptr) {
    std::string message;
    if (diagnosticsBlob != nullptr) {
      message = static_cast<const char *>(diagnosticsBlob->getBufferPointer());
    } else {
      message = "Failed to load module: " + pathNormalized;
    }
    return std::unexpected<CompilerError>(
        {.code = CompilerError::Code::moduleFailedToLoad,
         .message = message + '\n'});
  }

  std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPoints;
  try {
    std::ranges::for_each(shader.entryPoints, [&entryPoints,
                                               &module](const auto &ep) {
      Slang::ComPtr<slang::IEntryPoint> entry;
      SlangResult findResult =
          module->findEntryPointByName(ep.entry.c_str(), entry.writeRef());

      if (SLANG_FAILED(findResult) || entry.get() == nullptr) {
        // Try with explicit stage if not marked in source
        Slang::ComPtr<slang::IBlob> epDiagnostics;
        findResult = module->findAndCheckEntryPoint(
            ep.entry.c_str(), vkStageBitToSlangStage(ep.type), entry.writeRef(),
            epDiagnostics.writeRef());

        if (SLANG_FAILED(findResult) || entry.get() == nullptr) {
          throw std::runtime_error("Entry point not found: " + ep.entry + '\n');
        }
      }
      entryPoints.push_back(std::move(entry));
    });
  } catch (const std::runtime_error &e) {
    return std::unexpected<CompilerError>(
        {.code = CompilerError::Code::entryNotFound, .message = e.what()});
  }

  std::vector<slang::IComponentType *> components;
  components.push_back(module);
  std::ranges::transform(entryPoints, std::back_inserter(components),
                         [](const auto &ep) { return ep; });

  Slang::ComPtr<slang::IComponentType> composedProgram;
  SlangResult composeResult = session->createCompositeComponentType(
      components.data(), static_cast<SlangInt>(components.size()),
      composedProgram.writeRef(), diagnosticsBlob.writeRef());

  if (SLANG_FAILED(composeResult)) {
    std::string message;
    if (diagnosticsBlob != nullptr) {
      message = static_cast<const char *>(diagnosticsBlob->getBufferPointer());
    } else {
      message = "Failed to compose shader program";
    }
    return std::unexpected<CompilerError>(
        {.code = CompilerError::Code::failedToComposeShader,
         .message = message + '\n'});
  }

  // Link the program
  Slang::ComPtr<slang::IComponentType> linkedProgram;
  SlangResult linkResult = composedProgram->link(linkedProgram.writeRef(),
                                                 diagnosticsBlob.writeRef());

  if (SLANG_FAILED(linkResult)) {
    std::string message;
    if (diagnosticsBlob.get() != nullptr) {
      message = static_cast<const char *>(diagnosticsBlob->getBufferPointer());
    } else {
      message = "Failed to link shader program";
    }
    return std::unexpected<CompilerError>(
        {.code = CompilerError::Code::failedToLinkShaders,
         .message = message + '\n'});
  }

  // Get compiled SPIR-V code
  Slang::ComPtr<slang::IBlob> codeBlob;

  SlangResult codeResult = linkedProgram->getTargetCode(
      0, codeBlob.writeRef(), diagnosticsBlob.writeRef());

  if (SLANG_FAILED(codeResult) || codeBlob.get() == nullptr) {
    std::string message;
    if (diagnosticsBlob != nullptr) {
      message = static_cast<const char *>(diagnosticsBlob->getBufferPointer());
    } else {
      message = "Failed to get SPIR-V code";
    }
    return std::unexpected<CompilerError>(
        {.code = CompilerError::Code::failedToGetSPIRV,
         .message = message + '\n'});
  }

  // Validate and copy SPIR-V
  size_t codeSize = codeBlob->getBufferSize();
  if (codeSize == 0 || codeSize % sizeof(uint32_t) != 0) {
    return std::unexpected<CompilerError>(
        {.code = CompilerError::Code::invalidCodeSize,
         .message = "Invalid SPIR-V code size \n"});
  }

  std::filesystem::path outDir =
      std::filesystem::path(shader.sourcePath).parent_path() / "compiled";

  std::error_code ec;
  std::filesystem::create_directories(outDir, ec);
  if (ec) {
    return std::unexpected(CompilerError{
        .code = CompilerError::Code::fileNotFound,
        .message = "Could not create output directory: " + outDir.string()});
  }

  // Output file: same stem, .spv extension
  std::filesystem::path outPath =
      outDir / std::filesystem::path(shader.sourcePath).filename();
  outPath.replace_extension(".spv");

  std::ofstream outFile(outPath, std::ios::binary);
  if (!outFile) {
    return std::unexpected(CompilerError{
        .code = CompilerError::Code::fileNotFound,
        .message = "Could not open output file: " + outPath.string()});
  }

  outFile.write(static_cast<const char *>(codeBlob->getBufferPointer()),
                static_cast<std::streamsize>(codeSize));
  outFile.close();

  return outPath.string();
}

std::string Compiler::getBinary(const Shader &shader) {
  std::unique_lock lock(mtx_);

  std::filesystem::path compDir =
      std::filesystem::path(shader.sourcePath).parent_path() / "compiled";

  if (std::filesystem::exists(compDir)) {
    std::filesystem::path file =
        compDir / std::filesystem::path(shader.sourcePath).filename();
    file.replace_extension(".spv");
    if (std::filesystem::exists(file)) {
      return file.string();
    }
  }

  auto comp = compile(shader);

  if (!comp.has_value()) { // TODO deal with errors
  }
  return comp.value();
}

bool Compiler::askRecompile(const Shader &shader) {
  std::unique_lock lock(mtx_);

  std::filesystem::path compDir =
      std::filesystem::path(shader.sourcePath).parent_path() / "compiled";
  std::filesystem::path file =
      compDir / std::filesystem::path(shader.sourcePath).filename();
  file.replace_extension(".spv");

  std::filesystem::remove(file);

  if (!std::filesystem::exists(file)) {
    auto comp = compile(shader);
    return comp.has_value();
  }
  return false;
}

void Compiler::clearPaths() {
  std::unique_lock lock(mtx_);

  includePaths_.clear();

  auto folder = std::filesystem::path(getExecutableDir() / "assets/shaders");

  if (!std::filesystem::exists(folder)) {
    std::error_code ec;
    std::filesystem::create_directory(folder, ec);
    if (ec) {
      throw std::runtime_error("Couldn't create shaders folder - " +
                               ec.message() + '\n');
    }
  }

  includePaths_.push_back(folder.lexically_normal());
}

} // namespace graphics::vulkan::shaders
