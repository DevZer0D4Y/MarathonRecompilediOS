#include <mod/ios_mod_manager.h>
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace ios_mod_manager;

static void write(const fs::path& path, const std::string& contents)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path) << contents;
}

void ArchiveMergeTests(const fs::path& root);

int main(int argc, char** argv)
{
    assert(argc == 2); // Use a disposable scratch directory.
    const fs::path root = fs::absolute(argv[1]);
    assert(!fs::exists(root));
    fs::create_directories(root);
    const auto source = root / "source";
    const auto user = root / "user";
    const std::string asset = "[Main]\nIncludeDirCount=1\nIncludeDir0=\".\"\n[Desc]\nTitle=\"Test mod\"\n";
    write(source / "First" / "mod.ini", asset);
    write(source / "First" / "xenon/archives/sonic.arc", "fixture");
    std::string error;
    assert(Import(user, source / "First", error));
    assert(fs::exists(user / "mods/First/xenon/archives/sonic.arc"));
    assert(!Import(user, source / "First", error));
    auto entries = Scan(user);
    assert(entries.size() == 1 && !entries[0].enabled && entries[0].issue.empty());
    entries[0].enabled = true;
    assert(Save(user, entries, error));
    entries = Scan(user);
    assert(entries[0].enabled);

    write(user / "mods/Second/mod.ini", asset);
    write(user / "mods/DLL/mod.ini", "[Main]\nDLLFile=plugin.dll\nIncludeDirCount=1\nIncludeDir0=.\n");
    write(user / "mods/Code/mod.ini", "[Main]\nCodeFile=custom.hmm\n");
    write(user / "mods/Escape/mod.ini", "[Main]\nIncludeDirCount=1\nIncludeDir0=../..\n");
    write(user / "mods/Windows/mod.ini", "[Main]\nIncludeDirCount=1\nIncludeDir0=C:\\mods\\foo\n");
    entries = Scan(user);
    assert(entries[0].enabled && entries[0].iniPath.filename() == "mod.ini");
    for (auto& entry : entries)
        if (entry.iniPath.parent_path().filename() != "First" && entry.iniPath.parent_path().filename() != "Second")
            assert(!entry.issue.empty() && !entry.enabled);
    auto second = std::find_if(entries.begin(), entries.end(), [](const Entry& e) { return e.iniPath.parent_path().filename() == "Second"; });
    second->enabled = true;
    std::rotate(entries.begin(), second, second + 1);
    assert(Save(user, entries, error));
    entries = Scan(user);
    assert(entries[0].iniPath.parent_path().filename() == "Second" && entries[0].enabled);
    // Removing earlier entries must not lose later entries' settings/order.
    fs::remove_all(user / "mods/Second");
    entries = Scan(user);
    assert(entries[0].iniPath.parent_path().filename() == "First" && entries[0].enabled);

    assert(ResolveRelative(user, "mods\\First\\mod.ini") == user / "mods/First/mod.ini");
    assert(ResolveRelative(user, "../escape").empty());
    assert(ResolveRelative(user, "/absolute").empty());
    assert(ResolveRelative(user, "C:\\absolute").empty());
    write(user / "mods/Save/mod.ini", "[Main]\nIncludeDirCount=1\nIncludeDir0=.\nSaveFile=../../save\n");
    assert(!CompatibilityIssue(user / "mods/Save/mod.ini").empty());
    write(source / "Symlink/mod.ini", asset);
    fs::create_symlink(source / "First/xenon/archives/sonic.arc", source / "Symlink/linked");
    assert(!Import(user, source / "Symlink", error));
    assert(!fs::exists(user / "mods/Symlink"));

    write(user / "mods/Dependency/mod.ini", "[Main]\nID=base\nIncludeDirCount=1\nIncludeDir0=.\n[Desc]\nVersion=1.2.0\n");
    write(user / "mods/Dependent/mod.ini", "[Main]\nIncludeDirCount=1\nIncludeDir0=.\nDependsCount=1\nDepends0=\"base|Base mod|https://example.com|1.2\"\n");
    entries = Scan(user);
    for (auto& e : entries) e.enabled = e.iniPath.parent_path().filename() == "Dependent";
    assert(!SelectionIssue(entries).empty());
    assert(!Save(user, entries, error));
    for (auto& e : entries) if (e.iniPath.parent_path().filename() == "Dependency") e.enabled = true;
    assert(SelectionIssue(entries).empty());
    assert(Save(user, entries, error));
    write(user / "mods/Dependency/mod.ini", "[Main]\nID=base\nIncludeDirCount=1\nIncludeDir0=.\n[Desc]\nVersion=1.1.9\n");
    assert(!SelectionIssue(entries).empty());

    // Relative database paths continue to work after the sandbox path changes.
    const auto moved = root / "moved-user";
    fs::rename(user, moved);
    entries = Scan(moved);
    for (const auto& e : entries) assert(e.iniPath.string().starts_with(moved.string()));

    // Exercise the same immutable file overlay used by ModLoader on iOS.
    const auto overlayUser = root / "overlay-user";
    const std::string high = "[Main]\nIncludeDirCount=2\nIncludeDir0=Assets\nIncludeDir1=Extras\nSaveFile=saves\n";
    write(overlayUser / "mods/High/mod.ini", high);
    write(overlayUser / "mods/High/Assets/XENON/Archives/Sonic.ARC", "high-priority archive");
    write(overlayUser / "mods/High/Extras/xenon/archives/extra.arc", "second include directory");
    write(overlayUser / "mods/Low/mod.ini", asset);
    write(overlayUser / "mods/Low/xenon/archives/sonic.arc", "low-priority archive");
    entries = Scan(overlayUser);
    for (auto& e : entries) e.enabled = true;
    assert(Save(overlayUser, entries, error));
    FileOverlay overlay;
    overlay.Load(overlayUser);
    const auto highArchive = overlayUser / "mods/High/Assets/XENON/Archives/Sonic.ARC";
    const auto lowArchive = overlayUser / "mods/Low/xenon/archives/sonic.arc";
    assert(overlay.Resolve("D:\\xenon\\archives\\sonic.arc") == highArchive);
    assert(overlay.Resolve("GAME:/XENON/ARCHIVES/SONIC.ARC") == highArchive);
    assert(overlay.Resolve("xenon/archives/extra.arc") == overlayUser / "mods/High/Extras/xenon/archives/extra.arc");
    assert(overlay.Resolve("D:\\xenon\\missing.arc").empty()); // Caller falls back to original game data.
    assert(overlay.Resolve("D:\\..\\outside").empty());
    assert(overlay.Resolve("/absolute").empty());
    assert(overlay.Resolve("other:\\xenon\\archives\\sonic.arc").empty());
    const auto directories = overlay.SearchDirectories("D:\\XENON\\ARCHIVES");
    assert(directories.size() == 3 && directories.front() == highArchive.parent_path());
    assert(overlay.SearchDirectories("D:\\..").empty());
    assert(overlay.SearchDirectories("save:\\").empty());
    assert(overlay.IncludeDirectories(0)->size() == 2 && overlay.IncludeDirectories(2) == nullptr);
    assert(overlay.SaveFile() == overlayUser / "mods/High/saves/SonicNextSaveData.bin");
    assert(overlay.Resolve("SAVE:\\SonicNextSaveData.bin") == overlay.SaveFile());
    assert(overlay.Resolve("save:\\..\\normal-save.bin").empty());

    std::reverse(entries.begin(), entries.end());
    assert(Save(overlayUser, entries, error));
    overlay.Load(overlayUser);
    assert(overlay.Resolve("D:\\xenon\\archives\\sonic.arc") == lowArchive);
    entries[0].enabled = false;
    assert(Save(overlayUser, entries, error));
    overlay.Load(overlayUser);
    assert(overlay.Resolve("D:\\xenon\\archives\\sonic.arc") == highArchive);
    for (auto& e : entries) e.enabled = false;
    assert(Save(overlayUser, entries, error));
    overlay.Load(overlayUser);
    assert(overlay.Resolve("D:\\xenon\\archives\\sonic.arc").empty());
    assert(overlay.SaveFile().empty() && overlay.SearchDirectories("game:\\").empty());

    // Do not advertise Unleashed hooks or executable patches as Sonic '06 support.
    write(overlayUser / "mods/Merge/mod.ini", "[Details]\nMerge=true\n");
    assert(CompatibilityIssue(overlayUser / "mods/Merge/mod.ini").empty());
    write(overlayUser / "mods/Append/mod.ini", asset);
    write(overlayUser / "mods/Append/+sonic.ar.00", "unsupported append archive");
    assert(!CompatibilityIssue(overlayUser / "mods/Append/mod.ini").empty());
    write(overlayUser / "mods/Executable/mod.ini", asset);
    write(overlayUser / "mods/Executable/default.xex", "unsupported CPU patch");
    assert(!CompatibilityIssue(overlayUser / "mods/Executable/mod.ini").empty());
    write(overlayUser / "mods/CaseCollision/mod.ini", asset);
    write(overlayUser / "mods/CaseCollision/sonic.arc", "one");
    write(overlayUser / "mods/CaseCollision/Sonic.ARC", "two");
    // On default macOS volumes these spellings identify the same file.
    if (!fs::equivalent(overlayUser / "mods/CaseCollision/sonic.arc", overlayUser / "mods/CaseCollision/Sonic.ARC"))
        assert(!CompatibilityIssue(overlayUser / "mods/CaseCollision/mod.ini").empty());
    write(overlayUser / "mods/Malformed/mod.ini", "[Main\nIncludeDirCount=1\n");
    assert(!CompatibilityIssue(overlayUser / "mods/Malformed/mod.ini").empty());

    // UMM file replacement without merging, with an explicit save filename.
    write(overlayUser / "mods/UMM/mod.ini", "[Details]\nTitle=UMM\nSave=custom-save.bin\n");
    write(overlayUser / "mods/UMM/xenon/archives/umm.arc", "UMM archive");
    entries = Scan(overlayUser);
    for (auto& e : entries) e.enabled = e.iniPath.parent_path().filename() == "UMM";
    assert(Save(overlayUser, entries, error));
    overlay.Load(overlayUser);
    assert(overlay.Resolve("game:\\xenon\\archives\\umm.arc") == overlayUser / "mods/UMM/xenon/archives/umm.arc");
    assert(overlay.Resolve("save:\\SonicNextSaveData.bin") == overlayUser / "mods/UMM/custom-save.bin");
    const auto relocated = root / "relocated-overlay-user";
    fs::rename(overlayUser, relocated);
    overlay.Load(relocated);
    assert(overlay.Resolve("game:\\xenon\\archives\\umm.arc") == relocated / "mods/UMM/xenon/archives/umm.arc");
    assert(overlay.SaveFile() == relocated / "mods/UMM/custom-save.bin");

    const auto nestedUser = root / "nested-source/user";
    write(root / "nested-source/mod.ini", asset);
    assert(!Import(nestedUser, root / "nested-source", error));
    assert(!fs::exists(nestedUser / "mod-import.tmp"));
    assert(!fs::exists(nestedUser / "mods/nested-source"));

    const auto linkedUser = root / "linked-user";
    fs::create_directories(linkedUser);
    fs::create_directory_symlink(relocated / "mods", linkedUser / "mods");
    assert(!Import(linkedUser, source / "First", error));
    bool rejected = false;
    try { Scan(linkedUser); } catch (const std::exception&) { rejected = true; }
    assert(rejected);

    // Persistence must not follow a malicious temporary-file link.
    write(root / "keep.txt", "preserve me");
    fs::create_symlink(root / "keep.txt", DatabasePath(relocated).string() + ".tmp");
    assert(!Save(relocated, {}, error));
    std::string kept;
    std::ifstream(root / "keep.txt") >> kept;
    assert(kept == "preserve");

    // Khronos-based mods (Physics, Physics Companion) patch the executable or need patches that
    // only exist as executable edits. They must be rejected instead of crashing the game.
    const auto patched = root / "patched";
    const std::string umm = "[Details]\nTitle=\"%s\"\nPlatform=\"Xbox 360\"\n[Filesystem]\nMerge=\"True\"\n";
    auto ummIni = [&](const std::string& title, const std::string& extra) {
        std::string text = umm;
        text.replace(text.find("%s"), 2, title);
        return text + extra;
    };
    write(patched / "Khronos/mod.ini", ummIni("Khronos", ""));
    write(patched / "Khronos/patch.mlua", "WriteVirtualBytes(0x82AEA31C|\"01 01\")\n-- DLL_STRING: .string \"game:\\\\Core.xex\"\n");
    assert(CompatibilityIssue(patched / "Khronos/mod.ini").find("Khronos") != std::string::npos);
    write(patched / "Companion/mod.ini", ummIni("Companion", ""));
    write(patched / "Companion/patch.mlua", "DecryptExecutable()\nWriteBytes(Executable|0xB18E2B|\"1C821A4CB0\")\n");
    assert(CompatibilityIssue(patched / "Companion/mod.ini").find("executable") != std::string::npos);
    write(patched / "AssetPatch/mod.ini", ummIni("Asset patch", ""));
    write(patched / "AssetPatch/patch.mlua", "BeginBlock(\"core\\\\archives\\\\player.arc\")\nEndBlock(\"core\\\\archives\\\\player.arc\")\n");
    assert(CompatibilityIssue(patched / "AssetPatch/mod.ini").empty());

    const std::string physics = "[Patches]\nRequiredPatches=\"ControllableBoundAttack.mlua, Restore Sonic Action Gauge.mlua,DisableCharacterStumble.mlua\"\n";
    write(patched / "Physics/mod.ini", ummIni("Physics", physics));
    SetBuiltInCodes({ "ControllableBoundAttack", "RestoreSonicActionGauge" });
    const auto physicsIssue = CompatibilityIssue(patched / "Physics/mod.ini");
    assert(physicsIssue.find("DisableCharacterStumble.mlua") != std::string::npos);
    assert(physicsIssue.find("ControllableBoundAttack") == std::string::npos);
    write(patched / "Codes/mod.ini", ummIni("Codes", "[Patches]\nRequiredPatches=\"ControllableBoundAttack.mlua,restore_sonic_action_gauge.mlua\"\n"));
    assert(CompatibilityIssue(patched / "Codes/mod.ini").empty());
    const auto codes = RequiredCodes(patched / "Codes/mod.ini");
    assert(codes.size() == 2 && codes[0] == "ControllableBoundAttack" && codes[1] == "RestoreSonicActionGauge");
    SetBuiltInCodes({});
    assert(!CompatibilityIssue(patched / "Codes/mod.ini").empty());

    ArchiveMergeTests(root / "archives");
    fs::remove_all(root);
    std::cout << "Passed: mod imports, collisions, persistence/order, dependency versions, unsupported requirements, path confinement, sandbox relocation, Sonic '06 file overlay priority/case handling, directory lookup, reload, mod saves, executable patches and required built-in codes.\n";
}
