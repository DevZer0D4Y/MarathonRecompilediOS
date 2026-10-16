#include <mod/arc_merge.h>
#include <mod/ios_mod_manager.h>
#include <mod/umm_patch.h>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <zlib.h>

namespace fs = std::filesystem;
namespace arc = ios_mod_manager::arc;
namespace patch = ios_mod_manager::patch;

namespace {
void put(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(bytes.data(), bytes.size());
}
std::string get(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
std::string bytes(const std::vector<uint8_t>& data) { return {data.begin(), data.end()}; }
// Independent member decoder.
std::string member(const arc::Members& members, const std::string& key) {
    const auto& m = members.at(key);
    std::ifstream in(m.source, std::ios::binary);
    in.seekg(m.offset);
    std::string payload(m.size, '\0');
    in.read(payload.data(), payload.size());
    if (!m.expandedSize) return payload;
    std::string out(m.expandedSize, '\0');
    uLongf length = out.size();
    assert(uncompress(reinterpret_cast<Bytef*>(out.data()), &length, reinterpret_cast<const Bytef*>(payload.data()), payload.size()) == Z_OK);
    return out;
}
void rejects(const std::function<void()>& f) {
    bool caught = false;
    try { f(); } catch (const std::exception&) { caught = true; }
    assert(caught);
}
patch::Package samplePackage() {
    patch::Package package;
    package.categories.push_back({ "motion", { { "walk", "player/sonic/walk.xnm" }, { "run", "player/sonic/run.xnm" } } });
    package.categories.push_back({ "model", { { "body", "player/sonic/sonic.xno" } } });
    package.categories.push_back({ "empty", {} });
    return package;
}
std::string mlua(const std::string& body, const std::string& platform = "Xbox 360") {
    return "--[Patch]--\nTitle(\"Test\")\nAuthor(\"Tester\")\nPlatform(\"" + platform + "\")\nBlurb(\"Does a thing.\")\n\n--[Functions]--\n" + body;
}
}

void PatchTests(const fs::path& root) {
    fs::create_directories(root);

    // Packages round-trip, including shared strings and empty categories.
    {
        const auto data = samplePackage().Serialise();
        assert(bytes(data).substr(0x18, 4) == "BINA");
        const auto parsed = patch::Package::Parse(data);
        assert(parsed.categories.size() == 3 && parsed.categories[0].files[1].path == "player/sonic/run.xnm");
        assert(parsed.categories[2].files.empty());
        assert(parsed.Serialise() == data);
        rejects([] { patch::Package::Parse(std::vector<uint8_t>(64, 0)); });
    }

    // Classification follows the platform sections and the executable layout.
    const auto patches = root / "patches";
    put(patches / "Data.mlua", mlua("DecryptExecutable()\nWriteBytes(Executable|0x39BE4|\"3F 80 00 00\")\nWriteVirtualBytes(0x82000100|\"01 02\")\n"));
    put(patches / "Code.mlua", mlua("WriteNopPPC(Executable|0x16E76C)\n"));
    put(patches / "Virtual.mlua", mlua("WriteVirtualBytes(0x8265383C|\"48 00\")\n"));
    put(patches / "Lua.mlua", mlua("BeginBlock(\"core\\archives\\player.arc\")\nParameterEdit(\"core\\player\"|\"c_x\"|\"0\")\nEndBlock()\n"));
    put(patches / "Ps3.mlua", mlua("WriteBytes(Executable|0x39BE4|\"00\")\n", "PlayStation 3"));
    put(patches / "Sections.mlua", mlua("Xbox 360:\nWriteVirtualBytes(0x82000200|\"AA\")\n\nPlayStation 3:\nParameterEdit(\"core\\player\"|\"c_x\"|\"0\")\n", "All Systems"));
    put(patches / "Header.mlua", mlua("WriteByte(Executable|0x100|FF)\n"));
    assert(patch::Inspect(patches / "Data.mlua").issue.empty());
    assert(patch::Inspect(patches / "Data.mlua").title == "Test" && patch::Inspect(patches / "Data.mlua").blurb == "Does a thing.");
    assert(patch::Inspect(patches / "Code.mlua").issue.find("game code at 0x8216B76C") != std::string::npos);
    assert(patch::Inspect(patches / "Virtual.mlua").issue.find("game code") != std::string::npos);
    assert(patch::Inspect(patches / "Lua.mlua").issue.find("Lua") != std::string::npos);
    assert(patch::Inspect(patches / "Ps3.mlua").issue.find("PlayStation 3") != std::string::npos);
    assert(patch::Inspect(patches / "Sections.mlua").issue.empty());
    assert(!patch::Inspect(patches / "Header.mlua").issue.empty());
    assert(patch::Scan(root).size() == 7);

    // A game with an archive (one zlib member, one raw), a package and loose sound files.
    const auto game = root / "game";
    const auto memberDir = root / "fixture";
    put(memberDir / "xenon/player/sonic.pkg", bytes(samplePackage().Serialise()));
    put(memberDir / "xenon/player/data.bin", "0123456789");
    auto members = arc::ReadFolder(memberDir);
    const std::string text = "compressed member payload";
    std::string compressed(compressBound(text.size()), '\0');
    uLongf compressedSize = compressed.size();
    assert(compress2(reinterpret_cast<Bytef*>(compressed.data()), &compressedSize, reinterpret_cast<const Bytef*>(text.data()), text.size(), 9) == Z_OK);
    compressed.resize(compressedSize);
    put(root / "zlib.bin", compressed);
    members.emplace("xenon/player/packed.bin", arc::Member{ "xenon/player/Packed.bin", false, root / "zlib.bin", 0, uint32_t(compressed.size()), uint32_t(text.size()) });
    members.emplace("xenon/player/keep.bin", arc::Member{ "xenon/player/keep.bin", false, root / "zlib.bin", 0, uint32_t(compressed.size()), uint32_t(text.size()) });
    fs::create_directories(game / "xenon/archives");
    arc::Write(game / "xenon/archives/player.arc", members);
    put(game / "xenon/sound/Stage.xma", "music");
    put(game / "xenon/sound/voice.csb", "voices");

    const auto resolve = [&](const std::string& key) -> fs::path {
        for (const auto& entry : fs::recursive_directory_iterator(game))
            if (arc::Key(entry.path().lexically_relative(game).generic_string()) == key) return entry.path();
        return {};
    };
    const auto list = [&](const std::string& directory) {
        std::vector<std::string> keys;
        if (fs::is_directory(game / directory))
            for (const auto& entry : fs::directory_iterator(game / directory))
                if (entry.is_regular_file()) keys.push_back(arc::Key(entry.path().lexically_relative(game).generic_string()));
        return keys;
    };
    // "All Systems" patches only run lines below a matching section label.
    put(patches / "Archive.mlua", mlua(
        "All Systems:\n"
        "BeginBlock(\"core\\archives\\player.arc\")\n"
        "PackageEdit(\"core\\player\\sonic.pkg\"|\"motion\"|\"walk\"|\"player/sonic/walk_new.xnm\")\n"
        "PackageEdit(\"core\\player\"|\"model\"|\"head\"|\"player/sonic/head.xno\")\n"
        "PackageAdd(\"core\\player\\sonic.pkg\"|\"motion\"|\"jump\"|\"player/sonic/jump.xnm\")\n"
        "PackageErase(\"core\\player\\sonic.pkg\"|\"motion\"|\"run\")\n"
        "WriteBytes(\"core\\player\\data.bin\"|0x2|\"AA BB\")\n"
        "WriteNullBytes(\"core\\player\\packed.bin\"|0x0|3)\n"
        "Copy(\"core\\player\\data.bin\"|\"core\\player\\copy.bin\"|false)\n"
        "WriteBase64(\"core\\player\\new\\b64.bin\"|\"SGVsbG8=\")\n"
        "EndBlock()\n"
        "Xbox 360:\n"
        "RenameByExtension(\"core\\sound\"|\"*.xma\"|\".xma_back\")\n"
        "WriteVirtualBytes(0x82000100|\"01 02\")\n", "All Systems"));

    const auto cache = root / "mod-cache";
    auto result = patch::Apply({ patches / "Archive.mlua", patches / "Data.mlua" }, cache, resolve, list);
    const auto output = result.files.at("xenon/archives/player.arc");
    const auto patched = arc::Read(output);
    const auto packageBytes = member(patched, "xenon/player/sonic.pkg");
    auto package = patch::Package::Parse(std::vector<uint8_t>(packageBytes.begin(), packageBytes.end()));
    const auto& motion = package.categories[0].files;
    assert(motion.size() == 2 && motion[0].name == "walk" && motion[0].path == "player/sonic/walk_new.xnm" && motion[1].name == "jump");
    assert(package.categories[1].files.size() == 2 && package.categories[1].files[1].name == "head"); // Directory form, new entry.
    assert(member(patched, "xenon/player/data.bin") == "01\xAA\xBB" "456789");
    assert(member(patched, "xenon/player/copy.bin") == member(patched, "xenon/player/data.bin"));
    assert(member(patched, "xenon/player/new/b64.bin") == "Hello");
    assert(member(patched, "xenon/player/packed.bin") == std::string(3, '\0') + text.substr(3));
    assert(patched.at("xenon/player/packed.bin").name == "xenon/player/Packed.bin"); // Spelling is kept.
    assert(patched.at("xenon/player/keep.bin").expandedSize == text.size()); // Untouched member stays compressed.
    assert(member(patched, "xenon/player/keep.bin") == text);
    assert(!fs::exists(result.files.at("xenon/sound/stage.xma"))); // Hidden like the rename did.
    assert(result.files.at("xenon/sound/stage.xma_back") == game / "xenon/sound/Stage.xma");
    assert(!result.files.contains("xenon/sound/voice.csb"));
    assert(result.memory.size() == 3);
    assert(result.memory[0].address == 0x82000100 && result.memory[1].address == 0x82036BE4);
    assert(bytes(result.memory[1].bytes) == std::string("\x3F\x80\x00\x00", 4));
    assert(get(game / "xenon/sound/Stage.xma") == "music"); // Originals are never modified.

    // Generated files are reused while the patches and inputs are unchanged.
    const auto stamp = fs::last_write_time(output);
    auto again = patch::Apply({ patches / "Archive.mlua", patches / "Data.mlua" }, cache, resolve, list);
    assert(again.files.at("xenon/archives/player.arc") == output && fs::last_write_time(output) == stamp);
    auto other = patch::Apply({ patches / "Data.mlua" }, cache, resolve, list);
    assert(!other.files.contains("xenon/archives/player.arc") && other.memory.size() == 2);
    assert(!fs::exists(output)); // Superseded outputs are removed.

    rejects([&] { patch::Apply({ patches / "Code.mlua" }, cache, resolve, list); });
    put(patches / "Missing.mlua", mlua("BeginBlock(\"core\\archives\\none.arc\")\nEndBlock()\n"));
    rejects([&] { patch::Apply({ patches / "Missing.mlua" }, cache, resolve, list); });
    put(patches / "Exists.mlua", mlua("BeginBlock(\"core\\archives\\player.arc\")\nCopy(\"core\\player\\data.bin\"|\"core\\player\\sonic.pkg\"|false)\nEndBlock()\n"));
    rejects([&] { patch::Apply({ patches / "Exists.mlua" }, cache, resolve, list); });

    // Selection persistence and folder import.
    std::string error;
    assert(patch::SaveSelection(root, { "Archive.mlua", "Data.mlua" }, error));
    assert((patch::LoadSelection(root) == std::set<std::string>{ "Archive.mlua", "Data.mlua" }));
    put(root / "download/repo/sub/Extra.mlua", mlua("WriteVirtualBytes(0x82000300|\"01\")\n"));
    put(root / "download/repo/README.md", "readme");
    assert(patch::Import(root, root / "download", error) == 1 && fs::exists(patches / "Extra.mlua"));
    assert(patch::Import(root, root / "fixture", error) == 0 && !error.empty());

    // The file overlay serves patched archives and hands memory writes to the loader.
    for (const auto& name : { "Code.mlua", "Missing.mlua", "Exists.mlua", "Lua.mlua", "Ps3.mlua", "Header.mlua", "Virtual.mlua" })
        fs::remove(patches / name);
    ios_mod_manager::FileOverlay overlay;
    overlay.Load(root);
    assert(arc::Read(overlay.Resolve("game:\\xenon\\archives\\player.arc")).contains("xenon/player/copy.bin"));
    assert(overlay.Resolve("game:\\xenon\\sound\\stage.xma_back") == game / "xenon/sound/Stage.xma");
    assert(!fs::exists(overlay.Resolve("game:\\xenon\\sound\\Stage.xma")));
    assert(overlay.MemoryWrites().size() == 3);
    put(patches / "Code.mlua", mlua("WriteNopPPC(Executable|0x16E76C)\n"));
    assert(patch::SaveSelection(root, { "Code.mlua" }, error));
    rejects([&] { ios_mod_manager::FileOverlay failing; failing.Load(root); });

    // Optionally classify a real patch collection, such as Knuxfan24/Sonic-06-Mod-Manager-Patches.
    if (const char* real = std::getenv("UMM_PATCHES_DIR")) {
        const auto realRoot = root / "real";
        fs::create_directories(realRoot);
        std::string importError;
        const auto count = patch::Import(realRoot, real, importError);
        size_t supported = 0;
        for (const auto& info : patch::Scan(realRoot)) supported += info.issue.empty();
        std::cout << "Real patches: " << supported << " of " << count << " runnable.\n";
        assert(count > 0 && supported > 0);
    }

    std::cout << "Passed: .mlua patches - package round trip, classification (code, Lua, platform, header), archive/package/file edits on compressed and raw members, renames, memory writes, cache reuse, import, selection and overlay.\n";
}
