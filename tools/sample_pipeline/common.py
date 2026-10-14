import numpy as np, soundfile as sf, re
NOTE = {'C':0,'Db':1,'D':2,'Eb':3,'E':4,'F':5,'Gb':6,'G':7,'Ab':8,'A':9,'Bb':10,'B':11}
NAMES = ['C','Db','D','Eb','E','F','Gb','G','Ab','A','Bb','B']
def name2midi(n):
    m = re.fullmatch(r'([A-G]b?)(\d)', n); return NOTE[m.group(1)] + 12*(int(m.group(2))+1)
def midi2name(m): return f"{NAMES[m%12]}{m//12-1}"
def hz2midi(f): return 69+12*np.log2(f/440.0)
def midi2hz(m): return 440.0*2**((m-69)/12)

def load_mono(path):
    x, sr = sf.read(path, dtype='float64', always_2d=True)
    return x.mean(axis=1), sr

def rms_env(x, sr, win=0.02):
    h = int(sr*win); n = len(x)//h
    return np.sqrt(np.mean(x[:n*h].reshape(n,h)**2, axis=1)+1e-20), h

def yin_frame(fr, sr, fmin=150, fmax=4200, thr=0.12):
    W = len(fr)//2
    tmax = min(int(sr/fmin), W-1); tmin = max(2,int(sr/fmax))
    # difference function via FFT
    x = fr
    n = len(x); N = 1<<int(np.ceil(np.log2(2*n)))
    X = np.fft.rfft(x, N); r = np.fft.irfft(X*np.conj(X), N)[:tmax+1]
    cs = np.concatenate([[0],np.cumsum(x*x)])
    e0 = cs[W]  # energy of first window
    tau = np.arange(tmax+1)
    # energy of window starting at tau, length W
    et = cs[tau+W]-cs[tau]
    # cross term for window W: approximate with full corr restricted (use direct for accuracy)
    rr = np.array([np.dot(x[:W], x[t:t+W]) for t in tau]) if False else None
    # exact: use correlation of first-W segment with whole
    Y = np.fft.rfft(x[:W], N); c = np.fft.irfft(np.conj(Y)*X, N)[:tmax+1]
    d = e0 + et - 2*c
    d[0]=0
    cm = np.ones_like(d); run = np.cumsum(d[1:]); cm[1:] = d[1:]*tau[1:]/np.maximum(run,1e-20)
    idx = np.where(cm[tmin:tmax] < thr)[0]
    if len(idx)==0:
        t = tmin+int(np.argmin(cm[tmin:tmax])); conf = cm[t]
        if conf > 0.35: return 0.0, conf
    else:
        t = tmin+idx[0]
        while t+1 < tmax and cm[t+1] < cm[t]: t += 1
        conf = cm[t]
    s0,s1,s2 = cm[t-1],cm[t],cm[t+1]; den = s0-2*s1+s2
    sh = 0.5*(s0-s2)/den if abs(den)>1e-12 else 0
    return sr/(t+np.clip(sh,-1,1)), conf

def pitch_track(x, sr, hop=0.01, win=0.06, fmin=150, fmax=4200):
    W = int(sr*win); h = int(sr*hop); out=[]
    for i in range(0, len(x)-2*W, h):
        f,c = yin_frame(x[i:i+2*W], sr, fmin, fmax); out.append((i, f, c))
    return out
