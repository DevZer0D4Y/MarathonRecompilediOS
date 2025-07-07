#include "installer.h"

#include <xxh3.h>

#include "directory_file_system.h"
#include "iso_file_system.h"
#include "xcontent_file_system.h"

#include "hashes/episode_sonic.h"
#include "hashes/episode_shadow.h"
#include "hashes/episode_silver.h"
#include "hashes/episode_amigo.h"
#include "hashes/mission_sonic.h"
#include "hashes/mission_shadow.h"
#include "hashes/mission_silver.h"
#include "hashes/game.h"

static const std::string GameDirectory = "game";
static const std::string DLCDirectory = "dlc";
static const std::string EpisodeSonicDirectory = DLCDirectory + "/Episode Sonic Boss Attack";
static const std::string EpisodeShadowDirectory = DLCDirectory + "/Episode Shadow Boss Attack";
static const std::string EpisodeSilverDirectory = DLCDirectory + "/Episode Silver Boss Attack";
static const std::string EpisodeAmigoDirectory = DLCDirectory + "/Episode Team Attack Amigo";
static const std::string MissionSonicDirectory = DLCDirectory + "/Mission Pack Sonic Very Hard";
static const std::string MissionShadowDirectory = DLCDirectory + "/Mission Pack Shadow Very Hard";
static const std::string MissionSilverDirectory = DLCDirectory + "/Mission Pack Silver Very Hard";
static const std::string GameExecutableFile = "default.xex";
static const std::string DLCValidationFile = "download.arc";
static const std::string ISOExtension = ".iso";

static std::string fromU8(const std::u8string &str)
{
    return std::string(str.begin(), str.end());
}

static std::string fromPath(const std::filesystem::path &path)
{
    return fromU8(path.u8string());
}

static std::string toLower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
    return str;
};

static std::unique_ptr<VirtualFileSystem> createFileSystemFromPath(const std::filesystem::path &path)
{
    if (XContentFileSystem::check(path))
    {
        return XContentFileSystem::create(path);
    }
    else if (toLower(fromPath(path.extension())) == ISOExtension)
    {
        return ISOFileSystem::create(path);
    }
    else if (std::filesystem::is_directory(path))
    {
        return DirectoryFileSystem::create(path);
    }
    else
    {
        return nullptr;
    }
}

static bool copyFile(const FilePair &pair, const uint64_t *fileHashes, VirtualFileSystem &sourceVfs, const std::filesystem::path &targetDirectory, bool skipHashChecks, std::vector<uint8_t> &fileData, Journal &journal, const std::function<bool()> &progressCallback) {
    const std::string filename(pair.first);
    const uint32_t hashCount = pair.second;
    if (!sourceVfs.exists(filename))
    {
        journal.lastResult = Journal::Result::FileMissing;
        journal.lastErrorMessage = fmt::format("File {} does not exist in {}.", filename, sourceVfs.getName());
        return false;
    }

    if (!sourceVfs.load(filename, fileData))
    {
        journal.lastResult = Journal::Result::FileReadFailed;
        journal.lastErrorMessage = fmt::format("Failed to read file {} from {}.", filename, sourceVfs.getName());
        return false;
    }

    if (!skipHashChecks)
    {
        uint64_t fileHash = XXH3_64bits(fileData.data(), fileData.size());
        bool fileHashFound = false;
        for (uint32_t i = 0; i < hashCount && !fileHashFound; i++)
        {
            fileHashFound = fileHash == fileHashes[i];
        }

        if (!fileHashFound)
        {
            journal.lastResult = Journal::Result::FileHashFailed;
            journal.lastErrorMessage = fmt::format("File {} from {} did not match any of the known hashes.", filename, sourceVfs.getName());
            return false;
        }
    }

    std::filesystem::path targetPath = targetDirectory / std::filesystem::path(std::u8string_view((const char8_t *)(pair.first)));
    std::filesystem::path parentPath = targetPath.parent_path();
    if (!std::filesystem::exists(parentPath))
    {
        std::error_code ec;
        std::filesystem::create_directories(parentPath, ec);
    }
    
    while (!parentPath.empty()) {
        journal.createdDirectories.insert(parentPath);

        if (parentPath != targetDirectory) {
            parentPath = parentPath.parent_path();
        }
        else {
            parentPath = std::filesystem::path();
        }
    }

    std::ofstream outStream(targetPath, std::ios::binary);
    if (!outStream.is_open())
    {
        journal.lastResult = Journal::Result::FileCreationFailed;
        journal.lastErrorMessage = fmt::format("Failed to create file at {}.", fromPath(targetPath));
        return false;
    }

    journal.createdFiles.push_back(targetPath);

    outStream.write((const char *)(fileData.data()), fileData.size());
    if (outStream.bad())
    {
        journal.lastResult = Journal::Result::FileWriteFailed;
        journal.lastErrorMessage = fmt::format("Failed to create file at {}.", fromPath(targetPath));
        return false;
    }

    journal.progressCounter += fileData.size();
    
    if (!progressCallback())
    {
        journal.lastResult = Journal::Result::Cancelled;
        journal.lastErrorMessage = "Installation was cancelled.";
        return false;
    }

    return true;
}

