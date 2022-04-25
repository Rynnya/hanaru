@echo off

for /f "tokens=*" %%a in ('where vcpkg.exe') do set VCPKG_ROOT=%%~dpa

if "%VCPKG_ROOT%"=="" (
    echo === UNABLE TO FIND vcpkg.exe ===
    echo Please make sure that you have installed vcpkg.exe, run `vcpkg integrate install` and placed the vcpkg root into your PATH
    exit
)

echo === VCPKG_ROOT is %VCPKG_ROOT% ===

REM install 64bit packages that we need
vcpkg install --triplet x64-windows "drogon"

pushd "%~dp0"
    mkdir build
    pushd build
        cmake -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_GENERATOR_PLATFORM=x64 -DCMAKE_EXPORT_COMPILE_COMMANDS=true ..
    popd
popd