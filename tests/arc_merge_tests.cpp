#include <mod/arc_merge.h>
#include <mod/ios_mod_manager.h>
#include <cassert>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <functional>
#include <zlib.h>

namespace fs = std::filesystem;
namespace arc = ios_mod_manager::arc;
namespace {
void put(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(bytes.data(), bytes.size());
}
std::string get(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void be(std::string& s, unsigned v) {
    for (int shift : {24,16,8,0}) s += char(v >> shift);
}
unsigned u32(const std::string& s, size_t p) {
    assert(p + 4 <= s.size());
    unsigned v=0;
    for (int i=0;i<4;++i) v = v*256 + (unsigned char)s[p+i];
    return v;
}
void rejects(const std::function<void()>& f) {
    bool caught=false;
    try { f(); } catch (const std::exception&) { caught=true; }
    assert(caught);
}
// Independent fixture encoder: flat archive with a zlib member and a raw member.
void fixture(const fs::path& p) {
    const std::string text="original compressed payload";
    std::string compressed(compressBound(text.size()), '\0');
    uLongf n=compressed.size();
    assert(compress2((Bytef*)compressed.data(), &n, (const Bytef*)text.data(), text.size(), 9)==Z_OK);
    compressed.resize(n);
    std::string s;
    be(s,0x55aa382d); be(s,32); be(s,66); be(s,128); s.resize(32,'\0');
    be(s,0x01000000); be(s,0); be(s,3); be(s,0);
    be(s,1); be(s,128); be(s,compressed.size()); be(s,text.size());
    be(s,10); be(s,128+compressed.size()); be(s,4); be(s,0);
    s += std::string("\0Keep.bin\0Raw.bin\0",18); assert(s.size()==98);
    s.resize(128,'\0'); s+=compressed; s+="base";
    put(p,s);
}
// Independent node walk and zlib decoder checks the production writer's output.
std::map<std::string,std::string> decoded(const fs::path& p) {
    auto s=get(p); assert(u32(s,0)==0x55aa382d);
    size_t start=u32(s,4), count=u32(s,start+8), strings=start+16*count;
    std::vector<std::pair<unsigned,std::string>> dirs{{unsigned(count),""}};
    std::map<std::string,std::string> result;
    for(unsigned i=1;i<count;++i) {
        while(i>=dirs.back().first) dirs.pop_back();
        size_t node=start+i*16; unsigned flags=u32(s,node);
        std::string name=s.c_str()+strings+(flags&0xffffff);
        std::string path=dirs.back().second+name;
        if(flags>>24) { dirs.push_back({u32(s,node+8),path+'/'}); continue; }
        auto bytes=s.substr(u32(s,node+4),u32(s,node+8));
        unsigned expanded=u32(s,node+12);
        if(expanded) {
            std::string raw(expanded,'\0'); uLongf n=expanded;
            assert(uncompress((Bytef*)raw.data(),&n,(const Bytef*)bytes.data(),bytes.size())==Z_OK && n==expanded);
            bytes=std::move(raw);
        }
        assert(result.emplace(path,std::move(bytes)).second);
    }
    return result;
}
}

void ArchiveMergeTests(const fs::path& root) {
    const auto game=root/"game", cache=root/"cache", low=root/"low", high=root/"high", full=root/"full";
    const std::string key="xenon/archives/test.arc";
    fixture(game/key);
    auto original=get(game/key);
    assert(decoded(game/key).at("Keep.bin")=="original compressed payload");
    fs::create_directories(low); fs::create_directories(high); fs::create_directories(full);
    put(low/key/"RAW.bin","lower"); put(low/key/"Nested/Empty","");
    fs::create_directories(low/key/"EmptyDir");
    put(high/key/"raw.BIN","upper"); put(high/key/"Nested/New","new");
    auto run=[&](std::vector<arc::Layer> layers) { return arc::BuildOverlays(game,cache,layers); };
    auto outputs=run({{high,true,{}},{low,true,{}}});
    auto file=outputs.at(key);
    auto contents=decoded(file);
    assert(contents.at("Raw.bin")=="upper" && contents.at("Keep.bin")=="original compressed payload");
    assert(contents.at("Nested/Empty").empty() && contents.at("Nested/New")=="new");
    assert(arc::Read(file).at("keep.bin").expandedSize!=0);
    auto timestamp=fs::last_write_time(file);
    assert(run({{high,true,{}},{low,true,{}}}).at(key)==file && fs::last_write_time(file)==timestamp);
    // Fingerprint uses bytes, even if size and modification date remain identical.
    auto sourceTime=fs::last_write_time(high/key/"raw.BIN");
    put(high/key/"raw.BIN","other"); fs::last_write_time(high/key/"raw.BIN",sourceTime);
    auto changed=run({{high,true,{}},{low,true,{}}}).at(key);
    assert(changed!=file && decoded(changed).at("Raw.bin")=="other");
    assert(get(game/key)==original && get(high/key/"raw.BIN")=="other");
    assert(decoded(run({{low,true,{}},{high,true,{}}}).at(key)).at("Raw.bin")=="lower");
    // Partial packed archives and extensionless extracted directories.
    fs::create_directories(root/"packed"/"xenon/archives");
    arc::Write(root/"packed"/key,arc::ReadFolder(low/key));
    assert(decoded(run({{root/"packed",true,{}}}).at(key)).at("Raw.bin")=="lower");
    fs::rename(low/key,low/"xenon/archives/test");
    assert(decoded(run({{low,true,{}}}).at(key)).at("Raw.bin")=="lower");
    // Loose replacements locate a member through its archive index.
    const auto loose=root/"loose";
    put(loose/"xenon/keep.BIN","loose compressed replacement");
    assert(decoded(run({{loose,true,{}}}).at(key)).at("Keep.bin")=="loose compressed replacement");
    put(loose/"Nested/New","loose added member");
    assert(decoded(run({{loose,true,{}},{high,true,{}}}).at(key)).at("Nested/New")=="loose added member");
    // Full replacement cuts off the original and lower mods.
    fs::create_directories(full/"xenon/archives");
    arc::Write(full/key,arc::ReadFolder(high/key));
    assert(run({{full,false,{}},{low,true,{}}}).empty());
    auto cut=decoded(run({{low,true,{}},{full,false,{}}}).at(key));
    assert(!cut.contains("Keep.bin") && cut.at("raw.BIN")=="lower");
    assert(run({{full,true,{key}},{low,true,{}}}).empty());
    assert(decoded(run({{low,true,{}},{full,true,{key}}}).at(key)).size()==cut.size());
    // New archives need no original. Empty archives are valid too.
    auto created=arc::BuildOverlays(root/"missing",cache,{{high,true,{}}});
    assert(decoded(created.at(key)).at("raw.BIN")=="other");
    arc::Write(root/"empty.arc",{});
    assert(arc::Read(root/"empty.arc").empty() && decoded(root/"empty.arc").empty());
    // A corrupt generated cache is rebuilt and never changes source files.
    put(changed,"bad cache");
    assert(decoded(run({{high,true,{}},{low,true,{}}}).at(key)).at("Raw.bin")=="other");
    assert(get(game/key)==original);
    // Unsafe/truncated tables and payloads must fail before the guest runs.
    auto bad=[&](std::string bytes) { put(root/"bad.arc",bytes); rejects([&]{arc::Read(root/"bad.arc");}); };
    bad(original.substr(0,40));
    auto bytes=original; bytes[0]=0; bad(bytes);
    bytes=original; bytes[11]=1; bad(bytes);
    bytes=original; bytes[48]=2; bad(bytes);
    bytes=original; bytes[51]=char(255); bad(bytes);
    bytes=original; bytes[52]=char(255); bad(bytes);
    bytes=original; bytes.replace(81,8,"../evil!"); bad(bytes);
    bytes=original; bytes[67]=1; bad(bytes);
    fs::create_symlink(game/key,root/"link.arc"); rejects([&]{arc::Read(root/"link.arc");});
    fs::create_directory_symlink(cache,root/"linked-cache");
    rejects([&]{arc::BuildOverlays(game,root/"linked-cache",{{high,true,{}}});});
    // A higher opaque full replacement prevents irrelevant lower patch parsing.
    const auto opaque=root/"opaque";
    put(opaque/key,"complete replacement handled by the game");
    assert(run({{opaque,false,{}},{low,true,{}}}).empty());
    const auto collision=root/"collision";
    put(collision/key/"Raw.bin/child","conflict");
    rejects([&]{run({{collision,true,{}}});});
    const auto ambiguous=root/"ambiguous";
    put(ambiguous/"xenon/archives/test/file","extracted");
    put(ambiguous/key,original);
    rejects([&]{run({{ambiguous,true,{}}});});
    // Ordinary U8 input is converted to the extended game layout, never
    // mistaken for compressed 16-byte metadata.
    std::string nintendo;
    be(nintendo,0x55aa382d); be(nintendo,32); be(nintendo,54); be(nintendo,96); nintendo.resize(32,'\0');
    be(nintendo,0x01000000); be(nintendo,0); be(nintendo,3);
    be(nintendo,1); be(nintendo,96); be(nintendo,1);
    be(nintendo,10); be(nintendo,97); be(nintendo,1);
    nintendo += std::string("\0Keep.bin\0Raw.bin\0",18); nintendo.resize(96,'\0'); nintendo += "ab";
    put(root/"ordinary.arc",nintendo);
    auto ordinary=arc::Read(root/"ordinary.arc"); assert(ordinary.size()==2);
    arc::Write(root/"converted.arc",ordinary);
    assert(decoded(root/"converted.arc").at("Keep.bin")=="a");
    assert(decoded(root/"converted.arc").at("Raw.bin")=="b");
    assert(u32(get(root/"converted.arc"),16)!=0);
    put(game/"win32/archives/ordinary.arc",nintendo);
    put(high/"win32/archives/ordinary.arc/Raw.bin","converted replacement");
    auto mixed=decoded(run({{high,true,{}}}).at("win32/archives/ordinary.arc"));
    assert(mixed.at("Keep.bin")=="a" && mixed.at("Raw.bin")=="converted replacement");
    std::string tree;
    be(tree,0x55aa382d); be(tree,32); be(tree,94); be(tree,256); tree.resize(32,'\0');
    for(const auto& node:std::vector<std::array<unsigned,3>>{
        {0x01000000,0,6},{0x01000001,0,5},{5,256,1},
        {0x01000007,1,5},{11,257,0},{17,257,1}}) {
        for(auto field:node) be(tree,field);
    }
    tree+=std::string("\0Dir\0A\0Sub\0Empty\0Root\0",22); tree.resize(256,'\0'); tree+="xy";
    put(root/"ordinary-tree.arc",tree);
    arc::Write(root/"converted-tree.arc",arc::Read(root/"ordinary-tree.arc"));
    auto treeContents=decoded(root/"converted-tree.arc");
    assert(treeContents.at("Dir/A")=="x" && treeContents.at("Dir/Sub/Empty").empty() && treeContents.at("Root")=="y");
    // Root labels, root parent metadata and directory extension words are not
    // file payload fields. Older strict parsing rejected these valid layouts.
    bytes=original;
    bytes[35]=1; bytes[36]=char(0xde); bytes[37]=char(0xad); bytes[38]=char(0xbe); bytes[39]=char(0xef);
    bytes[44]=char(0xca); bytes[45]=char(0xfe); bytes[46]=char(0xba); bytes[47]=char(0xbe);
    put(root/"metadata.arc",bytes);
    assert(arc::Fingerprint(arc::Read(root/"metadata.arc"))==arc::Fingerprint(arc::Read(game/key)));
    put(game/key,bytes);
    assert(decoded(run({{high,true,{}}}).at(key)).at("Keep.bin")=="original compressed payload");
    put(game/key,original);
    auto dirs=get(root/"packed"/key);
    for(unsigned i=1;i<u32(dirs,40);++i) {
        auto node=32+i*16;
        if(u32(dirs,node)>>24) dirs[node+12]=char(0xa5);
    }
    put(root/"directory-metadata.arc",dirs);
    assert(arc::Fingerprint(arc::Read(root/"directory-metadata.arc"))==arc::Fingerprint(arc::Read(root/"packed"/key)));
    // Runtime overlay integration, enumeration precedence, reload and rollback.
    const auto user=root/"user";
    put(user/"game"/key,original);
    put(user/"mods/M/mod.ini","[Details]\nTitle=Merge mod\nMerge=true\n");
    put(user/"mods/M"/key/"Raw.bin","runtime");
    auto entries=ios_mod_manager::Scan(user); assert(entries.size()==1 && entries[0].issue.empty());
    entries[0].enabled=true; std::string error;
    assert(ios_mod_manager::Save(user,entries,error));
    ios_mod_manager::FileOverlay overlay; overlay.Load(user);
    const auto resolved=overlay.Resolve("game:\\xenon\\archives\\test.arc");
    assert(fs::is_regular_file(resolved) && decoded(resolved).at("Raw.bin")=="runtime");
    assert(overlay.SearchDirectories("game:\\xenon\\archives").front()==resolved.parent_path());
    fs::remove_all(user/"mods/M"/key); put(user/"mods/M"/key,"invalid arc");
    rejects([&]{overlay.Load(user);});
    assert(overlay.Resolve("game:\\xenon\\archives\\test.arc")==resolved);
    entries[0].enabled=false; assert(ios_mod_manager::Save(user,entries,error)); overlay.Load(user);
    assert(overlay.Resolve("game:\\xenon\\archives\\test.arc").empty());
    for (const auto& e : fs::directory_iterator(cache)) assert(!e.path().filename().string().starts_with(".building-"));
    // Exercise a genuine supplied archive without modifying or distributing it.
    if(const auto supplied=std::getenv("ARC_TEST_REAL_ARCHIVE")) {
        const fs::path real=supplied;
        assert(fs::is_regular_file(real));
        auto members=arc::Read(real); auto before=arc::Fingerprint(members);
        arc::Write(root/"real-roundtrip.arc",members);
        assert(arc::Fingerprint(arc::Read(root/"real-roundtrip.arc"))==before);
        assert(decoded(real)==decoded(root/"real-roundtrip.arc"));
    }
    // Optional downloaded mod corpus: run the complete manager/merge pipeline,
    // compare every decoded patch member and retain an original-only sentinel.
    if(const auto supplied=std::getenv("ARC_TEST_MOD_FOLDER")) {
        const fs::path mod=supplied, corpusUser=root/"corpus-user";
        std::map<std::string,std::map<std::string,std::string>> patches;
        for(const auto& entry:fs::recursive_directory_iterator(mod)) {
            if(entry.path().extension()!=".arc" || !entry.is_regular_file()) continue;
            auto key=entry.path().lexically_relative(mod).generic_string();
            patches.emplace(key,decoded(entry.path()));
            auto base=original; base[44]=char(0xa5); // Nonzero root extension metadata.
            put(corpusUser/"game"/key,base);
        }
        assert(!patches.empty());
        std::string importError;
        assert(ios_mod_manager::Import(corpusUser,mod,importError));
        auto mods=ios_mod_manager::Scan(corpusUser);
        assert(mods.size()==1 && mods[0].issue.empty()); mods[0].enabled=true;
        assert(ios_mod_manager::Save(corpusUser,mods,importError));
        ios_mod_manager::FileOverlay actual; actual.Load(corpusUser);
        for(const auto& [key,patch]:patches) {
            auto path=actual.Resolve("game:\\"+key);
            assert(fs::is_regular_file(path));
            auto merged=decoded(path);
            for(const auto& [member,bytes]:patch) assert(merged.at(member)==bytes);
            assert(merged.at("Keep.bin")=="original compressed payload");
        }
    }
    std::cout << "Passed: Sonic '06 U8 archive merging, independent zlib decoding, priority, loose/packed/extracted layouts, full replacements, content cache, malformed inputs and overlay rollback.\n";
}
