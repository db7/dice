#!/usr/bin/env bash
# SPDX-License-Identifier: 0BSD
# Reuse the existing application workloads and their cached instrumented binaries.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
out=$(cd "${1:?usage: applications.sh COMPARISON_DIRECTORY}" && pwd)
repeats=${REPEATS:-5}
suites=${SUITES:-"leveldb raytracing scratchapixel"}
csv=${CSV:-applications.csv}
tag=${LOG_TAG:-apps}
old="$out/original-build"
mkdir -p "$out/app-work" "$out/tsano"
if [ "$(uname)" = Darwin ]; then
    suffix=dylib
    preload=DYLD_INSERT_LIBRARIES
    search=DYLD_LIBRARY_PATH
    ln -sf "$old/deps/tsano/libtsano.dylib" "$out/tsano/libclang_rt.tsan_osx_dynamic.dylib"
else
    suffix=so
    preload=LD_PRELOAD
    search=LD_LIBRARY_PATH
    for name in libtsan.so.0 libtsan.so.1 libtsan.so.2; do
        ln -sf "$old/deps/tsano/libtsano.so" "$out/tsano/$name"
    done
fi
# Same fillseq population as the existing LevelDB Makefile, in a private DB.
if [[ " $suites " == *' leveldb '* ]]; then
    "$repo/bench/leveldb/work/vanilla/db_bench" --db="$out/leveldb-data" \
        --threads=1 --benchmarks=fillseq > "$out/logs/$tag-leveldb-populate.log" 2>&1
fi
printf 'benchmark,mode,version,repeat,seconds,launches\n' > "$out/$csv"
for name in $suites; do
    batch=1
    case "$name" in
        leveldb) binary="$repo/bench/leveldb/work/sanitized/db_bench"
            args=(--db="$out/leveldb-data" --use_existing_db=1 --threads=1 --benchmarks=readrandom --num=5000000) ;;
        raytracing) binary="$repo/bench/raytracing/work/sanitized/theRestOfYourLife"; args=() ;;
        scratchapixel) binary="$repo/bench/scratchapixel/work/sanitized/raster3d"
            args=(); batch=50 ;;
    esac
    shasum -a 256 "$binary" >> "$out/application-binaries.sha256"
    for mode in bundle box; do
        for ((r=0; r<=repeats; r++)); do
            versions='original generated'
            if ((r % 2 == 0)); then versions='generated original'; fi
            for version in $versions; do
                if [ "$version" = original ]; then
                    lib="$old/bench/lib/libdice-$mode.$suffix"
                    if [ "$mode" = box ]; then lib="$old/bench/lib/libdice-bundle-box.$suffix"; fi
                else
                    lib="$out/generated/$mode/runtime.$suffix"
                fi
                label="$tag-$name-$mode-$version-$r"
                mkdir -p "$out/app-work/$label"
                (
                    cd "$out/app-work/$label"
                    TIMEFORMAT='%3R'
                    { time for ((b=0; b<batch; b++)); do
                        env "$preload=$lib" "$search=$out/tsano" \
                            "$binary" ${args[@]+"${args[@]}"}
                        done > "$out/logs/$label.stdout" 2> "$out/logs/$label.stderr"; } \
                        2> "$out/logs/$label.time"
                )
                if [ "$name" = leveldb ] && rg -q '\(0 of .* found\)' "$out/logs/$label.stdout"; then
                    echo 'LevelDB read no populated keys' >&2
                    exit 1
                fi
                if ((r > 0)); then
                    seconds=$(awk -v n="$batch" '{ printf "%.6f", $1/n }' "$out/logs/$label.time")
                    printf '%s,%s,%s,%d,%s,%d\n' "$name" "$mode" "$version" "$r" \
                        "$seconds" "$batch" >> "$out/$csv"
                fi
                echo "Measured $label"
            done
        done
    done
done
