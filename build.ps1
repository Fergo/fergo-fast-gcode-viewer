param([switch]$Debug, [string]$OutputDirectory = 'dist')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$compiler = Join-Path $PSScriptRoot '.tools\w64devkit\bin\g++.exe'
if (!(Test-Path $compiler)) { throw 'Compiler not found. See README.md for CMake or portable toolchain setup.' }
$sdl = Join-Path $PSScriptRoot 'vendor\SDL3-3.4.18\x86_64-w64-mingw32'
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$appPath = Join-Path $OutputDirectory 'fergo-gcode-viewer.exe'
$testPath = Join-Path $OutputDirectory 'parser-tests.exe'
$resourcePath = Join-Path $OutputDirectory 'app-resource.o'
& ./.tools/w64devkit/bin/windres.exe -I . -I src src/app.rc -O coff -o $resourcePath
if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed' }
$optimization = if ($Debug) { '-O0' } else { '-O3' }
& $compiler -std=c++17 $optimization -g -Wall -Wextra -Wpedantic -mwindows -static-libgcc -static-libstdc++ "-I$sdl\include" src/main.cpp src/gcode.cpp $resourcePath "-L$sdl\lib" -lSDL3 -lopengl32 -o $appPath
if ($LASTEXITCODE -ne 0) { throw 'Application build failed' }
& $compiler -std=c++17 -O2 -Wall -Wextra -Wpedantic -static-libgcc -static-libstdc++ -Isrc tests/parser_tests.cpp src/gcode.cpp -o $testPath
if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
$colorsTestPath = Join-Path $OutputDirectory 'colors-tests.exe'
& $compiler -std=c++17 -O2 -Wall -Wextra -Wpedantic -static-libgcc -static-libstdc++ -Isrc tests/colors_tests.cpp -o $colorsTestPath
if ($LASTEXITCODE -ne 0) { throw 'Color test build failed' }
$colorsPath = Join-Path $OutputDirectory 'colors.txt'
if (!(Test-Path $colorsPath)) { Copy-Item colors.txt $colorsPath }
Copy-Item "$sdl\bin\SDL3.dll" $OutputDirectory -Force
Copy-Item vendor/SDL3-3.4.18/LICENSE.txt (Join-Path $OutputDirectory 'SDL-LICENSE.txt') -Force
& (Resolve-Path $testPath)
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
& (Resolve-Path $colorsTestPath)
if ($LASTEXITCODE -ne 0) { throw 'Color tests failed' }
if (!$Debug) { & ./.tools/w64devkit/bin/strip.exe $appPath }
Write-Host "Build completed: $appPath"
