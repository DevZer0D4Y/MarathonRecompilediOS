# Keep original game files on the PC

The intended storage layout is:

| Device | Files |
| --- | --- |
| Windows PC | Original extracted Xbox game, exposed through a read-only share |
| MacBook | This source project, changes, native tools, generated code/shaders, Xcode build products |
| iPhone | Signed app, installed game data, settings, and saves |

The iPhone needs its own copy of the game data to play. The source game files remain on the PC. The build only needs to read `default.xex`, `shader.arc`, and `shader_lt.arc`; it does not bundle the full game data into the app.

## Read the PC folder from the MacBook

Share the extracted game folder on Windows with an authenticated account that has **Read** permission for the share. A name such as `MarathonSource` can be used. The folder must contain `default.xex` and `xenon/archives/shader.arc` plus `shader_lt.arc`. A folder containing all three files at its root is also supported.

On the MacBook, use Finder's **Go → Connect to Server** and enter `smb://YOUR_PC_HOSTNAME/MarathonSource`. Authenticate using the PC account. Keep the repository on the MacBook, for example under `~/Developer/MarathonRecomp`; do not put it inside the game share.

Check the actual mounted share path in Finder. From the local project, build with that path:

```bash
bash ios/build.sh --game-source "/Volumes/MarathonSource" \
  -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=YOUR_TEAM_ID \
  -DMARATHON_RECOMP_IOS_BUNDLE_ID=your.identifier.MarathonRecomp
```

The script checks the input files before building dependencies and passes the same source path to both the native-tool and iOS configurations. Original inputs are read in place. Generated shader extraction is under `out/generated/ios/shader/input`, not beside the archives on the share. The PC share must remain mounted during code generation and any subsequent build that regenerates code or shaders.

For manual CMake builds, pass `-DMARATHON_RECOMP_GAME_INPUT_DIR=/Volumes/MarathonSource` to both `ios-host-tools` and `ios-device`. See [IOS.md](IOS.md) for the full build and signing requirements.

## Install and transfer to the iPhone

Open the generated Xcode project on the MacBook, select the signing team and connected iPhone, and build/run the app. This installs the app; it does not transfer the complete Xbox game.

On the same network as the PC, the iPhone's Files app can connect to `smb://YOUR_PC_HOSTNAME` using **Browse → … → Connect to Server**. If that share is available in the app's Files picker, select the original extracted folder in Marathon Recompiled's installer. The installer reads the source and copies game data into the app's own writable Documents directory.

If the network folder cannot be selected or the provider cannot enumerate it through the picker, use Files to **copy** the extracted folder from the share into an `incoming` folder in Marathon Recompiled's exposed Documents location, then select that local folder in the installer. Copying keeps the originals on the PC. An already installed Marathon data directory can also be copied through Finder file sharing as described in [IOS.md](IOS.md).

Network-provider access, installation, rendering, and gameplay must be checked on the actual device. No successful transfer or device run is implied by these instructions.
