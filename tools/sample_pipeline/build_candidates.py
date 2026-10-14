# Slice all recordings into single-note candidates, verify pitch, trim.
from common import *
from segment import zero_regions, parse
import glob, os, json, re, pickle
OUT = '/home/claude/proc/cand'; os.makedirs(OUT, exist_ok=True)

def harmonic_f0(x, sr, flo, fhi):
    n = len(x); w = np.hanning(n); N = 1<<18
    X = np.abs(np.fft.rfft(x*w, N)); L = 20*np.log10(X+1e-9); L -= L.max(); L = np.maximum(L, -60)
    fr = sr/N
    cands = flo*2**(np.arange(0, 1200*np.log2(fhi/flo), 4)/1200)
    best=(-1e9,0)
    for f in cands:
        hs = np.arange(1,9); idx = (hs*f/fr).astype(int); idx = idx[idx < len(L)-1]
        mid = ((hs-0.5)*f/fr).astype(int); mid = mid[mid < len(L)-1]
        s = np.mean(L[idx]) - 0.5*np.mean(L[mid])
        if s>best[0]: best=(s,f)
    f=best[1]
    # refine: peak near harmonics 1..4 using quadratic interp
    ests=[]
    for h in range(1,5):
        k = int(round(h*f/fr)); r = int(0.03*h*f/fr)+2
        lo,hi = max(1,k-r), min(len(X)-2,k+r)
        j = lo+int(np.argmax(X[lo:hi])); a,b,c = np.log(X[j-1:j+2]+1e-12)
        p = 0.5*(a-c)/(a-2*b+c) if (a-2*b+c)!=0 else 0
        ests.append(((j+p)*fr/h, X[j]))
    ests = [e for e in ests if abs(1200*np.log2(e[0]/f))<40]
    if ests: f = sum(e[0]*e[1] for e in ests)/sum(e[1] for e in ests)
    return f

def body_bounds(seg, sr, rel_on=-30, rel_off=-45):
    e,h = rms_env(seg, sr, 0.005); db = 20*np.log10(e); pk = db.max()
    on = np.where(db > pk+rel_on)[0][0]
    # back up onset to where the level first rises above floor+6 dB (catch the bow/pluck start)
    floor = np.percentile(db[:max(on,1)], 50) if on>2 else db.min()
    k = on
    while k>0 and db[k-1] > max(floor+6, pk-60): k -= 1
    off = np.where(db > pk+rel_off)[0][-1]
    return k*h, min(len(seg), (off+1)*h)

def arco_pitch(seg, sr, flo, fhi):
    tr = pitch_track(seg, sr, hop=0.01, win=0.04, fmin=flo, fmax=fhi)
    t = np.array([i for i,f,c in tr]); f = np.array([f for i,f,c in tr]); c = np.array([c for i,f,c in tr])
    ok = (f>0)&(c<0.25)
    return t[ok], f[ok]

cands=[]
def add(audio, sr, meta):
    i = len(cands); fn = f'{OUT}/{i:04d}.npy'
    np.save(fn, audio.astype(np.float32)); meta['path']=fn; meta['sr']=sr; cands.append(meta)

