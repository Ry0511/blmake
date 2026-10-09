# Borderlands Make

Patches the Make commandlet so that it no longer terminates early due to the cooked script packages.
This allows you to create custom packages deriving from the original script packages and with a bit
of effort recompile the original script packages.

> This has only been tested on Borderlands 1.4.1 UDK version

## Installation

1. Grab the latest version of the [plugin loader](https://github.com/bl-sdk/pluginloader/releases)
2. Grab the latest version of [Borderlands Make](https://github.com/Ry0511/blmake/releases)
3. Extract the contents of blmake*.zip into the plugins directory

With that done, the patcher will now run whenever you execute the make commandlet. You can check the
blmake.log file if you encounter any issues.

### Compiling Unreal Script

The current workflow uses unreal engines mod packages system which requires a little setup. The
following is a minimal example of getting a custom script package to compile.

> All paths are relative to the root game directory which is the folder containing Binaries and
> WillowGame

The following directory structure is required:

```text
- Borderlands
  - WillowGame
    - Src
      - CustomScript
        - Classes
          - SomeClass.uc
    - Script
      - CustomScript.upk <- generated from make commandlet
```

Once you have that setup you will need to do one more thing. Inside
`WillowGame/Config/WillowEditor.ini` create an entry for the mod package. For the above you should
see this:

```ini
[ModPackages]
ModPackagesInPath = ..\WillowGame\Src
ModOutputDir = ..\WillowGame\Script
ModPackages = CustomScript
```

Once you have all the above in place you can start the make commandlet with the command:

```ps1
./Borderlands.exe Make -NoHomeDir -NoPause -ForceLogFlush -NoCompress -Debug
```

Minimally all you need is

```ps1 
./Borderlands.exe Make -NoHomeDir
```

> You can check the `WillowGame/Logs/UCC.log` file for the compilation log

> You may need to compile against the steam version to ship on steam – depends on how native
> functions are handled, they are almost certainly not compatible between 141 and steam. I have not
> tested compiling against the steam version yet.

### Additional Parameters

| Argument      | Description                                                                                                                                                      |
|---------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| NoHomeDir     | Tells the game to search relatively from the `Borderlands.exe` instead of from the `my games/Borderlands` directory                                              |
| NoPause       | The popup terminal does not stay open and immediately closes when done                                                                                           |
| ForceLogFlush | Forces the log file to flush on write so that results can be seen immediately                                                                                    |
| Debug         | Compiles the unreal script in debug mode                                                                                                                         |
| Release       | Compiles the unreal script in release mode                                                                                                                       |
| NoCompress    | non-standard option that we add to unset the PKG_StoreCompressed flag on a package. By default all shipped script packages will be compressed this disables that |

> I eyeballed the descriptions here btw probably 50% correct

### Loading Script Packages

Once you have the `foo.u` package you might be wondering how you load it into the game. Which there
is two load paths that are relevant since the editor can use these packages i.e., custom actors
which can be placed into a map.

The editor will load it already since you added the `ModPackages=MayaPhaselock` entry to the config
file. That is all that is required for the editor to load it; in fact, base game probably also does
that. But telling users to mess with config files always ends badly so I suggest using an sdk mod
instead to load the script package. This snippet can be used to load a script package:

```py
from pathlib import Path
from unrealsdk import find_all, load_package, ObjectFlags


def load_script_package(pkg: str | Path) -> None:
    # this accepts an absolute path to a file so you can either have users install the package 
    # separately or you manually load it from your mod. When packaging into an sdkmod you'll first 
    # need to extract it and then load it. Ideally, the drag, drop, done approach should be taken. 
    pkg = load_package(str(pkg))

    for cls in find_all("Class"):
        if cls.Outer == pkg:
            cls.ObjectFlags |= ObjectFlags.KEEP_ALIVE
```

You're free to do whatever you want with the above. Maybe create a library that auto-loads script
packages from sdk_mods/script_packages/ ?