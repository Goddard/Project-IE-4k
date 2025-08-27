#!/bin/bash
BUILD_DIR="build"

for file in $(find ../ -name '*.cpp'); do
    echo "Analyzing $file"
    include-what-you-use -Xiwyu --mapping_file=/usr/share/include-what-you-use/iwyu.imp \
        -p $BUILD_DIR "$file" 2>&1 | tee iwyu_output.log
done