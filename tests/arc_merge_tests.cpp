#include <mod/arc_merge.h>
#include <mod/ios_mod_manager.h>
#include <cassert>
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
    // Standard Nintendo U8 has 12-byte nodes and is not a Sonic '06 archive.
    std::string nintendo;
    be(nintendo,0x55aa382d); be(nintendo,32); be(nintendo,54); be(nintendo,96); nintendo.resize(32,'\0');
    be(nintendo,0x01000000); be(nintendo,0); be(nintendo,3);
    be(nintendo,1); be(nintendo,96); be(nintendo,1);
    be(nintendo,10); be(nintendo,97); be(nintendo,1);
    nintendo += std::string("\0Keep.bin\0Raw.bin\0",18); nintendo.resize(96,'\0'); nintendo += "ab";
    bad(nintendo);
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
    std::cout << "Passed: Sonic '06 U8 archive merging, independent zlib decoding, priority, loose/packed/extracted layouts, full replacements, content cache, malformed inputs and overlay rollback.\n";
}
