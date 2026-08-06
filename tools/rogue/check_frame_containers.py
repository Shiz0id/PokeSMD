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

    # The asset path is taken from the INCGFX line rather than rebuilt from the
    # species name, for the same reason tileset paths are never guessed from
    # symbol names: a species can have a front and a back, they differ only by a
    # suffix, and reconstructing it would quietly check the front twice.
    assets = dict(re.findall(
        r'gBwAnimGfx_(\w+)\[\]\s*=\s*INCGFX_U32\(\s*"([^"]+)"', text))

    out = []
    for m in re.finditer(
            r'\.species\s*=\s*SPECIES_(\w+),.*?'
            r'\.frames\s*=\s*gBwAnimGfx_(\w+),.*?'
            r'\.frameCount\s*=\s*(\d+),.*?'
            r'\.width\s*=\s*(\d+),.*?'
            r'\.height\s*=\s*(\d+),', text, re.S):
        species, sym, count, w, h = m.groups()
        if sym not in assets:
            raise SystemExit(f'gBwAnimGfx_{sym} is in a table but never declared')
        out.append(dict(species=species, stem=species.lower(), sym=sym,
                        asset=assets[sym], frames=int(count),
                        w=int(w), h=int(h)))
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
    ap.add_argument('--chunks', default='',
                    help='also re-compress at these chunk sizes and round-trip '
                         'each; slow, and only worth it when the FORMAT changed')
    ap.add_argument('--sample', type=int, default=0,
                    help='with --chunks, limit to the first N containers')
    ap.add_argument('-v', '--verbose', action='store_true')
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

    for n, e in enumerate(table):
        #  The sweep re-compresses, so it is limited to a sample. The shipped
        #  container below is checked for every entry regardless.
        sweep = chunks if (not args.sample or n < args.sample) else []
        png = repo / e['asset']
        if not png.exists():
            failures.append(f'{e["sym"]}: {png} is missing')
            continue

        flat = work / f'{e["sym"]}.4bpp'
        subprocess.run([str(gbagfx), str(png), str(flat)], check=True,
                       capture_output=True)
        raw = flat.stat().st_size
        want = e['w'] * e['h'] // 2

        #  The container the BUILD produced, which is the one that ships. Always
        #  checked, and only decoded - no re-compression - so this stays fast
        #  enough to run over a few hundred species. Re-compressing every
        #  container at four chunk sizes was fine for eleven and is twenty
        #  minutes for three hundred, which is a check nobody runs.
        shipped = repo / 'build/assets' / (e['asset'] + '.4bpp.fsmol')
        if not shipped.exists():
            failures.append(f'{e["sym"]}: {shipped} is missing - run make first')
        else:
            h = container_header(shipped)
            if h['mode'] != 7:
                failures.append(f'{e["sym"]}: shipped container is mode {h["mode"]}')
            elif h['frameSize'] != want or h['totalFrames'] != e['frames']:
                failures.append(
                    f'{e["sym"]}: shipped container says {h["totalFrames"]} x '
                    f'{h["frameSize"]} B, table says {e["frames"]} x {want} B')
            else:
                out = work / f'{e["sym"]}.shipped.4bpp'
                r = subprocess.run([str(smol), '-d', str(shipped), str(out)],
                                   capture_output=True)
                if r.returncode != 0 or not out.exists():
                    failures.append(f'{e["sym"]}: shipped container will not decode')
                elif out.read_bytes() != flat.read_bytes():
                    failures.append(f'{e["sym"]}: shipped container decodes to '
                                    f'something other than its own source art')

        #  The table says the frames are w x h and there are frameCount of them.
        #  If that disagrees with the asset, everything downstream is wrong in a
        #  way no round trip would show, because the container would be built
        #  from the same wrong number.
        if raw != want * e['frames']:
            failures.append(
                f'{e["sym"]}: table says {e["frames"]} x {e["w"]}x{e["h"]} '
                f'= {want * e["frames"]} B, asset is {raw} B')
            continue

        for k in sweep:
            out = work / f'{e["sym"]}.k{k}.fsmol'
            back = work / f'{e["sym"]}.k{k}.4bpp'
            r = subprocess.run([str(smol), '-fw', str(flat), str(out),
                                str(want), str(k)], capture_output=True)
            if r.returncode != 0 or not out.exists():
                failures.append(f'{e["sym"]} k={k}: writing failed')
                continue

            h = container_header(out)
            expect = dict(mode=7, perChunk=k, frameSize=want,
                          totalFrames=e['frames'],
                          chunks=(e['frames'] + k - 1) // k)
            for field, value in expect.items():
                if h[field] != value:
                    failures.append(f'{e["sym"]} k={k}: header {field} is '
                                    f'{h[field]}, expected {value}')
            #  Offsets have to be inside the file and strictly increasing, or a
            #  chunk decode reads whatever follows.
            last = 1
            for i, off in enumerate(h['offsets']):
                if off * 4 >= h['size'] or off <= last:
                    failures.append(f'{e["sym"]} k={k}: chunk {i} offset '
                                    f'{off} is out of order or past the end')
                    break
                last = off

            r = subprocess.run([str(smol), '-d', str(out), str(back)],
                               capture_output=True)
            if r.returncode != 0 or not back.exists():
                failures.append(f'{e["sym"]} k={k}: decoding failed')
                continue
            if back.read_bytes() != flat.read_bytes():
                failures.append(f'{e["sym"]} k={k}: round trip differs')

        if args.verbose:
            print(f'{e["sym"]:16s} {e["frames"]:3d} frames  {e["w"]}x{e["h"]}  ok')

    shutil.rmtree(work)

    print()
    print(f'{len(table)} shipped containers decoded and compared against their '
          f'source art')
    if chunks:
        print(f'  and re-compressed at chunk sizes '
              f'{", ".join(str(c) for c in chunks)}')
    if failures:
        for f in failures:
            print(f'FAIL {f}')
        print(f'\n{len(failures)} failures')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
