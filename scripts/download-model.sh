#!/usr/bin/env bash
set -euo pipefail
model="${1:-base}"
case "$model" in
  tiny) expected=bd577a113a864445d4c299885e0cb97d4ba92b5f ;;
  base) expected=465707469ff3a37a2b9b8d8f89f2f99de7299dac ;;
  small) expected=55356645c2b361a969dfd0ef2c5a50d530afd8d5 ;;
  *) echo 'Supported multilingual models: tiny, base, small' >&2; exit 2 ;;
esac
if [[ "$(uname -s)" == Darwin ]]; then
  default_dir="$HOME/Library/Application Support/Doornenbal Audio/TextAndChordTracer/models"
else
  default_dir="$HOME/.config/Doornenbal Audio/TextAndChordTracer/models"
fi
model_dir="${2:-$default_dir}"
mkdir -p "$model_dir"
destination="$model_dir/ggml-$model.bin"
verify() { [[ "$(shasum "$1" | cut -d ' ' -f 1)" == "$expected" ]]; }
if [[ -f "$destination" ]] && verify "$destination"; then printf 'Model ready: %s\n' "$destination"; exit 0; fi
staging="$(mktemp "$model_dir/.model.XXXXXX")"
trap 'rm -f "$staging"' EXIT
curl --fail --location --retry 2 "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-$model.bin" --output "$staging"
verify "$staging" || { echo 'Model checksum does not match pinned whisper.cpp v1.7.6 metadata.' >&2; exit 1; }
mv "$staging" "$destination"
printf 'Model installed: %s\n' "$destination"
