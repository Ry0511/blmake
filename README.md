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

### Additional Parameters

| Argument      | Description                                                                                                                                                      |
|---------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| NoHomeDir     | Tells the game to search relatively from the `Borderlands.exe` instead of from the `my games/Borderlands` directory                                              |
| NoPause       | The popup terminal does not stay open and immediately closes when done                                                                                           |
| ForceLogFlush | Forces the log file to flush on write so that results can be seen immediately                                                                                    |
| Debug         | Compiles the unreal script in debug mode                                                                                                                         |
| Release       | Compiles the unreal script in release mode                                                                                                                       |
| NoCompress    | non-standard option that we add to unset the PKG_StoreCompressed flag on a package. By default all shipped script packages will be compressed this disables that |
