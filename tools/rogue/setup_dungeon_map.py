"""
Create the template map/layout that the runtime dungeon generator writes into.

Mirrors how the Battle Pyramid works: a real layout exists in ROM so the engine
has a MapHeader to load, and the generator then overwrites sBackupMapData.
The template content only matters as a fallback - if the hook fails to fire you
get a plain empty room instead of rooms-and-corridors, which is an obvious tell.
"""
import json, struct, shutil
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

W, H = 48, 48
FLOOR, VOID = 0x201, 0x200          # verified against LAYOUT_GRANITE_CAVE_1F
PASSABLE, IMPASSABLE = 0, 1

LAYOUT_ID = 'LAYOUT_ROGUE_DUNGEON_FLOOR'
LAYOUT_NAME = 'RogueDungeonFloor_Layout'
MAP_NAME = 'RogueDungeonFloor'
GROUP = 'gMapGroup_Rogue'


def block(mt, coll, elev):
    return (mt & 0x3FF) | ((coll << 10) & 0xC00) | ((elev << 12) & 0xF000)


def main():
    assert (W + 15) * (H + 14) <= 10240, 'exceeds MAX_MAP_DATA_SIZE'

    # --- layout binaries -------------------------------------------------
    ldir = REPO / 'data/layouts' / MAP_NAME
    ldir.mkdir(parents=True, exist_ok=True)
    (ldir / 'map.bin').write_bytes(
        struct.pack(f'<{W*H}H', *([block(FLOOR, PASSABLE, 3)] * (W * H))))
    (ldir / 'border.bin').write_bytes(
        struct.pack('<4H', *([block(VOID, IMPASSABLE, 0)] * 4)))

    # --- layouts.json ----------------------------------------------------
    lp = REPO / 'data/layouts/layouts.json'
    original = lp.read_text(encoding='utf-8')
    doc = json.loads(original)

    # fidelity check: does a plain re-dump reproduce the file byte for byte?
    roundtrip = json.dumps(doc, indent=2, ensure_ascii=False) + '\n'
    print(f'layouts.json round-trip identical: {roundtrip == original}')

    if not any(l.get('id') == LAYOUT_ID for l in doc['layouts'] if l):
        doc['layouts'].append({
            'id': LAYOUT_ID,
            'name': LAYOUT_NAME,
            'width': W,
            'height': H,
            'primary_tileset': 'gTileset_General',
            'secondary_tileset': 'gTileset_Cave',
            'border_filepath': f'data/layouts/{MAP_NAME}/border.bin',
            'blockdata_filepath': f'data/layouts/{MAP_NAME}/map.bin',
        })
        lp.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
        print(f'layouts.json: added {LAYOUT_ID} ({len(doc["layouts"])} layouts total)')

    # --- map.json --------------------------------------------------------
    mdir = REPO / 'data/maps' / MAP_NAME
    mdir.mkdir(parents=True, exist_ok=True)
    (mdir / 'map.json').write_text(json.dumps({
        'id': 'MAP_ROGUE_DUNGEON_FLOOR',
        'name': MAP_NAME,
        'layout': LAYOUT_ID,
        'music': 'MUS_PETALBURG_WOODS',
        'region': 'REGION_HOENN',
        'region_map_section': 'MAPSEC_GRANITE_CAVE',
        'requires_flash': False,
        'weather': 'WEATHER_NONE',
        'map_type': 'MAP_TYPE_UNDERGROUND',
        'allow_cycling': False,
        'allow_escaping': True,
        'allow_running': True,
        'show_map_name': True,
        'battle_scene': 'MAP_BATTLE_SCENE_NORMAL',
        'connections': None,
        'object_events': [],
        'warp_events': [],
        'coord_events': [],
        'bg_events': [],
    }, indent=2) + '\n', encoding='utf-8')
    print(f'map.json: wrote MAP_ROGUE_DUNGEON_FLOOR')

    # --- map_groups.json -------------------------------------------------
    gp = REPO / 'data/maps/map_groups.json'
    g = json.loads(gp.read_text(encoding='utf-8'))
    if GROUP not in g:
        g['group_order'].append(GROUP)
        g[GROUP] = [MAP_NAME]
        gp.write_text(json.dumps(g, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
        print(f'map_groups.json: added {GROUP} ({len(g["group_order"])} groups total)')

    print(f'\ntemplate: {W}x{H} = {(W+15)*(H+14)} blocks '
          f'({100*(W+15)*(H+14)/10240:.1f}% of MAX_MAP_DATA_SIZE)')


if __name__ == '__main__':
    main()
