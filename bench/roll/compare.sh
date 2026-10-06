#!/usr/bin/env bash
# SPDX-License-Identifier: 0BSD
# Compare the working-tree generator with an unchanged, archived Dice revision.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?usage: compare.sh OUTPUT_DIRECTORY [build|generate|micro]}
phase=${2:-build}
mkdir -p "$out"
out=$(cd "$out" && pwd)
original="$out/original"
old="$out/original-build"
new="$out/current-build"
cc=${CC:-cc}
revision=${REVISION:-HEAD}
if [ "$(uname)" = Darwin ]; then
    suffix=dylib
    shared=(-dynamiclib)
    extra=(-pthread)
    preload=DYLD_INSERT_LIBRARIES
else
    suffix=so
    shared=(-shared)
    extra=(-pthread -ldl)
    preload=LD_PRELOAD
fi
flags=(-D_GNU_SOURCE -DDEFAULT_SMR_EBR -O3 -DNDEBUG -fPIC)

adapt() {
    # Keep benchmark logic unchanged; only replace repository header imports.
    printf '#include "dice.h"\n' > "$2"
    sed '/^#include <dice\//d; /^#include <vsync\//d' "$1" >> "$2"
}

if [ "$phase" = build ] || [ "$phase" = generate ]; then
  if [ "$phase" = build ]; then
    if [ -e "$original" ]; then
        echo "Use a fresh output directory for a new build" >&2
        exit 1
    fi
    mkdir -p "$original" "$out/generated" "$out/logs"
    git -C "$repo" rev-parse "$revision" > "$out/revision.txt"
    git -C "$repo" archive "$revision" | tar -x -C "$original"
    git -C "$repo" diff > "$out/working-tree.patch"
    tar -czf "$out/generator-sources.tar.gz" -C "$repo" src/cli test/roll examples/roll
    {
        uname -a
        "$cc" --version
        printf 'flags: %s\n' "${flags[*]}"
        printf 'runtime checks: OFF; threads: 1\n'
    } > "$out/environment.txt"
    cmake -G 'Unix Makefiles' -S "$original" -B "$old" -DCMAKE_BUILD_TYPE=Release \
        -DDICE_LTO=ON -DDICE_TESTS=OFF -DDICE_BENCHMARKS=ON \
        > "$out/logs/original-build.log" 2>&1
    cmake --build "$old" --target micro micro2 micro3 micro4 micro5 \
        dice-self dice-pthread_create dice-bundle dice-bundle-box tsano -j4 \
        >> "$out/logs/original-build.log" 2>&1
    cmake -G 'Unix Makefiles' -S "$repo" -B "$new" -DCMAKE_BUILD_TYPE=Release \
        -DDICE_LTO=ON -DDICE_TESTS=OFF -DDICE_BENCHMARKS=OFF \
        > "$out/logs/current-build.log" 2>&1
    cmake --build "$new" --target dice-cli -j4 \
        >> "$out/logs/current-build.log" 2>&1
  fi
    # CMake uses ThinLTO with AppleClang. Match its actual mode, not just -flto.
    lto=$(sed -n '/^C_FLAGS = /s/.*\(-flto[^ ]*\).*/\1/p' \
        "$old/bench/micro/CMakeFiles/micro3.dir/flags.make")
    if [ -z "$lto" ]; then echo 'Cannot determine original LTO mode' >&2; exit 1; fi
    flags+=("$lto")
    printf 'generated compiler flags: %s\n' "${flags[*]}" >> "$out/environment.txt"
    for name in micro micro2 micro3 micro4 micro5 bundle box; do
        dir="$out/generated/$name"
        mkdir -p "$dir"
        plugins=false
        mods='self pthread_create pthread_mutex pthread_rwlock pthread_cond malloc sem cxa tsan stacktrace'
        consumes='(CHAIN_CONTROL EVENT_DICE_INIT)'
        case "$name" in
            micro) mods=''; plugins=true ;;
            micro2) mods='self pthread_create'; plugins=true
                consumes='(CAPTURE_EVENT EVENT_MA_AWRITE)' ;;
            micro3) consumes="$consumes (CAPTURE_EVENT EVENT_MA_AWRITE)" ;;
            micro4) consumes="$consumes (CAPTURE_EVENT EVENT_MA_AREAD)" ;;
            micro5) consumes="$consumes (CAPTURE_EVENT EVENT_MA_AWRITE EVENT_MA_AREAD)" ;;
            bundle|box) mods='self pthread_create pthread_mutex pthread_cond pthread_rwlock tsan stacktrace'
                if [ "$name" = bundle ]; then plugins=true; fi ;;
        esac
        {
            printf '(dice (version 1) (runtime (embed %s) (plugins %s))\n' "$mods" "$plugins"
            if [ -n "$mods" ]; then printf '(slot dice_self (number 4))\n'; fi
            case "$name" in
                micro2) printf '(slot app (number 10000) (plugin true) (consumes %s))\n' "$consumes" ;;
                micro[345]) printf '(slot app (number 5) (consumes %s))\n' "$consumes" ;;
            esac
            printf ')\n'
        } > "$dir/config.dice"
        "$new/src/cli/dice" roll "$dir/config.dice" -o "$dir"
        sources=("$dir/dice.c")
        case "$name" in
            micro3) handler=microcb.c ;;
            micro4) handler=microcb-tls.c ;;
            micro5) handler=microcb-multi.c ;;
            *) handler='' ;;
        esac
        if [ -n "$handler" ]; then
            adapt "$original/bench/micro/$handler" "$dir/handler.c"
            sources+=("$dir/handler.c")
        fi
        cxx=()
        if [ "$suffix" = dylib ] && [[ "$mods" == *cxa* ]]; then cxx=(-lstdc++); fi
        "$cc" "${flags[@]}" "${shared[@]}" -DDICE_MODULE_SLOT=5 -I"$dir" \
            "${sources[@]}" "${extra[@]}" ${cxx[@]+"${cxx[@]}"} -o "$dir/runtime.$suffix" \
            > "$out/logs/$name-build.log" 2>&1
        case "$name" in
            micro*)
                adapt "$original/bench/micro/$name.c" "$dir/main.c"
                "$cc" "${flags[@]}" -I"$dir" "$dir/main.c" \
                    "$dir/runtime.$suffix" "${extra[@]}" -o "$dir/$name" \
                    >> "$out/logs/$name-build.log" 2>&1 ;;
        esac
        echo "Built $name"
    done
    exit 0
