#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# make-stdcxx-pdf.sh — render STDCXX.md to STDCXX.pdf.
#
# The Markdown keeps its own title and subtitle so it reads well on GitHub.
# For the PDF they are passed as pandoc metadata instead, which puts a title
# block before the table of contents; the first two blocks of the Markdown are
# therefore skipped in the temporary copy.
#
# Needs pandoc and weasyprint.
# -----------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${HERE}/STDCXX.md"
PDF="${HERE}/STDCXX.pdf"
CSS="${HERE}/STDCXX.css"
TMP="$(mktemp "${TMPDIR:-/tmp}/stdcxx.XXXXXX.md")"
trap 'rm -f "${TMP}"' EXIT

# Drop the Markdown title and the subtitle paragraph; pandoc renders them from
# the metadata below.
awk '
  NR == 1 && /^# / { next }
  /^<p class="subtitle">/ { next }
  { print }
' "${SRC}" > "${TMP}"

pandoc "${TMP}" \
  -f markdown -t html5 --standalone --embed-resources \
  --css "${CSS}" \
  --metadata pagetitle="StateOS stdc++ on µOS++ III" \
  --metadata lang=en \
  --metadata title="StateOS stdc++ on µOS++ III" \
  --metadata subtitle="A detailed guide to the StateOS stdc++ package, its port to µOS++ III SMP, and its use in a µTest++ test and a CTest case — from the GCC gthread contract to the Raspberry Pi 4 B hardware run" \
  --metadata toc-title="Contents" \
  --toc --toc-depth=2 \
  -o "${TMP}.html"

weasyprint "${TMP}.html" "${PDF}"
echo "wrote ${PDF}"
