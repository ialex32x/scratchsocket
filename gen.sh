#/usr/bin/env sh

mkdir -p build/scratchsocket/linux
cd build/scratchsocket/linux
rm -rf *
cmake ../../../
cd ../../
cmake --build linux --config Debug
cd ..
