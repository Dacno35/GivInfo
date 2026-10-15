#!/usr/bin/env sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir="${TMPDIR:-/tmp}/esp32-givenergy-native-test"
mkdir -p "$build_dir"

c++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$project_dir/test/native/stubs" \
  -I"$project_dir/src" \
  "$project_dir/test/native/stubs/WiFi.cpp" \
  "$project_dir/src/GivEnergyBatteries.cpp" \
  "$project_dir/src/GivEnergyProtocol.cpp" \
  "$project_dir/src/GivEnergyRegisters.cpp" \
  "$project_dir/src/GivEnergyIdentity.cpp" \
  "$project_dir/src/GivEnergyControls.cpp" \
  "$project_dir/src/GivEnergyScanner.cpp" \
  "$project_dir/test/native/test_main.cpp" \
  -o "$build_dir/test_runner"

"$build_dir/test_runner"

c++ -std=c++17 -Wall -Wextra -Werror -pedantic -fsyntax-only \
  -I"$project_dir/test/native/stubs" \
  -I"$project_dir/src" \
  "$project_dir/test/native/stubs/WiFi.cpp" \
  "$project_dir/src/GivEnergyBatteries.cpp" \
  "$project_dir/src/GivEnergyClient.cpp" \
  "$project_dir/src/GivEnergyScanner.cpp"

echo "Arduino-facing sources passed host syntax checks"
