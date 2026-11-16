# Dolphin for Game Vault: builds Data/Sys/GameVault/wii-controls.txt from GameTDB's wiitdb.xml
# (https://www.gametdb.com/wiitdb.zip?LANG=ORIG&WIIWARE=1). One line per Wii game: "<ID6> <pick>".
# Pick, in this order: C = Classic Controller, G = GameCube controller, N = Wii Remote + Nunchuk, W = Wii Remote alone.
import sys, xml.etree.ElementTree as ET

def main(src, dst):
    out = []
    for g in ET.parse(src).getroot().iter('game'):
        gid = (g.findtext('id') or '').strip()
        if len(gid) != 6 or (g.findtext('type') or '').lower() == 'gamecube':
            continue
        kinds = {c.get('type') for c in g.iter('control')}
        if not kinds:
            continue
        pick = 'C' if 'classiccontroller' in kinds else 'G' if 'gamecube' in kinds else \
               'N' if 'nunchuk' in kinds else 'W'
        out.append(f'{gid} {pick}')
    with open(dst, 'w', newline='\n') as f:
        f.write('# Controller Game Vault picks for each Wii game. Data: GameTDB (https://www.gametdb.com)\n')
        f.write('\n'.join(sorted(set(out))) + '\n')
    print(len(out), 'games')

main(sys.argv[1], sys.argv[2])
