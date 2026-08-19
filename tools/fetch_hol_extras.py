#!/usr/bin/env python3
"""Fetch the Hall of Light media for the EXTRAS screen into extras/hol/ (screenshots, box, disk,
misc scans, cheats).  Solves HOL's Anubis proof-of-work challenge.  Run from the repo root."""
import re, os, json, hashlib, urllib.request, urllib.parse, http.cookiejar, html as H
from PIL import Image
UA = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/124 Safari/537.36"
base = "https://amiga.abime.net"; page = base + "/games/view/swiv"
cj = http.cookiejar.CookieJar(); op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj)); op.addheaders = [('User-Agent', UA), ('Referer', page)]
h = op.open(page).read().decode(errors='ignore')
m = re.search(r'id="anubis_challenge" type="application/json">(.*?)</script>', h, re.S)
if m:
    ch = json.loads(m.group(1)); rd = ch['challenge']['randomData']; diff = ch['rules']['difficulty']; n = 0
    while not hashlib.sha256((rd + str(n)).encode()).hexdigest().startswith('0' * diff): n += 1
    hh = hashlib.sha256((rd + str(n)).encode()).hexdigest()
    op.open(base + "/.within.website/x/cmd/anubis/api/pass-challenge?" + urllib.parse.urlencode({'id': ch['challenge']['id'], 'response': hh, 'nonce': n, 'redir': page, 'elapsedTime': 300})).read()
    h = op.open(page).read().decode(errors='ignore')
os.makedirs("extras/hol", exist_ok=True)
paths = sorted(set(re.findall(r'/(?:screen|box|disk|misc)/2201-2300/[^"<> ]+', h)))
for p in paths:
    d = p.split('/')[1]; os.makedirs("extras/hol/" + d, exist_ok=True)
    fn = os.path.basename(p.split('?')[0]); dst = os.path.join("extras/hol", d, fn)
    data = op.open(base + p).read()
    if not data: continue
    open(dst, 'wb').write(data)
    if dst.endswith('.jpg'):   # raylib has no JPG decoder
        im = Image.open(dst).convert('RGB')
        if im.width > 1600: im = im.resize((1600, int(im.height * 1600 / im.width)))
        im.save(dst[:-4] + '.png'); os.remove(dst)
m = re.search(r'<div id="cheat_list">(.*?)<div id="(?:trivia|scans|conversions|links|maps)', h, re.S)
if m:
    t = re.sub(r'<br\s*/?>', '\n', m.group(1)); t = re.sub(r'</(p|li|h\d|div)>', '\n', t); t = re.sub(r'<[^>]+>', '', t); t = H.unescape(t)
    t = '\n'.join(l.strip() for l in t.splitlines() if l.strip()).split('Conversion info')[0].strip()
    open("extras/hol/cheats.txt", 'w').write(t + '\n')
print("fetched", len(paths), "files into extras/hol/")
