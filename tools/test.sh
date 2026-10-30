#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -Ifirmware/v2m firmware/v2m/sonification.cpp tests/music_test.cpp \
  -o build/tests/music_test
build/tests/music_test
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -Ifirmware/v2m tests/controls_test.cpp -o build/tests/controls_test
build/tests/controls_test
