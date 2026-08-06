"""
Round-trip every BW frame container against the frame stack it was built from.

Why this exists alongside the emulator tests in test/compression/smol.c: those
cover two species at the shipped chunk size, because a test ROM is not the place
to carry every animated sprite. This covers ALL of them, at several chunk sizes,
in a few seconds and with no emulator - and it is what caught the bug that
shipped the format wrong the first time, where a five bit MODE_MASK read one bit
of numComponents and every container with an ODD chunk count decoded as garbage.
An even chunk count worked, so half the sprites were fine.

It needs no Pillow. Frames are stacked vertically on whole tile rows, so in 4bpp
each frame is a contiguous run and the tools alone are enough.

Run:  python check_frame_containers.py [--repo PATH] [--chunks 1,2,4,8]
Needs a built repo - gbagfx and compresSmol come from `make`.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HEADER = 'src/data/rogue_bw_anim.h'


def read_table(repo):
    """Species, frame count and frame size out of the generated table.

    Parsed rather than recomputed: the point is to check the container against
    what the ENGINE will believe, and the engine reads this table.
    """
    text = (repo / HEADER).read_text(encoding='utf-8', errors='replace')
    out = []
    for m in re.finditer(
            r'\.species\s*=\s*SPECIES_(\w+),.*?'
            r'\.frames\s*=\s*gBwAnimGfx_(\w+),.*?'
            r'\.frameCount\s*=\s*(\d+),.*?'
            r'\.width\s*=\s*(\d+),.*?'
            r'\.height\s*=\s*(\d+),', text, re.S):
        species, sym, count, w, h = m.groups()
        out.append(dict(species=species, stem=species.lower(), sym=sym,
                        frames=int(count), w=int(w), h=int(h)))
    return out


def container_header(path):
    """mode, chunk count, frames per chunk, frame size, total frames."""
    data = path.read_bytes()
    w0 = int.from_bytes(data[0:4], 'little')
    w1 = int.from_bytes(data[4:8], 'little')
    return dict(mode=w0 & 0xf,
                chunks=(w0 >> 4) & 0xfff,
                perChunk=(w0 >> 16) & 0xffff,
                frameSize=w1 & 0xffff,
                totalFrames=(w1 >> 16) & 0xffff,
                offsets=[int.from_bytes(data[8 + 4 * i:12 + 4 * i], 'little')
                         for i in range((w0 >> 4) & 0xfff)],
                size=len(data))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=os.environ.get(
        'POKEDECOMP_REPO', str(Path(__file__).resolve().parents[2])))
    ap.add_argument('--chunks', default='1,2,4,8',
                    help='chunk sizes to round-trip, beyond the shipped one')
    args = ap.parse_args()

    repo = Path(args.repo)
    gbagfx = repo / 'tools/gbagfx/gbagfx'
    smol = repo / 'tools/compresSmol/compresSmol'
    for t in (gbagfx, smol):
        if not t.exists():
            raise SystemExit(f'{t} is missing - run make first')

    work = repo / '_framecheck'
    if work.exists():
        shutil.rmtree(work)
    work.mkdir()

    table = read_table(repo)
    if not table:
        raise SystemExit(f'no entries parsed out of {HEADER}')

    chunks = [int(c) for c in args.chunks.split(',') if c.strip()]
    failures = []

    for e in table:
        png = repo / 'graphics/pokemon' / e['stem'] / 'bw_anim.png'
        if not png.exists():
            failures.append(f'{e["stem"]}: {png} is missing')
            continue

        flat = work / f'{e["stem"]}.4bpp'
        subprocess.run([str(gbagfx), str(png), str(flat)], check=True,
                       capture_output=True)
        raw = flat.stat().st_size
        want = e['w'] * e['h'] // 2

        #  The table says the frames are w x h and there are frameCount of them.
        #  If that disagrees with the asset, everything downstream is wrong in a
        #  way no round trip would show, because the container would be built
        #  from the same wrong number.
        if raw != want * e['frames']:
            failures.append(
                f'{e["stem"]}: table says {e["frames"]} x {e["w"]}x{e["h"]} '
                f'= {want * e["frames"]} B, asset is {raw} B')
            continue

        for k in chunks:
            out = work / f'{e["stem"]}.k{k}.fsmol'
            back = work / f'{e["stem"]}.k{k}.4bpp'
            r = subprocess.run([str(smol), '-fw', str(flat), str(out),
                                str(want), str(k)], capture_output=True)
            if r.returncode != 0 or not out.exists():
                failures.append(f'{e["stem"]} k={k}: writing failed')
                continue

            h = container_header(out)
            expect = dict(mode=7, perChunk=k, frameSize=want,
                          totalFrames=e['frames'],
                          chunks=(e['frames'] + k - 1) // k)
            for field, value in expect.items():
                if h[field] != value:
                    failures.append(f'{e["stem"]} k={k}: header {field} is '
                                    f'{h[field]}, expected {value}')
            #  Offsets have to be inside the file and strictly increasing, or a
            #  chunk decode reads whatever follows.
            last = 1
            for i, off in enumerate(h['offsets']):
                if off * 4 >= h['size'] or off <= last:
                    failures.append(f'{e["stem"]} k={k}: chunk {i} offset '
                                    f'{off} is out of order or past the end')
                    break
                last = off

            r = subprocess.run([str(smol), '-d', str(out), str(back)],
                               capture_output=True)
            if r.returncode != 0 or not back.exists():
                failures.append(f'{e["stem"]} k={k}: decoding failed')
                continue
            if back.read_bytes() != flat.read_bytes():
                failures.append(f'{e["stem"]} k={k}: round trip differs')

        print(f'{e["stem"]:12s} {e["frames"]:3d} frames  {e["w"]}x{e["h"]}  '
              f'{want:4d} B/frame  ok')

    shutil.rmtree(work)

    print()
    if failures:
        for f in failures:
            print(f'FAIL {f}')
        print(f'\n{len(failures)} failures')
        return 1
    print(f'{len(table)} containers round-tripped at chunk sizes '
          f'{", ".join(str(c) for c in chunks)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