# ---------- pre-2012 scale files ----------
segs = json.load(open('segs_old.json'))
for fn, v in sorted(segs.items()):
    x, sr = load_mono('/home/claude/raw/old/'+fn)
    art = v['art']
    flo = max(170.0, midi2hz(v['lo']-5)); fhi = min(4300.0, midi2hz(v['hi']+5))
    for (a,b) in v['segs']:
        seg = x[a:b]
        if art == 'arco':
            t, f = arco_pitch(seg, sr, flo, fhi)
            if len(f) < 30: print('reject (unvoiced)', fn, a/sr); continue
            lf = 1200*np.log2(f)
            # split if the segment holds more than one note: rolling median jumps
            k = 25; rm = np.array([np.median(lf[max(0,j-k):j+k]) for j in range(len(lf))])
            jumps = np.where(np.abs(np.diff(rm)) > 30)[0]
            parts=[]; start=0
            # group by stable plateaus of rolling median rounded to 100 cents
            lab = np.round(rm/100).astype(int)
            bounds=[0]+[j+1 for j in range(len(lab)-1) if lab[j+1]!=lab[j]]+[len(lab)]
            plates=[(bounds[q],bounds[q+1]) for q in range(len(bounds)-1) if (t[bounds[q+1]-1]-t[bounds[q]])/sr > 0.8]
            labs = sorted(set(lab[p0] for p0,_ in plates))
            if len(labs) <= 1:
                pieces=[(0,len(seg))]
            else:
                # cut between plateaus with different labels
                cuts=[]
                for q in range(len(plates)-1):
                    if lab[plates[q][0]] != lab[plates[q+1][0]]:
                        cuts.append(int((t[plates[q][1]-1]+t[plates[q+1][0]])//2))
                edges=[0]+cuts+[len(seg)]; pieces=list(zip(edges[:-1],edges[1:]))
                print('split', fn, round(a/sr,1), len(pieces))
            for (p0,p1) in pieces:
                piece = seg[p0:p1]
                if len(piece) < sr*0.8: continue
                s0, s1 = body_bounds(piece, sr)
                body = piece[s0:s1]
                tt, ff = arco_pitch(body, sr, flo, fhi)
                if len(ff) < 30: continue
                # root = median over the sustain (skip attack/release)
                dur = len(body)/sr
                sel = (tt > 0.3*sr) & (tt < len(body)-0.3*sr)
                lf = 1200*np.log2(ff[sel] if sel.sum()>20 else ff)
                root_c = np.median(lf); f0 = 2**(root_c/1200)
                stab = np.mean(np.abs(lf-root_c) < 60)
                vib = np.std(lf)
                midi = int(round(hz2midi(f0)))
                add(body, sr, dict(src='old', file=fn, art='arco', dyn=v['dyn'], string=v['string'], midi=midi,
                                   f0=float(f0), dev=float(1200*np.log2(f0/midi2hz(midi))), stab=float(stab), vib=float(vib), dur=dur))
        else:
            s0, s1 = body_bounds(seg, sr, -25, -55)
            body = seg[s0:s1]
            if len(body) < sr*0.1: continue
            on = int(0.02*sr); w = body[on:on+int(0.25*sr)]
            f0 = harmonic_f0(w, sr, flo, fhi)
            midi = int(round(hz2midi(f0)))
            add(body, sr, dict(src='old', file=fn, art='pizz', dyn=v['dyn'], string=v['string'], midi=midi,
                               f0=float(f0), dev=float(1200*np.log2(f0/midi2hz(midi))), stab=1.0, vib=0.0, dur=len(body)/sr))
    print(fn, ' '.join(midi2name(c['midi'])+'%+d'%c['dev'] for c in cands if c['file']==fn), flush=True)

# ---------- 2012 per-note stereo files (ff) ----------
for f in sorted(glob.glob('/home/claude/raw/z2012/*/*.aif')):
    b = os.path.basename(f)
    m = re.match(r'Violin\.(arco|pizz)\.ff\.sul([GDAE])\.([A-G]b?\d)\.stereo\.aif', b)
    if not m: print('skip', b); continue
    art, st, nm = m.groups(); nom = name2midi(nm)
    x, sr = load_mono(f)
    if art == 'arco':
        s0, s1 = body_bounds(x, sr); body = x[s0:s1]
        tt, ff = arco_pitch(body, sr, midi2hz(nom-3), midi2hz(nom+3))
        if len(ff) < 30: print('reject', b); continue
        sel = (tt > 0.3*sr) & (tt < len(body)-0.3*sr)
        lf = 1200*np.log2(ff[sel] if sel.sum()>20 else ff); root_c = np.median(lf); f0 = 2**(root_c/1200)
        stab = float(np.mean(np.abs(lf-root_c) < 60)); vib = float(np.std(lf))
    else:
        s0, s1 = body_bounds(x, sr, -25, -55); body = x[s0:s1]
        f0 = harmonic_f0(body[int(0.02*sr):int(0.27*sr)], sr, midi2hz(nom-3), midi2hz(nom+3)); stab=1.0; vib=0.0
    midi = int(round(hz2midi(f0))); dev = 1200*np.log2(f0/midi2hz(midi))
    if midi != nom: print('pitch mismatch', b, 'detected', midi2name(midi), '%+d'%dev)
    add(body, sr, dict(src='2012', file=b, art=art, dyn='ff', string=st, midi=midi, f0=float(f0), dev=float(dev),
                       stab=stab, vib=vib, dur=len(body)/sr, nominal=nom))
json.dump(cands, open('cands.json','w'), indent=0)
print(len(cands), 'candidates')
