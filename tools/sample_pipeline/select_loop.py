# Stage 2: loop points, level matching, best-string selection, packing.
from common import *
import json, os, sys, subprocess, pickle
SR = 44100
OPEN = {'G':55,'D':62,'A':69,'E':76}
cands = json.load(open('cands.json'))

def env_db(x, win=0.01):
    e,h = rms_env(x, SR, win); return 20*np.log10(e), h

def ncc(a, b):
    a = a - a.mean(); b = b - b.mean()
    return float(np.dot(a,b) / (np.sqrt(np.dot(a,a)*np.dot(b,b)) + 1e-12))

def process_arco(x):
    """Returns dict(audio, loopStart, loopEnd, legatoStart, quality, sustain) or None."""
    db, h = env_db(x)
    n = len(db)
    mid = db[int(n*0.25):int(n*0.75)]
    M = np.median(mid)
    above = np.where(db >= M-3)[0]
    if len(above) < 10: return None
    A = int(min(max(above[0]*h + 0.1*SR, 0.15*SR), len(x)*0.4))
    R = int(np.where(db >= M-5)[0][-1]*h - 0.2*SR)
    if R - A < 0.7*SR: return None
    y = x.copy()
    # gentle level flattening of the sustain (keep ~30% of the natural movement)
    e = np.sqrt(np.convolve(y**2, np.ones(int(0.3*SR))/int(0.3*SR), mode='same')+1e-12)
    tgt = 10**(M/20)
    g = (tgt/np.maximum(e, tgt*0.05))**0.7
    w = np.zeros(len(y)); ramp = int(0.15*SR)
    w[A:R] = 1; w[max(0,A-ramp):A] = np.linspace(0,1,A-max(0,A-ramp))
    g = 1 + w*(g-1)
    y = y*g
    # loop end as late as allowed (store at most ~3.6 s), loop length >= 0.6 s
    emax = min(R, int(3.6*SR)); W = 2048; hw = W//2
    from scipy.signal import fftconvolve
    best = (-9, 0, 0, 0)
    for e_ in range(emax - int(0.5*SR), emax, 512):
        if e_ - A < 0.6*SR: continue
        we = y[e_-hw:e_+hw]
        if len(we) < W or A - hw < 0: continue
        we0 = we - we.mean(); ne = np.sqrt(np.dot(we0,we0))+1e-12
        lo = A - hw; hi = e_ - int(0.6*SR) + hw
        Y = y[lo:hi]
        if len(Y) <= W: continue
        dots = fftconvolve(Y, we0[::-1], mode='valid')          # sum(Y[k:k+W]*we0)
        cs = np.concatenate([[0], np.cumsum(Y)]); cs2 = np.concatenate([[0], np.cumsum(Y*Y)])
        sm = cs[W:] - cs[:-W]; sq = cs2[W:] - cs2[:-W]
        var = np.maximum(sq - sm*sm/W, 1e-12)
        c = dots / (np.sqrt(var)*ne)
        lvl_s = np.sqrt(sq/W)+1e-12; lvl_e = np.sqrt(np.mean(we**2))+1e-12
        sidx = np.arange(len(c)) + A                 # loop start candidate (window centre)
        L = (e_ - sidx)/SR
        sc = c - 0.15*np.abs(20*np.log10(lvl_e/lvl_s)) + 0.08*np.minimum(L, 2.0)
        k = int(np.argmax(sc))
        if sc[k] > best[0]: best = (float(sc[k]), int(sidx[k]), e_, float(c[k]))
    _, s_, e_, bc = best
    if e_ == 0: return None
    L = e_ - s_
    # level match across the loop: slow dB ramp so the end level equals the start level
    lvl = lambda p: np.sqrt(np.mean(y[p-hw:p+hw]**2))
    ddb = 20*np.log10(lvl(s_)/lvl(e_))
    ramp = np.ones(len(y)); ramp[s_:e_] = 10**(np.linspace(0, ddb, L)/20)
    y[:e_] *= ramp[:e_]
    # bake an energy-preserving crossfade before the loop end
    X = int(min(0.12*SR, 0.3*L))
    t = (np.arange(X)+1)/X
    a = np.cos(t*np.pi/2)**2; b = np.sin(t*np.pi/2)**2  # linear-sum shape
    cc = max(0.0, bc)
    gn = 1/np.sqrt(a*a + b*b + 2*a*b*cc)
    y[e_-X:e_] = (a*y[e_-X:e_] + b*y[s_-X:s_]) * gn
    y = y[:e_]
    legato = int(min(A, s_))
    return dict(audio=y, loopStart=int(s_), loopEnd=int(e_), legatoStart=legato, quality=bc, sustain=(e_-A)/SR)

def process_pizz(x, midi):
    db, h = env_db(x, 0.005)
    pk = db.max(); maxlen = np.clip(2.6 - (midi-55)*0.035, 0.9, 2.6)
    end = np.where(db > pk-60)[0][-1]*h
    end = int(min(end, maxlen*SR, len(x)))
    y = x[:end].copy()
    f = int(0.04*SR); y[-f:] *= np.linspace(1,0,f)**2
    return dict(audio=y, loopStart=-1, loopEnd=-1, legatoStart=0, quality=1.0, sustain=end/SR)

def loudness(y, a, b):
    seg = y[a:b]; return np.sqrt(np.mean(seg**2)+1e-20)

results = []
for i, c in enumerate(cands):
    if c['art']=='arco' and not (55 <= c['midi'] <= 100): continue
    if c['art']=='pizz' and not (55 <= c['midi'] <= 96): continue
    if c['midi'] < OPEN[c['string']]: continue          # impossible on that string = misdetection
    x = np.load(c['path']).astype(np.float64)
    if c['art']=='arco':
        if c['stab'] < 0.5 or c['dur'] < 1.2: continue
        r = process_arco(x)
        if r is None: continue
        lv = loudness(r['audio'], r['loopStart'], r['loopEnd'])
    else:
        r = process_pizz(x, c['midi'])
        lv = loudness(r['audio'], 0, int(0.15*SR))
    r['gain'] = 0.2/lv if c['art']=='arco' else 0.25/lv
    r.update({k:c[k] for k in ('src','file','art','dyn','string','midi','f0','dev','stab','vib')})
    d = c['midi'] - OPEN[c['string']]
    pref = 0.0 if d <= 7 else -0.12*(d-7)
    score = -abs(c['dev'])/30 + 1.5*c['stab'] + 2.0*r['quality'] + 0.4*min(r['sustain'],3)/3 + max(pref,-2)
    if c['art']=='arco' and c['vib'] > 45: score -= (c['vib']-45)/30
    if c['src']=='2012': score += 0.3       # newer 24-bit recordings
    r['score'] = float(score)
    results.append(r)
    if i % 25 == 0: print(i, len(results), flush=True)

pickle.dump(results, open('processed.pkl','wb'))
print('processed', len(results))
