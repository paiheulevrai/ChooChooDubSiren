$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$compiler = 'C:\msys64\ucrt64\bin\g++.exe'
$runtime = 'C:\msys64\ucrt64\bin'
$include = 'C:\msys64\ucrt64\include'
$lib = 'C:\msys64\ucrt64\lib'
$out = Join-Path $root 'build\windows-release'
New-Item -ItemType Directory -Force $out | Out-Null
$env:PATH = "$runtime;C:\msys64\usr\bin;$env:PATH"
Push-Location $root
try {
  & $compiler -std=c++17 -Wall -Wextra -O2 "-I$include" `
    'src\main.cpp' 'src\siren.cpp' 'src\font12x16.cpp' "-L$lib" -lSDL2 `
    -static-libgcc -static-libstdc++ -o (Join-Path $out 'DubSiren.exe')
  if ($LASTEXITCODE -ne 0) { throw 'Windows compilation failed' }
  Copy-Item (Join-Path $runtime 'SDL2.dll') $out
  Copy-Item (Join-Path $runtime 'libwinpthread-1.dll') $out
  Copy-Item 'README.md', 'LICENSE.tracker' $out
  New-Item -ItemType Directory -Force (Join-Path $out 'docs') | Out-Null
  Copy-Item 'docs\*.png' (Join-Path $out 'docs')
  Write-Host "Created $(Join-Path $out 'DubSiren.exe')"
} finally {
  Pop-Location
}
