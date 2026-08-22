#!/usr/bin/env bash
set -euo pipefail

font_url="https://raw.githubusercontent.com/notofonts/noto-cjk/f8d157532fbfaeda587e826d4cd5b21a49186f7c/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf"
font_sha256="2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "${script_dir}/.." && pwd)"
font_dir="$(mktemp -d /tmp/aura-font.XXXXXX)"
font_path="${font_dir}/NotoSansCJKsc-Regular.otf"

cleanup() {
  rm -rf "${font_dir}"
}
trap cleanup EXIT

curl -L --fail --silent --show-error \
  "${font_url}" \
  -o "${font_path}"
printf '%s  %s\n' "${font_sha256}" "${font_path}" | shasum -a 256 -c -

symbols="$(cd "${repo_dir}/aura" && python3 extract_unicode_chars.py --symbols-only weather.ino translations.h)"

normalize_font_output() {
  if ! grep -Fq '__has_include("lvgl.h")' "$1"; then
    perl -0pi -e 's{#ifdef LV_LVGL_H_INCLUDE_SIMPLE}{#ifdef __has_include\n    #if __has_include("lvgl.h")\n        #ifndef LV_LVGL_H_INCLUDE_SIMPLE\n            #define LV_LVGL_H_INCLUDE_SIMPLE\n        #endif\n    #endif\n#endif\n\n#ifdef LV_LVGL_H_INCLUDE_SIMPLE}' "$1"
  fi
  perl -0pi -e 's{^ \* Opts:.*$}{ * Opts: reproducible via aura/regenerate_chinese_fonts.sh}m; s/\n+\z/\n/' "$1"
}

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
  normalize_font_output "${output_path}"
done
