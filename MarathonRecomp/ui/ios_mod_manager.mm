#include "ios_mod_manager.h"
#include <algorithm>
#include <exception>
#include <set>
#include <unordered_map>
#include <mod/ios_mod_manager.h>
#include <mod/mod_loader.h>
#include <user/paths.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface RecompModManager : UITableViewController <UIDocumentPickerDelegate>
{
    std::vector<ios_mod_manager::Entry> _mods;
    std::vector<ModLoader::Code> _codes;
    std::vector<ios_mod_manager::patch::Info> _patches;
    std::set<std::string> _enabledPatches;
    bool _scanFailed;
}
@property(nonatomic, copy) void (^completion)(void);
@end

// Table sections, top to bottom.
enum : NSInteger { ModsSection, PatchesSection, CodesSection, SectionCount };

@implementation RecompModManager
- (void)viewDidLoad
{
    [super viewDidLoad];
    self.title = @"Mods — top wins conflicts";
    self.tableView.allowsSelectionDuringEditing = YES;
    self.tableView.editing = YES;
    // Rows grow to fit why a mod can't be enabled, instead of spilling into the next row.
    self.tableView.rowHeight = UITableViewAutomaticDimension;
    self.tableView.estimatedRowHeight = 76;
    _codes = ModLoader::GetBuiltInCodes();
    std::vector<std::string> codeNames;
    for (const auto& code : _codes) codeNames.push_back(code.name);
    ios_mod_manager::SetBuiltInCodes(std::move(codeNames));
    self.navigationItem.leftBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Import folder" style:UIBarButtonItemStylePlain target:self action:@selector(importFolder)];
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"Play" style:UIBarButtonItemStyleDone target:self action:@selector(play)];
    UILabel* footer = [[UILabel alloc] initWithFrame:CGRectMake(0, 0, 600, 90)];
    footer.numberOfLines = 0;
    footer.textAlignment = NSTextAlignmentCenter;
    footer.text = @"Tap to enable or disable; drag to reorder mods. Import an extracted mod folder containing mod.ini, or a folder of Sonic '06 Mod Manager .mlua patches. Mods and patches that change the game's code or load Xbox DLLs (such as Khronos) need ports.\nChanges apply when you tap Play.";
    footer.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
    self.tableView.tableFooterView = footer;
    _enabledPatches = ios_mod_manager::patch::LoadSelection(GetUserPath());
    [self reloadMods];
}
- (void)showMessage:(NSString*)message
{
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Mod manager" message:message preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}
- (void)showError:(NSString*)message
{
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"Mod manager" message:message preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}
- (void)reloadMods
{
    try { _mods = ios_mod_manager::Scan(GetUserPath()); _scanFailed = false; }
    catch (const std::exception& ex) { _scanFailed = true; [self showError:[NSString stringWithUTF8String:ex.what()]]; }
    // Patches that can run come first; the rest stay listed with the reason.
    _patches = ios_mod_manager::patch::Scan(GetUserPath());
    std::stable_partition(_patches.begin(), _patches.end(), [](const auto& patch) { return patch.issue.empty(); });
    [self.tableView reloadData];
}
- (bool)isPatchEnabled:(const ios_mod_manager::patch::Info&)patch
{
    return patch.issue.empty() && _enabledPatches.contains(patch.file.filename().string());
}
- (NSInteger)numberOfSectionsInTableView:(UITableView*)tableView { return SectionCount; }
- (NSString*)tableView:(UITableView*)tableView titleForHeaderInSection:(NSInteger)section
{
    if (section == ModsSection) return @"Installed mods";
    if (section == PatchesSection) return @"Patches (.mlua)";
    return @"Built-in codes";
}
- (NSString*)tableView:(UITableView*)tableView titleForFooterInSection:(NSInteger)section
{
    if (section != PatchesSection) return nil;
    return @"Import the extracted Sonic-06-Mod-Manager-Patches folder (or any folder of .mlua patches) with Import folder. Patches that edit archives, packages or game data run here; those that rewrite game code or edit compiled Lua scripts can't, and show why.";
}
- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section
{
    if (section == ModsSection) return _mods.size();
    if (section == PatchesSection) return _patches.size();
    return _codes.size();
}
- (UITableViewCell*)tableView:(UITableView*)tableView cellForRowAtIndexPath:(NSIndexPath*)indexPath
{
    UITableViewCell* cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    if (indexPath.section == PatchesSection)
    {
        const auto& patch = _patches[indexPath.row];
        const bool enabled = [self isPatchEnabled:patch];
        cell.textLabel.text = [NSString stringWithUTF8String:patch.title.c_str()];
        cell.textLabel.numberOfLines = 0;
        const auto subtitle = patch.issue.empty() ? (enabled ? "Enabled • " : "Disabled • ") + patch.blurb : patch.issue;
        cell.detailTextLabel.text = [NSString stringWithUTF8String:subtitle.c_str()];
        cell.detailTextLabel.numberOfLines = 0;
        cell.accessoryType = enabled ? UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone;
        cell.textLabel.textColor = patch.issue.empty() ? UIColor.labelColor : UIColor.secondaryLabelColor;
        return cell;
    }
    if (indexPath.section == CodesSection)
    {
        const auto& code = _codes[indexPath.row];
        NSString* title = [NSString stringWithUTF8String:code.name.c_str()];
        title = [title stringByReplacingOccurrencesOfString:@"([A-Z]+)([A-Z][a-z])" withString:@"$1 $2" options:NSRegularExpressionSearch range:NSMakeRange(0, title.length)];
        title = [title stringByReplacingOccurrencesOfString:@"([a-z])([A-Z])" withString:@"$1 $2" options:NSRegularExpressionSearch range:NSMakeRange(0, title.length)];
        cell.textLabel.text = title;
        cell.textLabel.numberOfLines = 2;
        cell.detailTextLabel.text = code.enabled ? @"Enabled" : @"Disabled";
        cell.accessoryType = code.enabled ? UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone;
        return cell;
    }
    const auto& mod = _mods[indexPath.row];
    cell.textLabel.text = [NSString stringWithUTF8String:mod.title.c_str()];
    cell.textLabel.numberOfLines = 0;
    const auto subtitle = mod.issue.empty() ? (mod.enabled ? "Enabled • " : "Disabled • ") + mod.author : mod.issue;
    cell.detailTextLabel.text = [NSString stringWithUTF8String:subtitle.c_str()];
    cell.detailTextLabel.numberOfLines = 0;
    cell.accessoryType = mod.enabled ? UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone;
    cell.textLabel.textColor = mod.issue.empty() ? UIColor.labelColor : UIColor.secondaryLabelColor;
    return cell;
}
- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath
{
    if (indexPath.section == PatchesSection)
    {
        const auto& patch = _patches[indexPath.row];
        const auto name = patch.file.filename().string();
        if (!patch.issue.empty()) [self showError:[NSString stringWithUTF8String:patch.issue.c_str()]];
        else if (_enabledPatches.contains(name)) _enabledPatches.erase(name);
        else _enabledPatches.insert(name);
        [tableView deselectRowAtIndexPath:indexPath animated:YES];
        [tableView reloadData];
        return;
    }
    if (indexPath.section == CodesSection)
    {
        _codes[indexPath.row].enabled = !_codes[indexPath.row].enabled;
        [tableView deselectRowAtIndexPath:indexPath animated:YES];
        [tableView reloadData];
        return;
    }
    auto& mod = _mods[indexPath.row];
    if (mod.issue.empty()) mod.enabled = !mod.enabled;
    else [self showError:[NSString stringWithUTF8String:mod.issue.c_str()]];
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    [tableView reloadData];
}
- (BOOL)tableView:(UITableView*)tableView canMoveRowAtIndexPath:(NSIndexPath*)indexPath { return indexPath.section == ModsSection; }
- (NSIndexPath*)tableView:(UITableView*)tableView targetIndexPathForMoveFromRowAtIndexPath:(NSIndexPath*)source toProposedIndexPath:(NSIndexPath*)destination
{
    return destination.section == ModsSection ? destination : source;
}
- (UITableViewCellEditingStyle)tableView:(UITableView*)tableView editingStyleForRowAtIndexPath:(NSIndexPath*)indexPath { return UITableViewCellEditingStyleNone; }
- (BOOL)tableView:(UITableView*)tableView shouldIndentWhileEditingRowAtIndexPath:(NSIndexPath*)indexPath { return NO; }
- (void)tableView:(UITableView*)tableView moveRowAtIndexPath:(NSIndexPath*)source toIndexPath:(NSIndexPath*)destination
{
    auto entry = std::move(_mods[source.row]);
    _mods.erase(_mods.begin() + source.row);
    _mods.insert(_mods.begin() + destination.row, std::move(entry));
}
- (BOOL)saveMods
{
    if (_scanFailed) { [self showError:@"The mods folder could not be scanned. Fix its permissions using Files and relaunch."]; return NO; }
    std::string error;
    std::set<std::string> patches;
    for (const auto& patch : _patches)
        if ([self isPatchEnabled:patch]) patches.insert(patch.file.filename().string());
    if (ios_mod_manager::Save(GetUserPath(), _mods, error) && ios_mod_manager::patch::SaveSelection(GetUserPath(), patches, error))
    {
        // Switch on the built-in codes that enabled mods declare as required patches.
        for (const auto& mod : _mods)
        {
            if (!mod.enabled || !mod.issue.empty()) continue;
            for (const auto& name : ios_mod_manager::RequiredCodes(mod.iniPath))
                for (auto& code : _codes)
                    if (code.name == name) code.enabled = true;
        }
        ModLoader::SaveBuiltInCodes(_codes);
        [self.tableView reloadData];
        return YES;
    }
    [self showError:[NSString stringWithUTF8String:error.c_str()]];
    return NO;
}
- (void)play
{
    if (![self saveMods]) return;
    self.tableView.userInteractionEnabled = NO;
    self.title = @"Preparing mods…";
    self.navigationItem.leftBarButtonItem.enabled = NO;
    self.navigationItem.rightBarButtonItem.enabled = NO;
    // Archive preparation can involve large files. Keep UIKit responsive and
    // leave the manager open so a failed patch can be disabled and retried.
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSString* error = nil;
        try { ModLoader::Init(); }
        catch (const std::exception& ex) { error = [NSString stringWithUTF8String:ex.what()]; }
        dispatch_async(dispatch_get_main_queue(), ^{
            self.tableView.userInteractionEnabled = YES;
            self.title = @"Mods — top wins conflicts";
            self.navigationItem.leftBarButtonItem.enabled = YES;
            self.navigationItem.rightBarButtonItem.enabled = YES;
            if (error) { [self showError:error]; return; }
            [self.navigationController dismissViewControllerAnimated:YES completion:^{ if (self.completion) self.completion(); }];
        });
    });
}
- (void)importFolder
{
    if (![self saveMods]) return;
    UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeFolder] asCopy:NO];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
    NSURL* url = urls.firstObject;
    if (!url || !url.isFileURL) return;
    BOOL scoped = [url startAccessingSecurityScopedResource];
    self.tableView.userInteractionEnabled = NO;
    self.title = @"Importing…";
    self.navigationItem.leftBarButtonItem.enabled = NO;
    self.navigationItem.rightBarButtonItem.enabled = NO;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        std::string error;
        const std::filesystem::path folder(url.fileSystemRepresentation);
        // A folder with mod.ini is a mod; otherwise import the .mlua patches it contains.
        std::error_code ec;
        bool success;
        std::string notice;
        if (std::filesystem::is_regular_file(folder / "mod.ini", ec))
            success = ios_mod_manager::Import(GetUserPath(), folder, error);
        else
        {
            const auto count = ios_mod_manager::patch::Import(GetUserPath(), folder, error);
            success = count > 0;
            if (success) notice = "Imported " + std::to_string(count) + (count == 1 ? " patch." : " patches.");
            else if (error == "The folder contains no .mlua patches.")
                error = "The folder has no mod.ini and no .mlua patches. Select the folder that contains mod.ini, or a folder of patches.";
        }
        if (scoped) [url stopAccessingSecurityScopedResource];
        NSString* message = [NSString stringWithUTF8String:error.c_str()];
        NSString* info = notice.empty() ? nil : [NSString stringWithUTF8String:notice.c_str()];
        dispatch_async(dispatch_get_main_queue(), ^{
            self.tableView.userInteractionEnabled = YES;
            self.title = @"Mods — top wins conflicts";
            self.navigationItem.leftBarButtonItem.enabled = YES;
            self.navigationItem.rightBarButtonItem.enabled = YES;
            [self reloadMods];
            if (!success) [self showError:message];
            else if (info) [self showMessage:info];
        });
    });
}
@end

