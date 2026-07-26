#!/usr/bin/env python3
"""Download high-quality Pokemon cries into sounds/cries/.

The port will use sounds/cries/<species>.ogg or .mp3 in place of the GBA's 8-bit
cry samples, where <species> is the game's *internal* species number (the value
of SPECIES_* in include/constants/species.h). This script reads those constants
straight from the repo, so it stays correct if the species list is extended.

    python tools/fetch_hq_cries.py                  # all species
    python tools/fetch_hq_cries.py --limit 386      # Gen 1-3 only
    python tools/fetch_hq_cries.py --source <url>   # a different mirror

The default source is Pokemon Showdown's public cry directory, which serves
mp3 files keyed by lowercase species name. SDL2_mixer plays mp3 directly via
libmpg123, so nothing needs converting.

These are ripped game assets: they are downloaded to your machine and are NOT
committed to this repository (sounds/cries/ is gitignored).
"""

import argparse
import os
import re
import sys
import time
import urllib.error
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPECIES_H = os.path.join(REPO, "include", "constants", "species.h")
OUT_DIR = os.path.join(REPO, "sounds", "cries")

DEFAULT_SOURCE = "https://play.pokemonshowdown.com/audio/cries/{slug}.mp3"

# Species whose Showdown slug differs from a plain lowercasing of the constant.
SLUG_OVERRIDES = {
    "NIDORAN_F": "nidoranf",
    "NIDORAN_M": "nidoranm",
    "MR_MIME": "mrmime",
    "HO_OH": "hooh",
    "FARFETCHD": "farfetchd",
    "PORYGON2": "porygon2",
    "DEOXYS": "deoxys",
    "UNOWN": "unown",
}


def read_species():
    """Return [(number, constant_name)] for every real species in the game."""
    out = []
    with open(SPECIES_H, "r", encoding="utf-8") as f:
        for line in f:
            m = re.match(r"\s*#define\s+SPECIES_([A-Z0-9_]+)\s+(\d+)\s*$", line)
            if not m:
                continue
            name, num = m.group(1), int(m.group(2))
            if num == 0 or name in ("NONE", "EGG"):
                continue
            if name.startswith("OLD_UNOWN") or name.startswith("UNOWN_"):
                continue  # Unown letter forms share one cry
            out.append((num, name))
    return out


def slug_for(name):
    if name in SLUG_OVERRIDES:
        return SLUG_OVERRIDES[name]
    return name.lower().replace("_", "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", default=DEFAULT_SOURCE,
                    help="URL template containing {slug}")
    ap.add_argument("--limit", type=int, default=0,
                    help="only fetch species numbers up to this value")
    ap.add_argument("--force", action="store_true",
                    help="re-download files that already exist")
    ap.add_argument("--delay", type=float, default=0.15,
                    help="seconds to wait between requests")
    args = ap.parse_args()

    species = read_species()
    if args.limit:
        species = [s for s in species if s[0] <= args.limit]
    if not species:
        print("no species found in %s" % SPECIES_H, file=sys.stderr)
        return 1

    os.makedirs(OUT_DIR, exist_ok=True)
    ext = os.path.splitext(args.source)[1].lstrip(".") or "mp3"

    got = skipped = failed = 0
    for num, name in species:
        dest = os.path.join(OUT_DIR, "%d.%s" % (num, ext))
        if os.path.exists(dest) and not args.force:
            skipped += 1
            continue

        url = args.source.format(slug=slug_for(name))
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "pokeemerald-accessible/1.0"})
            with urllib.request.urlopen(req, timeout=30) as r:
                data = r.read()
        except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError) as e:
            print("FAIL %4d %-14s %s" % (num, name, e), file=sys.stderr)
            failed += 1
            continue

        with open(dest, "wb") as f:
            f.write(data)
        got += 1
        print("  ok %4d %-14s %6d bytes" % (num, name, len(data)))
        time.sleep(args.delay)

    print("\ndownloaded %d, already present %d, failed %d -> %s"
          % (got, skipped, failed, OUT_DIR))
    return 0


if __name__ == "__main__":
    sys.exit(main())
