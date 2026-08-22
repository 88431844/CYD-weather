#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "${script_dir}/.." && pwd)"
font_dir="$(mktemp -d /tmp/aura-font.XXXXXX)"
font_path="${font_dir}/NotoSansCJKsc-Regular.otf"

cleanup() {
  rm -rf "${font_dir}"
}
trap cleanup EXIT

curl -L --fail --silent --show-error \
  "https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf" \
  -o "${font_path}"

symbols="$(cd "${repo_dir}/aura" && python3 extract_unicode_chars.py --symbols-only weather.ino translations.h)"

for size in 12 14 16 20; do
  output_path="${repo_dir}/aura/lv_font_noto_sans_sc_${size}.c"
  npx --yes lv_font_conv@1.5.3 \
    --font "${font_path}" \
    --range 0x20-0x7E \
    --symbols "${symbols}" \
    --size "${size}" \
    --bpp 4 \
    --no-compress \
    --format lvgl \
    --lv-font-name "lv_font_noto_sans_sc_${size}" \
    --output "${output_path}"
  perl -0pi -e 's/\n+\z/\n/' "${output_path}"
done
