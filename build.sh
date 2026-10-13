#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${project_dir}/build/linux"
build_type=Release
cmake_args=()

while (($#)); do
    case "$1" in
        --debug) build_type=Debug; shift ;;
        --release) build_type=Release; shift ;;
        --build-dir)
            if (($# < 2)) || [[ -z "$2" ]]; then
                echo 'Error: --build-dir requires a directory.' >&2
                exit 2
            fi
            build_dir="$2"; shift 2 ;;
        -h|--help)
            echo 'Usage: bash build.sh [--debug|--release] [--build-dir DIRECTORY] [CMake options]'
            echo 'Example: bash build.sh -DCMAKE_PREFIX_PATH=/opt/SDL3'
            echo 'Requires: Bash, CMake >= 3.20, C++17 compiler, SDL3 and OpenGL development packages.'
            exit 0 ;;
        --) shift; cmake_args+=("$@"); break ;;
        *) cmake_args+=("$1"); shift ;;
    esac
done

if [[ "$(uname -s)" != Linux ]]; then
    echo 'This script targets Linux. On Windows, use build.bat or build.ps1.' >&2
    exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo 'Error: install CMake >= 3.20 and the dependencies listed in README.md.' >&2
    exit 1
fi

cmake -S "$project_dir" -B "$build_dir" \
    "-DCMAKE_BUILD_TYPE=$build_type" "${cmake_args[@]}"
cmake --build "$build_dir" --config "$build_type" --parallel
ctest --test-dir "$build_dir" -C "$build_type" --output-on-failure
printf '\nBuild completed in: %s\n' "$build_dir"
