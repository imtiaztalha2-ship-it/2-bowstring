#!/bin/sh
# Rebuilds Samples/violin_pack.ogg and Source/SampleMap.h from the University of Iowa violin samples.
# Needs: python3 with numpy, scipy, soundfile; oggenc (vorbis-tools); flac; curl; unzip.  Takes ~20 min.
set -e
cd "$(dirname "$0")"
B=https://theremin.music.uiowa.edu
mkdir -p raw/old raw/z2012
curl -sSL "$B/MISviolin.html" -o old.html
curl -sSL "$B/MIS-Pitches-2012/MISViolin2012.html" -o new.html
grep -io 'href="sound files/MIS/Strings/violin/[^"]*\.aiff"' old.html | sed 's/href="//;s/"$//;s/ /%20/g' | sort -u |
  while read u; do curl -sSL "$B/$u" -o "raw/old/$(basename "$u")"; done
grep -io 'href="[^"]*\.zip"' new.html | sed 's/href="..\///;s/"$//;s/ /%20/g' | sort -u |
  while read u; do f="raw/z2012/$(basename "$u")"; curl -sSL "$B/$u" -o "$f"; unzip -q -o "$f" -d "${f%.zip}"; done
sed -i "s#/home/claude/raw#$(pwd)/raw#g; s#/home/claude/proc#$(pwd)#g" *.py
python3 segment.py           # slice the scale recordings at the silences
python3 build_candidates.py  # verify pitch of every note, trim
python3 select_loop.py       # loop points, level matching, scoring
python3 pack.py              # best string per note, Ogg pack, SampleMap.h
cp violin_pack.ogg ../../Samples/ && cp SampleMap.h ../../Source/
