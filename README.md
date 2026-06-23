# BandPilot

BandPilot is a small cross-platform Qt Widgets starter app written in C++.

## Requirements

- CMake 3.16 or newer
- Qt 5 or Qt 6 with the Widgets module
- A C++17 compiler

## Build

```sh
cmake -S . -B build
cmake --build build
```

On Windows, run those commands from a Qt-enabled developer shell or pass `CMAKE_PREFIX_PATH` to your Qt installation, for example:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.9.0/mingw_64
cmake --build build
```

On macOS and Linux, install Qt through the Qt installer or your package manager, then use the same CMake commands.
