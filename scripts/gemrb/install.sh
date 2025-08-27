#!/bin/bash

# switch to gemrb directory
cd install/gemrb

# update gemrb
git pull

# update submodules
#git submodule update --init --recursive --remote

# rm -rf build
mkdir -p build

# switch to build directory
cd build

# build gemrb
# cmake -DCMAKE_BUILD_TYPE=Debug ..
# cmake -DCMAKE_BUILD_TYPE=Debug -DDISABLE_WERROR=1 ..
cmake ..

make -j$(nproc)
# ninja -j$(nproc)

sudo make install
# sudo cmake --install .