static DLC detectDLC(const std::filesystem::path &sourcePath, VirtualFileSystem &sourceVfs, Journal &journal)
{
    std::string name;
    std::ifstream dlcFile(sourcePath);

    dlcFile.seekg(0x412, std::ios::beg);

    char ch;
    while (dlcFile.get(ch) && ch != '\0') {
        // If we're reading an invalid file, don't keep reading
        // past the maximum length of a valid DLC file name
        if (name.length() > 41) {
            break;
        }

        name += ch;
        // DLC file names have one character proceeded by null,
        // so we skip every other byte to read a continuous string
        dlcFile.seekg(1, std::ios::cur);
    }

    dlcFile.close();

    if (name == "Additional Episode \"Sonic Boss Attack\"") {
        return DLC::EpisodeSonic;
    } else if (name == "Additional Episode \"Shadow Boss Attack\"") {
        return DLC::EpisodeShadow;
    } else if (name == "Additional Episode \"Silver Boss Attack\"") {
        return DLC::EpisodeSilver;
    } else if (name == "Additional Episode \"Team Attack Amigo\"") {
        return DLC::EpisodeAmigo;
    } else if (name == "Additional Mission Pack \"Sonic/Very Hard") {
        return DLC::MissionSonic;
    } else if (name == "Additional Mission Pack \"Shadow/Very Har") {
        return DLC::MissionShadow;
    } else if (name == "Additional Mission Pack \"Silver/Very Har") {
        return DLC::MissionSilver;
    }

    journal.lastResult = Journal::Result::UnknownDLCType;
    journal.lastErrorMessage = fmt::format("DLC type for {} is unknown.", name);
    return DLC::Unknown;
}

bool Installer::checkGameInstall(const std::filesystem::path &baseDirectory, std::filesystem::path &modulePath)
{
    modulePath = baseDirectory / GameDirectory / GameExecutableFile;

    if (!std::filesystem::exists(modulePath))
        return false;

    return true;
}

bool Installer::checkDLCInstall(const std::filesystem::path &baseDirectory, DLC dlc)
{
    switch (dlc)
    {
    case DLC::EpisodeSonic:
        return std::filesystem::exists(baseDirectory / EpisodeSonicDirectory / DLCValidationFile);
    case DLC::EpisodeShadow:
        return std::filesystem::exists(baseDirectory / EpisodeShadowDirectory / DLCValidationFile);
    case DLC::EpisodeSilver:
        return std::filesystem::exists(baseDirectory / EpisodeSilverDirectory / DLCValidationFile);
    case DLC::EpisodeAmigo:
        return std::filesystem::exists(baseDirectory / EpisodeAmigoDirectory / DLCValidationFile);
    case DLC::MissionSonic:
        return std::filesystem::exists(baseDirectory / MissionSonicDirectory / DLCValidationFile);
    case DLC::MissionShadow:
        return std::filesystem::exists(baseDirectory / MissionShadowDirectory / DLCValidationFile);
    case DLC::MissionSilver:
        return std::filesystem::exists(baseDirectory / MissionSilverDirectory / DLCValidationFile);
    default:
        return false;
    }
}

