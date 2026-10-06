# FlowTools

FlowTools is a Windows command-line toolkit inspired by the Unix philosophy.

It provides small, independent CLI tools together with `flow`, the FlowTools package manager.

## Installation

The recommended installation method is PowerShell.

Open PowerShell and run:

```powershell
irm "https://raw.githubusercontent.com/filyx0/FlowTools/main/installer/install.ps1?cache=$(Get-Date -Format 'yyyyMMddHHmmss')" | iex
```

The installer:

1. Finds the latest FlowTools GitHub Release.
2. Downloads `flow.exe`.
3. Downloads `core.dll`.
4. Downloads `data.json`.
5. Installs them into `C:\FlowTools`.
6. Adds `C:\FlowTools\bin` to the user `PATH`.
7. Starts `flow.exe`.

After installation, open a new PowerShell window and run:

```powershell
flow
```

## Installation directory

FlowTools is installed into:

```text
C:\FlowTools\
├── bin\
│   ├── flow.exe
│   ├── core.dll
│   ├── fpath.exe
│   ├── fsearch.exe
│   ├── fproc.exe
│   ├── fsize.exe
│   ├── fenv.exe
│   ├── fnet.exe
│   └── fport.exe
├── data.json
└── versions.json
```

`bin` is added to the user `PATH`, allowing FlowTools commands to be used directly from PowerShell or Command Prompt.

## Flow package manager

`flow` is the FlowTools package manager.

Run it without arguments to open the interactive interface:

```powershell
flow
```

The interface can be controlled with:

```text
[UP/DOWN] Navigate
[ENTER]   Select
[ESC]     Back
```

## Commands

### Install a tool

```powershell
flow install fsearch
```

Flow downloads the required files, verifies them and installs them into `C:\FlowTools\bin`.

Example:

```text
FlowTools 0.1.0

-> Checking latest release...
[OK] Release v0.1.0 found

-> Installing fsearch 0.1.0
  Downloading [####################----------] 72% 1.30 MB / 1.80 MB
  [OK] Download complete
  Verifying SHA-256...
  [OK] SHA-256 verified
  Installing fsearch.exe...
  [OK] Installed fsearch 0.1.0
```

### Update a tool

```powershell
flow update fsearch
```

### Update everything

```powershell
flow update
```

### Update Flow and Core

```powershell
flow update --flow
```

### Update libraries

```powershell
flow update --libs
```

### List packages

```powershell
flow list
```

### Show package information

```powershell
flow info fsearch
```

### Uninstall a tool

```powershell
flow uninstall fsearch
```

### Show Flow version

```powershell
flow --version
```

### Show help

```powershell
flow --help
```

## Available tools

| Tool      | Description                                   | Version |
| --------- | --------------------------------------------- | ------: |
| `fpath`   | Inspect and manage the Windows PATH           |   0.1.0 |
| `fsearch` | Search files and directories                  |   0.1.0 |
| `fproc`   | Inspect and manage processes                  |   0.1.0 |
| `fsize`   | Calculate file and directory sizes            |   0.1.0 |
| `fenv`    | Inspect environment variables                 |   0.1.0 |
| `fnet`    | Inspect network interfaces and IP information |   0.1.0 |
| `fport`   | Inspect TCP ports and connections             |   0.1.0 |

## Direct tool usage

Every FlowTools utility can also be executed directly.

Examples:

```powershell
fpath
```

```powershell
fsearch "example"
```

```powershell
fproc list
```

```powershell
fsize .
```

```powershell
fenv
```

```powershell
fnet interfaces
```

```powershell
fport
```

## Releases

FlowTools uses GitHub Releases as its distribution backend.

Each release contains:

```text
data.json
flow.exe
core.dll
fpath.exe
fsearch.exe
fproc.exe
fsize.exe
fenv.exe
fnet.exe
fport.exe
```

The release manifest is stored in `data.json`.

It contains package names, versions, installation paths, file sizes and SHA-256 hashes.

## Security

Downloaded files are verified before installation when a SHA-256 hash is available.

FlowTools downloads files from the official GitHub Release associated with the project.

The installer does not require administrator privileges because it installs into:

```text
C:\FlowTools
```

If Windows permissions prevent installation into this directory, run PowerShell with the required permissions.

## Updating FlowTools

FlowTools is designed to update itself through:

```powershell
flow update --flow
```

Individual tools can be updated with:

```powershell
flow update <tool>
```

All installed tools can be updated with:

```powershell
flow update
```

## Building from source

Requirements:

* Windows
* Visual Studio with C++20 support
* CMake 3.20 or newer
* Git

Clone the repository:

```powershell
git clone https://github.com/filyx0/FlowTools.git
cd FlowTools
```

Build the project:

```powershell
.\clean-build.ps1
```

The Release binaries are generated in:

```text
build\Release\
```

## Project structure

```text
FlowTools/
├── core/
│   ├── include/
│   └── src/
├── tools/
│   ├── fpath/
│   ├── fsearch/
│   ├── fproc/
│   ├── fsize/
│   ├── fenv/
│   ├── fnet/
│   └── fport/
├── flow/
│   └── src/
├── installer/
│   └── install.ps1
├── data.json
├── CMakeLists.txt
├── README.md
└── LICENSE
```

## License

See `LICENSE` for the project license.