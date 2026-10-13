#include "document_picker.h"
#include <ui/game_window.h>
#include <SDL_syswm.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// Keep the security scopes alive while the installer reads the selected files.
static NSMutableArray<NSURL*>* g_scopedURLs;

@interface MarathonDocumentPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, copy) void (^completion)(NSArray<NSURL*>*);
@end

@implementation MarathonDocumentPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
    self.completion(urls);
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller
{
    self.completion(@[]);
}
@end

static MarathonDocumentPickerDelegate* g_delegate;

void ios::PickDocuments(bool folders, PickerCompletion completion)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        SDL_GetWindowWMInfo(GameWindow::s_pWindow, &info);
        UIWindow* window = (__bridge UIWindow*)info.info.uikit.window;
        UIViewController* presenter = window.rootViewController;
        while (presenter.presentedViewController) presenter = presenter.presentedViewController;
        if (!presenter)
        {
            completion({}, "The Files picker could not find an active window.");
            return;
        }

        UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc]
            initForOpeningContentTypes:@[folders ? UTTypeFolder : UTTypeItem] asCopy:NO];
        picker.allowsMultipleSelection = YES;
        g_delegate = [MarathonDocumentPickerDelegate new];
        g_delegate.completion = ^(NSArray<NSURL*>* urls) {
            if (!g_scopedURLs) g_scopedURLs = [NSMutableArray new];
            std::list<std::filesystem::path> paths;
            for (NSURL* url in urls)
            {
                if ([url startAccessingSecurityScopedResource]) [g_scopedURLs addObject:url];
                paths.emplace_back(url.fileSystemRepresentation);
            }
            completion(std::move(paths), {});
            g_delegate = nil;
        };
        picker.delegate = g_delegate;
        [presenter presentViewController:picker animated:YES completion:nil];
    });
}

void ios::ReleasePickedDocuments()
{
    for (NSURL* url in g_scopedURLs) [url stopAccessingSecurityScopedResource];
    g_scopedURLs = nil;
}
