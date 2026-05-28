# Sightline Visualizer

A lightweight streaming front-end for Windows, compatible from **Windows XP x86 to Windows 11**.
Written in pure C++. No Electron, no heavy runtimes.

## Features
- Search YouTube via InnerTube API
- Stream playback via VLClib
- Minimal RAM footprint — tested on Atom N450 / 2GB RAM / GMA 3150 running XP86

## Requirements
- Windows XP SP3 or later
- Direct3D 9 capable GPU
- a computer lol

## Building
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Requires Visual Studio 2019 with **v141_xp toolset** or MinGW-w64.

## License
MIT