namespace ios_mod_ui
{
    bool Run()
    {
        __block bool finished = false;
        __block bool presented = false;
        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        auto present = ^{
            UIWindow* window = nil;
            for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
            {
                if (![scene isKindOfClass:UIWindowScene.class] || scene.activationState != UISceneActivationStateForegroundActive) continue;
                for (UIWindow* candidate in ((UIWindowScene*)scene).windows)
                    if (candidate.isKeyWindow) window = candidate;
            }
            // Also support the delegate window while the SDL scene is attaching.
            if (!window && [UIApplication.sharedApplication.delegate respondsToSelector:@selector(window)])
                window = UIApplication.sharedApplication.delegate.window;
            UIViewController* presenter = window.rootViewController;
            while (presenter.presentedViewController) presenter = presenter.presentedViewController;
            if (!presenter) { finished = true; dispatch_semaphore_signal(semaphore); return; }
            RecompModManager* manager = [[RecompModManager alloc] initWithStyle:UITableViewStyleInsetGrouped];
            manager.completion = ^{ finished = true; dispatch_semaphore_signal(semaphore); };
            UINavigationController* navigation = [[UINavigationController alloc] initWithRootViewController:manager];
            navigation.modalPresentationStyle = UIModalPresentationFullScreen;
            navigation.modalInPresentation = YES;
            [presenter presentViewController:navigation animated:NO completion:nil];
            presented = true;
        };
        if (NSThread.isMainThread)
        {
            present();
            while (!finished)
                [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
        else
        {
            dispatch_sync(dispatch_get_main_queue(), present);
            dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
        }
        return presented;
    }
}