bool Installer::checkAllDLC(const std::filesystem::path& baseDirectory)
{
    bool result = true;

    for (int i = 1; i < (int)DLC::Count; i++)
    {
        if (!checkDLCInstall(baseDirectory, (DLC)i))
            result = false;
    }

    return result;
}

bool Installer::computeTotalSize(std::span<const FilePair> filePairs, const uint64_t *fileHashes, VirtualFileSystem &sourceVfs, Journal &journal, uint64_t &totalSize)
{
    for (FilePair pair : filePairs)
    {
        const std::string filename(pair.first);
        if (!sourceVfs.exists(filename))
        {
            journal.lastResult = Journal::Result::FileMissing;
            journal.lastErrorMessage = fmt::format("File {} does not exist in {}.", filename, sourceVfs.getName());
            return false;
        }

        totalSize += sourceVfs.getSize(filename);
    }

    return true;
}

bool Installer::copyFiles(std::span<const FilePair> filePairs, const uint64_t *fileHashes, VirtualFileSystem &sourceVfs, const std::filesystem::path &targetDirectory, const std::string &validationFile, bool skipHashChecks, Journal &journal, const std::function<bool()> &progressCallback)
{
    std::error_code ec;
    if (!std::filesystem::exists(targetDirectory) && !std::filesystem::create_directories(targetDirectory, ec))
    {
        journal.lastResult = Journal::Result::DirectoryCreationFailed;
        journal.lastErrorMessage = "Unable to create directory at " + fromPath(targetDirectory);
        return false;
    }

    uint32_t hashIndex = 0;
    uint32_t hashCount = 0;
    std::vector<uint8_t> fileData;
    for (FilePair pair : filePairs)
    {
        hashIndex = hashCount;
        hashCount += pair.second;

        if (!copyFile(pair, &fileHashes[hashIndex], sourceVfs, targetDirectory, skipHashChecks, fileData, journal, progressCallback))
        {
            return false;
        }
    }

    return true;
}

bool Installer::parseContent(const std::filesystem::path &sourcePath, std::unique_ptr<VirtualFileSystem> &targetVfs, Journal &journal)
{
    targetVfs = createFileSystemFromPath(sourcePath);
    if (targetVfs != nullptr)
    {
        return true;
    }
    else
    {
        journal.lastResult = Journal::Result::VirtualFileSystemFailed;
        journal.lastErrorMessage = "Unable to open " + fromPath(sourcePath);
        return false;
    }
}

bool Installer::parseSources(const Input &input, Journal &journal, Sources &sources)
{
    journal = Journal();
    sources = Sources();

    // Parse the contents of the base game.
    if (!input.gameSource.empty())
    {
        if (!parseContent(input.gameSource, sources.game, journal))
        {
            return false;
        }

        if (!computeTotalSize({ GameFiles, GameFilesSize }, GameHashes, *sources.game, journal, sources.totalSize))
        {
            return false;
        }
    }

    // Parse the contents of the DLC Packs.
    for (const auto &path : input.dlcSources)
    {
        sources.dlc.emplace_back();
        DLCSource &dlcSource = sources.dlc.back();
        if (!parseContent(path, dlcSource.sourceVfs, journal))
        {
            return false;
        }

        DLC dlc = detectDLC(path, *dlcSource.sourceVfs, journal);
        switch (dlc)
        {
        case DLC::EpisodeSonic:
            dlcSource.filePairs = { EpisodeSonicFiles, EpisodeSonicFilesSize };
            dlcSource.fileHashes = EpisodeSonicHashes;
            dlcSource.targetSubDirectory = EpisodeSonicDirectory;
            break;
        case DLC::EpisodeShadow:
            dlcSource.filePairs = { EpisodeShadowFiles, EpisodeShadowFilesSize };
            dlcSource.fileHashes = EpisodeShadowHashes;
            dlcSource.targetSubDirectory = EpisodeShadowDirectory;
            break;
        case DLC::EpisodeSilver:
            dlcSource.filePairs = { EpisodeSilverFiles, EpisodeSilverFilesSize };
            dlcSource.fileHashes = EpisodeSilverHashes;
            dlcSource.targetSubDirectory = EpisodeSilverDirectory;
            break;
        case DLC::EpisodeAmigo:
            dlcSource.filePairs = { EpisodeAmigoFiles, EpisodeAmigoFilesSize };
            dlcSource.fileHashes = EpisodeAmigoHashes;
            dlcSource.targetSubDirectory = EpisodeAmigoDirectory;
            break;
        case DLC::MissionSonic:
            dlcSource.filePairs = { MissionSonicFiles, MissionSonicFilesSize };
            dlcSource.fileHashes = MissionSonicHashes;
            dlcSource.targetSubDirectory = MissionSonicDirectory;
            break;
        case DLC::MissionShadow:
            dlcSource.filePairs = { MissionShadowFiles, MissionShadowFilesSize };
            dlcSource.fileHashes = MissionShadowHashes;
            dlcSource.targetSubDirectory = MissionShadowDirectory;
            break;
        case DLC::MissionSilver:
            dlcSource.filePairs = { MissionSilverFiles, MissionSilverFilesSize };
            dlcSource.fileHashes = MissionSilverHashes;
            dlcSource.targetSubDirectory = MissionSilverDirectory;
            break;
        default:
            return false;
        }

        if (!computeTotalSize(dlcSource.filePairs, dlcSource.fileHashes, *dlcSource.sourceVfs, journal, sources.totalSize))
        {
            return false;
        }
    }

    // Add the total size in bytes as the journal progress.
    journal.progressTotal += sources.totalSize;

    return true;
}

