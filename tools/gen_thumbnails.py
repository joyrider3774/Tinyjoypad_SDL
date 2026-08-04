#!/usr/bin/env python3
# Embeds assets/thumbnails/thumb_NN.bmp as C byte arrays, so the SDL3/SDL2
# ports can load them straight out of the exe's own .rodata instead of
# reading loose files off disk at runtime (see sdlBackend.c's own
# thumbnailsProbeIfNeeded()/gThumbnailBlobs). Re-run this whenever a
# thumbnail .bmp is added, removed, or regenerated (e.g. via `-ms`) -
# thumbnailData.h is generated output, not meant to be hand-edited.
#
# Usage: python tools/gen_thumbnails.py

import pathlib

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
THUMB_DIR = REPO_ROOT / "assets" / "thumbnails"
OUT_PATH = THUMB_DIR / "thumbnailData.h"
BYTES_PER_LINE = 20


def format_bytes(data: bytes) -> str:
    lines = []
    for i in range(0, len(data), BYTES_PER_LINE):
        chunk = data[i:i + BYTES_PER_LINE]
        lines.append("    " + ",".join(f"0x{b:02x}" for b in chunk) + ",")
    return "\n".join(lines)


def main() -> None:
    # Probe thumb_00.bmp, thumb_01.bmp, ... sequentially, stopping at the
    # first missing index - mirrors the same probe-and-stop convention the
    # old disk-loading thumbnailsProbeIfNeeded() used, so a future 34th
    # game with no thumbnail yet is still a silent no-op, not an error.
    paths = []
    index = 0
    while True:
        candidate = THUMB_DIR / f"thumb_{index:02d}.bmp"
        if not candidate.exists():
            break
        paths.append(candidate)
        index += 1

    if not paths:
        raise SystemExit(f"no thumb_NN.bmp files found in {THUMB_DIR}")

    out = []
    out.append("// GENERATED FILE - do not edit by hand.")
    out.append("// Regenerate with: python tools/gen_thumbnails.py")
    out.append(f"// Source: assets/thumbnails/thumb_00.bmp .. thumb_{len(paths) - 1:02d}.bmp")
    out.append("#pragma once")
    out.append("")

    for i, path in enumerate(paths):
        data = path.read_bytes()
        out.append(f"static const unsigned char gThumbnail{i:02d}Bmp[] = {{")
        out.append(format_bytes(data))
        out.append("};")
        out.append("")

    out.append("typedef struct")
    out.append("{")
    out.append("    const unsigned char* data;")
    out.append("    unsigned int len;")
    out.append("} ThumbnailBlob;")
    out.append("")
    out.append("static const ThumbnailBlob gThumbnailBlobs[] =")
    out.append("{")
    for i, path in enumerate(paths):
        out.append(f"    {{ gThumbnail{i:02d}Bmp, sizeof( gThumbnail{i:02d}Bmp ) }},")
    out.append("};")
    out.append("")
    out.append("static const int gThumbnailBlobCount = sizeof( gThumbnailBlobs ) / sizeof( gThumbnailBlobs[0] );")
    out.append("")

    OUT_PATH.write_text("\n".join(out), encoding="ascii")
    total_bytes = sum(p.stat().st_size for p in paths)
    print(f"wrote {OUT_PATH} ({len(paths)} thumbnails, {total_bytes} bytes source, "
          f"{OUT_PATH.stat().st_size} bytes generated)")


if __name__ == "__main__":
    main()
