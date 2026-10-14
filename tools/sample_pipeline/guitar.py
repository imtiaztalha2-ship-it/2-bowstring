# Simulated Stratocaster (neck single-coil) playing a monophonic melody with picks, hammer-ons,
# bends, slides and vibrato.  Karplus-Strong string with time-varying length.
import numpy as np, soundfile as sf, json
SR = 48000
def m2f(m): return 440*2**((m-69)/12)

# events: (time, kind, args)  kinds: pick(midi, strength) | hammer(midi) | bend(cents_to, dur) | slide(midi, dur) | vib(depth_c, rate, dur) | mute
def melody():
    ev=[]; t=0.5
    def pick(m,s,d): nonlocal t; ev.append((t,'pick',m,s)); t+=d
    def hammer(m,d): nonlocal t; ev.append((t,'hammer',m)); t+=d
    def mute(gap=0.0): nonlocal t; ev.append((t,'mute')); t+=gap
    def bend(c,dur,hold): nonlocal t; ev.append((t,'bend',c,dur)); t+=hold
    def slide(m,dur,hold): nonlocal t; ev.append((t,'slide',m,dur)); t+=hold
    def vib(depth,rate,dur): ev.append((t,'vib',depth,rate,dur))
    # phrase 1: lyrical opening
    pick(64,0.6,0.9); pick(67,0.5,0.45); vib(25,5.5,1.4); pick(69,0.8,1.5)
    pick(71,0.6,0.45); hammer(72,0.45); pick(74,0.9,0.35); bend(200,0.25,1.0); bend(0,0.3,0.5); mute(0.25)
    # phrase 2: slide up and a long held note (tests the loops)
    pick(69,0.7,0.5); slide(74,0.22,0.5); slide(76,0.2,0.2); vib(30,5.2,4.6); t+=4.6; mute(0.3)
    # phrase 3: re-picked notes (bow changes) and a fast run
    pick(76,0.9,0.45); pick(76,0.6,0.45); pick(74,0.7,0.22); pick(72,0.7,0.22); pick(71,0.7,0.22); pick(69,0.8,0.9); mute(0.35)
    # phrase 4: soft low note swelling (pp layer), then a hard accent (ff layer)
    pick(67,0.2,2.4); mute(0.2); pick(79,1.0,1.6); mute(0.4)
    # phrase 5: meend - slow slides between notes, raga style
    pick(72,0.6,0.7); slide(74,0.45,0.5); slide(76,0.5,0.8); slide(74,0.4,0.4); slide(72,0.6,1.2); vib(18,4.5,1.0); t+=1.0; mute(0.5)
    return ev, t+0.5

def render(ev, total):
    n = int(total*SR); out = np.zeros(n)
    maxd = int(SR/60)+4; buf = np.zeros(maxd); w = 0
    pitch = np.full(n, np.nan); expected = np.full(n, np.nan)
    # build control curves: base midi, bend cents, vibrato cents, damping
    midi = np.full(n, np.nan); cents = np.zeros(n); damp = np.zeros(n)
    exc = np.zeros(n)
    cur = None
    rng = np.random.default_rng(3)
    evs = sorted(ev, key=lambda e: e[0])
    for e in evs:
        i = int(e[0]*SR)
        if e[1]=='pick':
            midi[i:] = e[2]; cents[i:] = 0; damp[i:] = 0
            L = int(SR/m2f(e[2]))
            burst = rng.uniform(-1,1,L)
            # pick strength: brighter + louder
            a = np.exp(-2*np.pi*(1500+4000*e[3])/SR); y=0; fb=[]
            for v in burst: y = v + (y-v)*a; fb.append(y)
            fb = np.array(fb); fb -= fb.mean()
            exc[i:i+L] += fb*(0.25+0.75*e[3])
        elif e[1]=='hammer':
            midi[i:] = e[2]; cents[i:] = 0
            exc[i:i+60] += rng.uniform(-1,1,60)*0.05
        elif e[1]=='slide':
            j = int(e[3]*SR); m0 = midi[i-1]
            midi[i:i+j] = m0 + (e[2]-m0)*np.linspace(0,1,j); midi[i+j:] = e[2]
        elif e[1]=='bend':
            j = int(e[3]*SR); c0 = cents[i-1]
            cents[i:i+j] = c0 + (e[2]-c0)*np.linspace(0,1,j); cents[i+j:] = e[2]
        elif e[1]=='mute':
            damp[i:] = 1; midi[i+int(0.05*SR):] = np.nan
    for e in evs:
        if e[1]=='vib':
            i=int(e[0]*SR); j=int(e[4]*SR); tt=np.arange(j)/SR
            ramp = np.minimum(1, tt/0.4)
            cents[i:i+j] += e[2]*ramp*np.sin(2*np.pi*e[3]*tt)
    # string loop
    lp = 0.0; dc = 0.0
    m_last = 64.0
    for k in range(n):
        m = midi[k] if not np.isnan(midi[k]) else m_last
        m_last = m
        f = m2f(m)*2**(cents[k]/1200)
        d = SR/f - 0.5
        rp = w - d
        if rp < 0: rp += maxd
        i0 = int(rp); fr = rp-i0
        y = buf[i0] + (buf[(i0+1)%maxd]-buf[i0])*fr
        g = 0.99995 if damp[k]==0 else 0.95
        lp = 0.85*y + 0.15*lp
        buf[w] = lp*g + exc[k]
        w = (w+1)%maxd
        out[k] = y
        if damp[k]==0 and not np.isnan(midi[k]): expected[k] = f
    # pickup: neck single-coil comb + resonant lowpass ~4.5 kHz, tone knob rolled back slightly
    from scipy.signal import lfilter, butter
    pk = int(SR/m2f(64)*0.25)
    out = out - 0.5*np.concatenate([np.zeros(pk), out[:-pk]])
    b,a = butter(2, 4000/(SR/2)); out = lfilter(b,a,out)
    out += rng.normal(0, 10**(-78/20), n)  # hum/hiss floor
    out *= 0.35/np.abs(out).max()
    return out, expected

ev, total = melody()
x, expf = render(ev, total)
sf.write('guitar_in.wav', np.stack([x,x],1), SR, subtype='PCM_24')
np.save('expected_hz.npy', expf)
print('guitar', round(total,1), 's')
