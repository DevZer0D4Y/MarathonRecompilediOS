# Additional sources in the iOS port

- Touch controls and haptics: DevZer0D4Y/UnleashedRecompilediOS, commit d42b6de6b77e7abdccd2674ec13fbdef2e0bd4bc, GPL-3.0. The source layout identifies its XeniOS/Xenia origins; those attribution comments are preserved. Upstream license: https://github.com/DevZer0D4Y/UnleashedRecompilediOS/blob/d42b6de6b77e7abdccd2674ec13fbdef2e0bd4bc/COPYING
- Native mod manager UI, portable import/selection backend and baseline tests: adapted from DevZer0D4Y/UnleashedRecompilediOS, commit 5625afe81d2f9f2bac947087034260dc8c87dcc1 (GPL-3.0). Marathon adds its own Sonic '06 filesystem overlay and compatibility checks. https://github.com/DevZer0D4Y/UnleashedRecompilediOS/blob/5625afe81d2f9f2bac947087034260dc8c87dcc1/COPYING
- BC decoder: Sergii "iOrange" Kudlai, bcdec commit 93628fe5627102fe5187b7eeb99122dec6612c36. `bcdec.h` retains its full license text and credits. https://github.com/iOrange/bcdec
- XMA decoder patch: extracted unchanged from the pinned sonicnext-dev/ffmpeg-core submodule's `ffmpeg.patch`. It is applied to FFmpeg 7.1.1; FFmpeg's source and license notices remain in the build source directory.
- Plume and XenosRecomp changes are stored as patches against the pinned submodules. Their existing copyright and license notices remain intact.