fi

if [ "$phase" != micro ]; then exit 2; fi
repeats=${REPEATS:-7}
seconds=${SECONDS_PER_RUN:-3}
printf 'benchmark,version,repeat,threads,count,seconds,iterations_per_second\n' > "$out/micro.csv"
run_one() {
    local name=$1 version=$2 repeat=$3 duration=$4
    local binary log
    local command=()
    if [ "$version" = original ]; then
        binary="$old/bench/micro/$name"
        if [ "$name" = micro2 ]; then
            command=(env "$preload=$old/src/mod/dice-self.$suffix:$old/src/mod/dice-pthread_create.$suffix")
        fi
    else
        binary="$out/generated/$name/$name"
    fi
    log="$out/logs/$name-$version-$repeat.log"
    ${command[@]+"${command[@]}"} "$binary" 1 "$duration" > "$log" 2>&1
    case "$name" in
        micro[345])
            # Existing handlers print delivery counts at shutdown. Verify that
            # both runtimes actually execute the same amount of handler work.
            awk -v b="$name" '
              /^count:/ { delivered=$2 }
              /threads=.*count=.*elapsed=/ { split($2,c,"="); iterations=c[2] }
              END { if (delivered != iterations * (b=="micro5" ? 2 : 1)) exit 1 }
            ' "$log" ;;
    esac
    if [ "$repeat" != warmup ]; then
        awk -v b="$name" -v v="$version" -v r="$repeat" '
          /threads=.*count=.*elapsed=/ {
            split($1,t,"="); split($2,c,"="); split($3,s,"="); sub(/s$/,"",s[2]);
            printf "%s,%s,%d,%d,%.0f,%.2f,%.2f\n",b,v,r,t[2],c[2],s[2],c[2]/s[2]
          }' "$log" >> "$out/micro.csv"
    fi
}
for name in micro micro2 micro3 micro4 micro5; do
    run_one "$name" original warmup 1
    run_one "$name" generated warmup 1
    for ((r=1; r<=repeats; r++)); do
        versions='original generated'
        if ((r % 2 == 0)); then versions='generated original'; fi
        for version in $versions; do run_one "$name" "$version" "$r" "$seconds"; done
    done
    echo "Measured $name ($repeats alternating pairs, ${seconds}s each)"
done
