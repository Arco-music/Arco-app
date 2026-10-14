# scan GitHub SFZ libraries (blobless clones) -> programs.json : per program the files it needs
import os, re, subprocess, json, sys, posixpath, concurrent.futures as cf
LIBS = sys.argv[1]
def git(d, *a, inp=None):
    return subprocess.run(['git', '-C', d] + list(a), input=inp, capture_output=True, timeout=600).stdout
def strip_comments(t):
    t = re.sub(r'/\*.*?\*/', ' ', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)
SKIPDIR = re.compile(r'(^|/)(includes?|inc|sources?|mappings?|maps|common|data|lib|_|samples?|controls?|modules?|parts?|layers?|regions?|groups?|velocity|vel|mics?|articulations?)/', re.I)
def programs(files, repo):
    sfz = [f for f in files if f.lower().endswith('.sfz')]
    if repo == 'peastman/sso':
        return [f for f in sfz if '/includes/' not in f and ' KS' not in f and 'Performance' not in f]
    top = [f for f in sfz if not SKIPDIR.search(f)]
    # programs are usually in the shallowest folders that hold sfz files
    if not top: top = sfz
    depth = min(f.count('/') for f in top)
    sel = [f for f in top if f.count('/') <= depth + (1 if depth == 0 else 0)]
    return sorted(sel)[:40]
def scan_repo(d, repo):
    files = git(d, 'ls-tree', '-r', '--name-only', 'HEAD').decode('utf-8', 'replace').splitlines()
    fileset = set(files)
    lower = {f.lower(): f for f in files}
    cache = {}
    def blob(p):
        if p not in cache:
            cache[p] = git(d, 'show', 'HEAD:' + p).decode('utf-8', 'replace')
        return cache[p]
    out = []
    for prog in programs(files, repo):
        root = posixpath.dirname(prog)
        need, samples, missing = set([prog]), set(), 0
        defines = {}
        def expand(path, depth=0):
            nonlocal missing
            if depth > 8: return ''
            t = strip_comments(blob(path))
            res = []
            for line in t.split('\n'):
                m = re.match(r'\s*#define\s+(\$\w+)\s+(.+)', line)
                if m: defines[m.group(1)] = m.group(2).strip(); continue
                m = re.match(r'\s*#include\s+"([^"]+)"', line)
                if m:
                    inc = m.group(1)
                    for k, v in defines.items(): inc = inc.replace(k, v)
                    p = posixpath.normpath(posixpath.join(root, inc.replace('\\', '/')))
                    p = lower.get(p.lower(), p)
                    if p in fileset: need.add(p); res.append(expand(p, depth + 1))
                    else: missing += 1
                    continue
                res.append(line)
            return '\n'.join(res)
        try: text = expand(prog)
        except Exception as e: continue
        for k in sorted(defines, key=len, reverse=True): text = text.replace(k, defines[k])
        dpath = ''
        for m in re.finditer(r'(<\w+>)|(\w+)=', text):
            pass
        # walk opcodes in order, tracking default_path and sample
        tokens = re.split(r'(<\w+>)', text)
        for tk in tokens:
            for m in re.finditer(r'(\w+)=(.*?)(?=\s+\w+=|\s*$|\s*<)', tk, flags=re.M):
                key, val = m.group(1), m.group(2).strip()
                if key == 'default_path': dpath = val.replace('\\', '/')
                elif key == 'sample':
                    if val.startswith('*'): continue
                    p = posixpath.normpath(posixpath.join(root, dpath, val.replace('\\', '/')))
                    p = lower.get(p.lower(), p)
                    if p in fileset: samples.add(p)
                    else: missing += 1
        if not samples: continue
        out.append({'program': prog, 'files': sorted(need), 'samples': sorted(samples), 'missing': missing})
    return {'repo': repo, 'branch': git(d, 'branch', '--show-current').decode().strip(), 'programs': out}
jobs = []
for line in open(os.path.join(LIBS, 'found.txt')):
    repo = line.split()[1]
    if repo.startswith('sgossner/'): continue
    d = os.path.join(LIBS, 'sso' if repo == 'peastman/sso' else 'r/' + repo.replace('/', '_'))
    jobs.append((d, repo))
res = []
with cf.ThreadPoolExecutor(8) as ex:
    for r in ex.map(lambda j: scan_repo(*j), jobs):
        print(r['repo'], len(r['programs']), file=sys.stderr); res.append(r)
json.dump(res, open(os.path.join(LIBS, 'programs.json'), 'w'))
