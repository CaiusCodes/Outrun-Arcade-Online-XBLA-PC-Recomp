#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xex_module.h>
#include <filesystem>
#include <fstream>
#include <iostream>

// Loads the user's imported image without launching any guest code. Dumps stay
// in the ignored out/ directory and must never be added to a distributable.
int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: menu_image_probe <assets directory> <new output directory>\n";
    return 1;
  }
  const auto assets = std::filesystem::absolute(argv[1]);
  const auto output = std::filesystem::absolute(argv[2]);
  if (!std::filesystem::is_regular_file(assets / "default.xex") ||
      !std::filesystem::create_directory(output)) {
    std::cerr << "Assets missing or output already exists; refusing overwrite.\n";
    return 1;
  }
  rex::InitLogging((output / "load.log").string().c_str());
  rex::Runtime runtime(assets, output / "userdata", {}, output / "cache");
  rex::RuntimeConfig config;
  config.tool_mode = true;
  if (runtime.Setup(std::move(config)) != 0) return 2;
  // Use the binary loader directly; no title registration or guest execution.
  std::ifstream input(assets / "default.xex", std::ios::binary);
  std::vector<char> xex((std::istreambuf_iterator<char>(input)), {});
  rex::runtime::XexModule module(runtime.function_dispatcher(), runtime.kernel_state());
  if (!module.Load("default.xex", "game:\\default.xex", xex.data(), xex.size()) ||
      !module.LoadContinue()) {
    std::cerr << "Image load failed; see load.log.\n";
    return 2;
  }
  std::ofstream index(output / "sections.tsv");
  index << "name\taddress\tsize\texecutable\tfile\n";
  unsigned id = 0;
  for (const auto& section : module.binary_sections()) {
    if (!section.host_data || !section.virtual_size) continue;
    const uint64_t image_end = uint64_t(module.base_address()) + module.image_size();
    if (section.virtual_address < module.base_address() ||
        uint64_t(section.virtual_address) + section.virtual_size > image_end) {
      std::cout << "Skip " << section.name << ": not fully mapped in the XEX image\n";
      continue;
    }
    const auto filename = "section-" + std::to_string(id++) + ".bin";
    std::ofstream bytes(output / filename, std::ios::binary);
    bytes.write(reinterpret_cast<const char*>(section.host_data), section.virtual_size);
    if (!bytes) return 3;
    index << section.name << '\t' << std::hex << section.virtual_address << '\t'
          << section.virtual_size << '\t' << section.executable << '\t' << filename << '\n';
    std::cout << section.name << " " << std::hex << section.virtual_address << " "
              << section.virtual_size << '\n';
  }
  return id && index ? 0 : 4;
}
