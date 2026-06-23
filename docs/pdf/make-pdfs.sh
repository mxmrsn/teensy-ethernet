#!/usr/bin/env bash
#
# Build the teensy-ethernet write-ups as PDF, using headless Chrome (or Edge)
# to render the HTML sources in this directory. Shares the house style and
# pipeline with the Sciton wifi-video-streamer write-ups.
#
#   ./make-pdfs.sh              one PDF per document (default)
#   ./make-pdfs.sh --combined   a single combined PDF, in reading order
#   ./make-pdfs.sh -o DIR       write output to DIR (default: docs/pdf/)
#
# Override the browser with CHROME=/path/to/chrome (Chrome or Chromium-based Edge).
#
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
out="$here"
combined=0

while [ $# -gt 0 ]; do
  case "$1" in
    -c|--combined) combined=1 ;;
    -o|--out)      out="${2:?-o needs a directory}"; shift ;;
    -h|--help)     sed -n '3,12p' "$0" | sed 's/^#\s\{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

# locate a Chromium browser: $CHROME, then common Windows / macOS / Linux paths
find_chrome() {
  [ -n "${CHROME:-}" ] && { echo "$CHROME"; return; }
  local c
  for c in \
    "/c/Program Files/Google/Chrome/Application/chrome.exe" \
    "/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" \
    "/c/Program Files/Microsoft/Edge/Application/msedge.exe" \
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
    "$(command -v google-chrome 2>/dev/null)" \
    "$(command -v chromium 2>/dev/null)"; do
    [ -n "$c" ] && [ -x "$c" ] && { echo "$c"; return; }
  done
}
CHROME="$(find_chrome)"
[ -n "$CHROME" ] && [ -x "$CHROME" ] || { echo "Chrome/Edge not found (set CHROME=...)" >&2; exit 1; }

# documents in reading order
docs=(performance)

# build a file:// URL Chrome understands. On Windows (Git Bash / WSL paths),
# native Chrome/Edge can't read a /c/... path, so convert to file:///C:/... and
# percent-encode spaces (this tree lives under "OneDrive - Sciton Inc").
to_url() {
  local p="$1"
  if command -v cygpath >/dev/null 2>&1; then
    p="$(cygpath -m "$p")"               # -> C:/Users/.../file.html
    printf 'file:///%s' "${p// /%20}"
  else
    printf 'file://%s' "${p// /%20}"
  fi
}

render() { # <html-path> <pdf-path>
  "$CHROME" --headless=new --disable-gpu --no-sandbox --no-pdf-header-footer \
    --print-to-pdf="$2" "$(to_url "$1")" >/dev/null 2>&1
  [ -s "$2" ] || { echo "render failed: $2" >&2; exit 1; }
  # Chrome stamps each render with the wall-clock CreationDate/ModDate; pin them
  # (length-preserving, so the xref stays valid) so unchanged content produces
  # byte-identical PDFs and git only sees real changes.
  perl -0777 -i -pe "s/D:[0-9]{14}/D:20000101000000/g" "$2" 2>/dev/null || true
  echo "wrote ${2/#$repo\//}"
}

mkdir -p "$out"

if [ "$combined" -eq 1 ]; then
  tmp="$here/.combined.html"
  {
    echo '<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">'
    echo '<title>teensy-ethernet</title><link rel="stylesheet" href="styles.css"></head><body>'
    first=1
    for d in "${docs[@]}"; do
      [ "$first" -eq 1 ] || echo '<div class="pagebreak"></div>'
      first=0
      awk '/<body>/{f=1;next} /<\/body>/{f=0} f' "$here/$d.html"
    done
    echo '</body></html>'
  } > "$tmp"
  render "$tmp" "$out/teensy-ethernet-performance.pdf"
  rm -f "$tmp"
else
  for d in "${docs[@]}"; do
    render "$here/$d.html" "$out/teensy-ethernet-$d.pdf"
  done
fi
