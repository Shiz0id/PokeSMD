"""Export hand-tuned VARIANTS from render_gb_preview.py into a plans JSON.

The arrangements corrected by ear live in the renderer, because that is where
they were auditioned. wire_gb_song.py consumes plans JSON. Rather than
transcribe one into the other by hand -- which is exactly how Brendan ended up
wired with another song's slot numbers -- this exports them.

Usage:  python3 tools/rogue/export_tuned_plans.py --repo . --out plans.json \\
            --variants champion_fix,brendan_fix2,hiker_fix
"""
import argparse
import importlib.util
import json
import sys
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default='.', type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--variants', required=True)
    ap.add_argument('--names', help='comma-separated plan names, defaults to '
                                    'the song stem without mus_')
    args = ap.parse_args()

    spec = importlib.util.spec_from_file_location(
        'rgp', args.repo / 'tools/rogue/render_gb_preview.py')
    rgp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(rgp)

    wanted = [v.strip() for v in args.variants.split(',') if v.strip()]
    names = [n.strip() for n in args.names.split(',')] if args.names else None

    plans = {}
    if args.out.exists():
        plans = json.loads(args.out.read_text(encoding='utf-8'))

    for i, var in enumerate(wanted):
        if var not in rgp.VARIANTS:
            sys.exit('no such variant: %s' % var)
        song = rgp.VARIANT_SONG[var]
        name = names[i] if names else song.replace('mus_', '')
        voices = {}
        for k, v in rgp.VARIANTS[var].items():
            voices[str(k)] = dict(v)
        plans[name] = dict(song=song, voices=voices)
        kept = [v['name'] for v in rgp.VARIANTS[var].values()
                if v['kind'] != 'drop']
        print('%-16s -> %-28s %d parts kept' % (var, name, len(kept)))

    args.out.write_text(json.dumps(plans, indent=2), encoding='utf-8',
                        newline='\n')
    print('wrote %s (%d plans)' % (args.out, len(plans)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
