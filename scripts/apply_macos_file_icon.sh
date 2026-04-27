#!/bin/sh
set -eu

icon_png=$1
target_file=$2
work_dir=$3

if [ ! -f "$icon_png" ] || [ ! -f "$target_file" ]; then
    exit 0
fi

if ! command -v sips >/dev/null 2>&1 ||
   ! command -v DeRez >/dev/null 2>&1 ||
   ! command -v Rez >/dev/null 2>&1 ||
   ! command -v SetFile >/dev/null 2>&1; then
    exit 0
fi

mkdir -p "$work_dir"
icon_copy="$work_dir/cli-icon.png"
resource_file="$work_dir/cli-icon.rsrc"

cp "$icon_png" "$icon_copy"
sips -i "$icon_copy" >/dev/null
DeRez -only icns "$icon_copy" > "$resource_file"

if [ -s "$resource_file" ]; then
    xattr -d com.apple.ResourceFork "$target_file" 2>/dev/null || true
    Rez -append "$resource_file" -o "$target_file"
    SetFile -a C "$target_file"
fi
