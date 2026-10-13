param([switch]$Debug, [string]$OutputDirectory = 'dist')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$compiler = Join-Path $PSScriptRoot '.tools\w64devkit\bin\g++.exe'
if (!(Test-Path $compiler)) { throw 'Compilador ausente. Veja README.md para compilar com CMake ou preparar o toolchain.' }
$sdl = Join-Path $PSScriptRoot 'vendor\SDL3-3.4.18\x86_64-w64-mingw32'
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$appPath = Join-Path $OutputDirectory 'gcode-viewer.exe'
$testPath = Join-Path $OutputDirectory 'parser-tests.exe'
$optimization = if ($Debug) { '-O0' } else { '-O3' }
& $compiler -std=c++17 $optimization -g -Wall -Wextra -Wpedantic -mwindows -static-libgcc -static-libstdc++ "-I$sdl\include" src/main.cpp src/gcode.cpp "-L$sdl\lib" -lSDL3 -lopengl32 -o $appPath
if ($LASTEXITCODE -ne 0) { throw 'Falha ao compilar aplicativo' }
& $compiler -std=c++17 -O2 -Wall -Wextra -Wpedantic -static-libgcc -static-libstdc++ -Isrc tests/parser_tests.cpp src/gcode.cpp -o $testPath
if ($LASTEXITCODE -ne 0) { throw 'Falha ao compilar testes' }
Copy-Item "$sdl\bin\SDL3.dll" $OutputDirectory -Force
Copy-Item vendor/SDL3-3.4.18/LICENSE.txt (Join-Path $OutputDirectory 'SDL-LICENSE.txt') -Force
& (Resolve-Path $testPath)
if ($LASTEXITCODE -ne 0) { throw 'Testes falharam' }
if (!$Debug) { & ./.tools/w64devkit/bin/strip.exe $appPath }
Write-Host "Build concluido: $appPath"
