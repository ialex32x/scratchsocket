@echo off

pushd build
pushd scratchsocket 
cmake --build x64 --config Debug
popd
popd
