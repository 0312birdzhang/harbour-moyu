#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
native_root="$project_root/.rime-build"
prefix="$native_root/prefix"
jobs="${MOYU_BUILD_JOBS:-4}"
vendor="$project_root/librime/vendor"

cd "$vendor"
sha256sum --check SHA256SUMS
mkdir -p "$native_root/source" "$prefix" "$native_root/data/build" "$native_root/licenses"

unpack() {
    local archive="$1"
    local name="$2"
    mkdir -p "$native_root/source/$name"
    tar -xzf "$vendor/$archive" --strip-components=1 -C "$native_root/source/$name"
}

build_library() {
    local name="$1"
    shift
    cmake -S "$native_root/source/$name" -B "$native_root/build/$name" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH="$prefix" \
        -DCMAKE_CXX_FLAGS="${CXXFLAGS:-} -I$prefix/include" \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF "$@"
    cmake --build "$native_root/build/$name" --parallel "$jobs"
    cmake --install "$native_root/build/$name"
}

unpack leveldb-1.23.tar.gz leveldb
unpack opencc-1.1.9.tar.gz opencc
unpack librime-1.13.1.tar.gz librime
unpack rime-pinyin-simp-0c6861ef7420ee780270ca6d993d18d4101049d0.tar.gz pinyin-simp
unpack rime-essay-054920de4f54c9e5994276a96a4fc2a35cb51aa3.tar.gz essay
unpack rime-ice-927e1cf1d3eeb9bf4be85e3f684dba4dd485b6f6.tar.gz ice

# Marisa's image can exceed librime's 4 KiB reserve for a one-entry dictionary.
# Leave space for that image so small imported dictionaries compile successfully.
python3 - "$native_root/source/librime/src/rime/dict/table.cc" <<'PY'
from pathlib import Path
import sys

source = Path(sys.argv[1])
text = source.read_text(encoding='utf-8')
old = 'const size_t kReservedSize = 4096;'
if text.count(old) != 1:
    raise SystemExit('Unexpected librime table allocation code')
source.write_text(text.replace(old, 'const size_t kReservedSize = 8192;'), encoding='utf-8')
PY

build_library leveldb -DLEVELDB_BUILD_TESTS=OFF -DLEVELDB_BUILD_BENCHMARKS=OFF \
    -DHAVE_SNAPPY=OFF -DHAVE_CRC32C=OFF -DHAVE_TCMALLOC=OFF
build_library opencc -DUSE_SYSTEM_MARISA=OFF -DENABLE_GTEST=OFF -DENABLE_DARTS=OFF \
    -DBUILD_DOCUMENTATION=OFF -DBUILD_TESTING=OFF
cp "$native_root/build/opencc/deps/marisa-0.2.6/libmarisa.a" "$prefix/lib/"
cp -a "$native_root/source/opencc/deps/marisa-0.2.6/include/." "$prefix/include/"
build_library librime -DENABLE_LOGGING=OFF -DBUILD_TEST=OFF -DBUILD_DATA=OFF

cp "$native_root/source/pinyin-simp/pinyin_simp.dict.yaml" "$native_root/data/"
cp "$native_root/source/essay/essay.txt" "$native_root/data/"
cp "$project_root/librime/data/"* "$native_root/data/"
cp -a "$prefix/share/opencc" "$native_root/data/"

python3 - "$native_root/data" "$native_root/source/ice" <<'PY'
from pathlib import Path
import re
import sys

data_directory = Path(sys.argv[1])
ice_directory = Path(sys.argv[2])
for name in ('8105', 'base', 'ext', 'tencent', 'others'):
    text = (ice_directory / 'cn_dicts' / (name + '.dict.yaml')).read_text(encoding='utf-8')
    text = re.sub(r'^name:.*$', 'name: ice_' + name, text, count=1, flags=re.MULTILINE)
    # Keep the spelling already used by migrated user dictionaries.
    text = re.sub(r'\b([nl])ve\b', r'\1ue', text)
    (data_directory / ('ice_' + name + '.dict.yaml')).write_text(text, encoding='utf-8')
syllables = set()
for dictionary in data_directory.glob('*.dict.yaml'):
    for line in dictionary.read_text(encoding='utf-8').splitlines():
        fields = line.split('\t')
        if len(fields) > 1:
            syllables.update(token for token in fields[1].split() if re.fullmatch('[a-z]+', token))
(data_directory / 'full-pinyin.txt').write_text('\n'.join(sorted(syllables)) + '\n', encoding='utf-8')
PY

cp "$native_root/source/librime/LICENSE" "$native_root/licenses/librime.txt"
cp "$native_root/source/leveldb/LICENSE" "$native_root/licenses/leveldb.txt"
cp "$native_root/source/opencc/deps/marisa-0.2.6/COPYING.md" "$native_root/licenses/marisa.txt"
cp "$native_root/source/opencc/LICENSE" "$native_root/licenses/opencc.txt"
cp "$native_root/source/librime/include/COPYING.darts-clone" "$native_root/licenses/darts-clone.txt"
cp "$native_root/source/pinyin-simp/LICENSE" "$native_root/licenses/pinyin-simp.txt"
cp "$native_root/source/essay/LICENSE" "$native_root/licenses/essay.txt"
cp "$native_root/source/ice/LICENSE" "$native_root/licenses/rime-ice.txt"
cp "$project_root/librime/vendor/README.md" "$native_root/licenses/sources.md"