bool Installer::install(const Sources &sources, const std::filesystem::path &targetDirectory, bool skipHashChecks, Journal &journal, std::chrono::seconds endWaitTime, const std::function<bool()> &progressCallback)
{
    // Install files in reverse order of importance. In case of a process crash or power outage, this will increase the likelihood of the installation
    // missing critical files required for the game to run. These files are used as the way to detect if the game is installed.

    // Install the DLC.
    if (!sources.dlc.empty())
    {
        journal.createdDirectories.insert(targetDirectory / DLCDirectory);
    }

    for (const DLCSource &dlcSource : sources.dlc)
    {
        if (!copyFiles(dlcSource.filePairs, dlcSource.fileHashes, *dlcSource.sourceVfs, targetDirectory / dlcSource.targetSubDirectory, DLCValidationFile, skipHashChecks, journal, progressCallback))
        {
            return false;
        }
    }

    // If no game was specified, we're finished. This means the user was only installing the DLC.
    if ((sources.game == nullptr))
    {
        return true;
    }

    // Install the base game.
    if (!copyFiles({ GameFiles, GameFilesSize }, GameHashes, *sources.game, targetDirectory / GameDirectory, GameExecutableFile, skipHashChecks, journal, progressCallback))
    {
        return false;
    }
    
    for (uint32_t i = 0; i < 2; i++)
    {
        if (!progressCallback())
        {
            journal.lastResult = Journal::Result::Cancelled;
            journal.lastErrorMessage = "Installation was cancelled.";
            return false;
        }

        if (i == 0)
        {
            // Wait the specified amount of time to allow the consumer of the callbacks to animate, halt or cancel the installation for a while after it's finished.
            std::this_thread::sleep_for(endWaitTime);
        }
    }

    return true;
}

void Installer::rollback(Journal &journal)
{
    std::error_code ec;
    for (const auto &path : journal.createdFiles)
    {
        std::filesystem::remove(path, ec);
    }

    for (auto it = journal.createdDirectories.rbegin(); it != journal.createdDirectories.rend(); it++)
    {
        std::filesystem::remove(*it, ec);
    }
}

bool Installer::parseGame(const std::filesystem::path &sourcePath)
{
    std::unique_ptr<VirtualFileSystem> sourceVfs = createFileSystemFromPath(sourcePath);
    if (sourceVfs == nullptr)
    {
        return false;
    }

    return sourceVfs->exists(GameExecutableFile);
}

DLC Installer::parseDLC(const std::filesystem::path &sourcePath)
{
    Journal journal;
    std::unique_ptr<VirtualFileSystem> sourceVfs = createFileSystemFromPath(sourcePath);
    if (sourceVfs == nullptr)
    {
        return DLC::Unknown;
    }

    return detectDLC(sourcePath, *sourceVfs, journal);
}
