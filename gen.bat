@echo off

mkdir build
pushd build

mkdir scratchsocket
pushd scratchsocket 
rd /s /q x64
mkdir x64
pushd x64
cmake -G "Visual Studio 15 2017 Win64" ..\..\..\
popd
REM cmake --build x64 --config Debug
popd
popd
