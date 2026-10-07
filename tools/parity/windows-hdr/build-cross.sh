#!/usr/bin/env bash
# Compile only with already installed tools. Never invokes Wine or the probe.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
python3 build_manifest.py begin build --recipe cross
{
  clang --version
  lld-link --version
  clang --target=x86_64-pc-windows-msvc -std=c11 -O1 -Wall -Wextra -Werror \
    -Wno-unknown-pragmas -Wno-unused-parameter -DPROBE_CROSS_ENTRY \
    -I/usr/include/wine/windows -I/usr/include/wine/msvcrt \
    -c mediaengine_readback.c -o build/mediaengine_readback.obj
  lld-link /entry:mainCRTStartup /subsystem:console /machine:x64 \
    /out:build/mediaengine_readback.exe build/mediaengine_readback.obj \
    /libpath:/usr/lib/wine/x86_64-windows \
    libkernel32.a libuser32.a libshell32.a libole32.a liboleaut32.a \
    libd3d11.a libdxgi.a libdxguid.a libmfplat.a libmfuuid.a libuuid.a \
    libucrtbase.a libntdll.a
  llvm-readobj --coff-imports build/mediaengine_readback.exe > build/imports.txt
  if rg -q '__chkstk' build/imports.txt; then
    echo 'Unexpected cross-CRT stack helper import; inspect before use.' >&2
    exit 1
  fi
} > build/build.log 2>&1
python3 build_manifest.py finish build --recipe cross
