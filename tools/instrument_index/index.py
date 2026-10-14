import json, yaml, re, zlib, sys, os, posixpath
from urllib.parse import urlparse
LIBS, OUT = sys.argv[1], sys.argv[2]
progs = json.load(open(os.path.join(LIBS, 'programs.json')))
avg = json.load(open(os.path.join(LIBS, 'avgsize.json')))
yml = yaml.safe_load(open(os.path.join(LIBS, 'site/data/sfz/instruments.yml')))
def norm(s): return re.sub(r'[^a-z0-9]', '', s.lower())
meta = {}
for cat in yml:
    for i in cat.get('instruments', []):
        meta[norm(i['name'])] = (i, cat['name'])
REPO_NAME = {  # repo -> yml name
 'sfzinstruments/EthanWiner.Soundfonts': 'Ethan Winer Collection', 'sfzinstruments/FlameStudios.Kay5StringBanjo': 'Kay 5-String Banjo',
 'sfzinstruments/GTownChurchSamplingProject': 'G-Town Church Sampling Project', 'sfzinstruments/GregSullivan.E-Pianos': 'Greg Sullivan E-Pianos',
 'sfzinstruments/Ixox.Flute': 'Ixox Flute', 'sfzinstruments/Karoryfer.HorsePulse': 'Horse Pulse', 'sfzinstruments/MTG.SoloSax': 'MTG Solo Sax',
 'sfzinstruments/MatsHelgesson.MaestroConcertGrandPiano': 'Maestro Concert Grand Piano', 'sfzinstruments/Project16Rickenbacker4001': 'Rickenbacker 4001',
 'sfzinstruments/SMDrums': 'SM Drums', 'sfzinstruments/SalamanderGrandPiano': 'Salamander Grand Piano', 'sfzinstruments/SamsSonor': "Sam's Sonor",
 'sfzinstruments/SplendidGrandPiano': 'Splendid Grand Piano', 'sfzinstruments/Terkelsen.Marimba': "Terkelsen's Marimba", 'sfzinstruments/WilkinsonAudio.NakedDrums': 'Naked Drums',
 'sfzinstruments/dsmolken.double-bass': 'D. Smolken Double Bass', 'sfzinstruments/ganjo': 'ganjo', 'sfzinstruments/jlearman.jRhodes3c': 'jRhodes3c', 'sfzinstruments/jlearman.jRhodes3d': 'jRhodes3d',
 'sfzinstruments/DrumGizmo.MuldjordKit': 'Muldjord Kit', 'sfzinstruments/virtuosity_drums': 'Virtuosity Drums', 'sfzinstruments/karoryfer.ergo': 'Ergo electric upright bass',
 'sfzinstruments/karoryfer.272-merry-orks': '272 Merry Orks', 'sfzinstruments/karoryfer.black-and-green-guitars': 'Black_And_Green_Guitars',
}
# words people type -> added to an entry's search words
SYN = {
 'cello': 'cello violoncello violoncelo celo cellos strings string streicher cordas', 'celli': 'cello cellos celli section ensemble violoncello violoncelo strings orchestra',
 'violin': 'violin violino geige violon strings', 'violins': 'violin violins section ensemble violino geige strings orchestra',
 'viola': 'viola bratsche alto strings', 'violas': 'viola violas section ensemble bratsche strings orchestra',
 'bass': 'bass contrabass double bass contrabaixo kontrabass baixo', 'basses': 'bass basses double bass section ensemble contrabaixo kontrabass orchestra',
 'strings': 'strings string ensemble orchestra cordas streicher', 'flute': 'flute flauta flote flöte flauto woodwind', 'flutes': 'flute flutes section ensemble flauta woodwind',
 'piccolo': 'piccolo flute flautim', 'oboe': 'oboe oboé woodwind', 'oboes': 'oboe oboes section woodwind', 'clarinet': 'clarinet clarinete klarinette woodwind',
 'clarinets': 'clarinet clarinets section woodwind', 'bassoon': 'bassoon fagote fagott woodwind', 'bassoons': 'bassoon bassoons section woodwind', 'contrabassoon': 'contrabassoon bassoon woodwind',
 'horn': 'horn french horn trompa horn brass', 'horns': 'horn horns french horn section brass', 'trumpet': 'trumpet trompete brass', 'trumpets': 'trumpet trumpets section brass',
 'trombone': 'trombone posaune brass', 'trombones': 'trombone trombones section brass', 'tuba': 'tuba brass', 'harp': 'harp harpa harfe', 'piano': 'piano klavier flügel grand keys',
 'organ': 'organ orgel órgão keys church', 'harpsichord': 'harpsichord cravo cembalo keys baroque', 'chorus': 'choir chorus coro chor voices vocal', 'choir': 'choir chorus coro chor voices',
 'timpani': 'timpani pauken tímpanos percussion', 'marimba': 'marimba mallets percussion', 'xylophone': 'xylophone xilofone mallets', 'vibraphone': 'vibraphone vibes mallets',
 'glockenspiel': 'glockenspiel mallets bells', 'celeste': 'celeste celesta keys bells', 'chimes': 'chimes tubular bells', 'drum': 'drums drum kit bateria schlagzeug percussion',
 'drums': 'drums drum kit bateria schlagzeug percussion', 'snare': 'snare drum caixa percussion', 'guitar': 'guitar guitarra gitarre', 'guitars': 'guitar guitarra gitarre', 'sax': 'sax saxophone saxofone saxophon woodwind',
 'saxophone': 'sax saxophone saxofone', 'rhodes': 'rhodes electric piano epiano keys', 'banjo': 'banjo folk', 'synth': 'synth synthesizer', 'tin': 'whistle'
}
lines = []
total = 0
for r in progs:
    repo, br = r['repo'], r['branch']
    P = [p for p in r['programs'] if p['missing'] == 0 and p['samples']]
    if not P or repo not in avg: continue
    a = avg[repo]
    if repo == 'peastman/sso':
        lib, author, lic, cat0, site, desc = 'Sonatina Symphonic Orchestra', 'Mattias Westlund, Peter Eastman', 'CC Sampling Plus 1.0', 'Orchestra', 'github.com/peastman/sso', ''
    else:
        nm = REPO_NAME.get(repo)
        key = norm(nm) if nm else norm(repo.split('/')[1].replace('karoryfer.', ''))
        if key not in meta: print('no meta', repo, file=sys.stderr); continue
        i, cat0 = meta[key]
        lib, author, lic = i['name'].replace('_', ' '), str(i.get('author') or ''), str(i.get('license') or 'free')
        site = urlparse(str(i.get('url') or 'https://github.com/' + repo)).netloc.replace('www.', '') or 'github.com'
        desc = re.sub(r'<[^>]+>', '', ' '.join(str(i.get('short_description') or '').split()))
    files = sorted(set(f for p in P for f in p['files'] + p['samples']))
    idx = {f: k for k, f in enumerate(files)}
    lines.append('L|%s|%s|%s|%s|%s|%s|%s|%s' % (repo, br, lib, author, lic, cat0, site, desc[:160].replace('|', '/')))
    for f in files: lines.append('F|' + f)
    for p in P:
        prog = p['program']; base = posixpath.basename(prog)[:-4]
        cat = cat0
        if repo == 'peastman/sso':
            folder = prog.split('/')[1]
            cat = folder.split(' - ')[0] if ' - ' in folder else ('Keys' if folder in ('Grand Piano', 'Harpsichord', 'Organ') else ('Strings' if 'Harp' in base else folder))
            if cat == 'Percussion': cat = 'Percussion'
            m = re.search(r'\b(Sustain|Legato|Marcato|Staccato|Pizzicato|Col Legno|Tremolo|Harmonics|Spiccato|Hits|Rolls|Trill|Flutter|Sforzando|Swell|Muted|Tongued|Fluttertongue)\b.*$', base)
            art = m.group(0) if m else ''
            name = base[:m.start()].strip() if m else base
            ms = re.match(r'(.*) Solo( \d)?$', name)
            if ms: name = 'Solo ' + ms.group(1) + (ms.group(2) or '')
            name = name.replace('Chrous', 'Chorus')
            if name.startswith('Celli'): name = 'Celli (cello section)'
            if not art: art = ''
        else:
            name = lib
            art = re.sub(r'^[\d_\-\. ]+', '', base).replace('_', ' ').replace('-', ' ').strip()
            art = re.sub(r'\s+', ' ', art)
            if norm(art) == norm(lib): art = ''
        words = (name + ' ' + art + ' ' + lib + ' ' + cat + ' ' + author).lower()
        extra = set()
        for w in re.findall(r'[a-zà-ÿ]+', words):
            if w in SYN: extra.update(SYN[w].split())
        tags = ' '.join(sorted(extra))
        mb = max(1, round(len(p['samples']) * a / 1e6))
        if mb > 700: continue                                   # too big for an iPad's memory (it is played from memory)
        if len(p['samples']) < 4 and cat not in ('Percussion', 'Misc', 'Drums', 'Drum Machines'): continue
        fl = ','.join(str(idx[f]) for f in [prog] + [f for f in p['files'] if f != prog] + p['samples'])
        lines.append('P|%s|%s|%s|%s|%d|%d|%s' % (name, art, cat, tags, mb, len(p['samples']), fl))
        total += 1
text = '\n'.join(lines).encode('utf-8')
z = zlib.compress(text, 9)
with open(OUT, 'w') as o:
    o.write('// generated by tools/instrument_index.py from github.com/sfzinstruments (sfzinstruments.github.io) and github.com/peastman/sso - %d programs\n' % total)
    o.write('#include "InstrumentIndex.h"\nnamespace arco {\nstatic const unsigned char indexData[] = {\n')
    for k in range(0, len(z), 24): o.write(','.join(str(b) for b in z[k:k+24]) + ',\n')
    o.write('};\nconst unsigned char* instrumentIndexData(size_t& n) { n = sizeof(indexData); return indexData; }\nsize_t instrumentIndexRawSize() { return %d; }\n} // namespace arco\n' % len(text))
print(total, 'programs', len(text), 'bytes raw', len(z), 'zlib', file=sys.stderr)
