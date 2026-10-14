from common import *
import glob, os, json, re
def zero_regions(x, sr, mingap=0.2, minlen=0.3):
    nz = np.abs(x) > 2.0/32768
    h = int(sr*0.005); n=len(x)//h
    blk = nz[:n*h].reshape(n,h).any(axis=1)
    segs=[]; i=0
    while i<n:
        if blk[i]:
            j=i
            while j<n:
                # end if a gap of zeros >= mingap
                if not blk[j]:
                    k=j
                    while k<n and not blk[k]: k+=1
                    if (k-j)*0.005 >= mingap or k==n: break
                    j=k
                else: j+=1
            segs.append((i*h, j*h)); i=j
        else: i+=1
    return [s for s in segs if (s[1]-s[0])/sr >= minlen]

def parse(fn):
    b = os.path.basename(fn)
    m = re.match(r'Violin\.(arco|pizz)\.(pp|mf|ff)\.sul([GDAE])\.([A-G]b?\d)([A-G]b?\d)?\.', b)
    art,dyn,st,lo,hi = m.groups(); lo=name2midi(lo); hi=name2midi(hi) if hi else lo
    return art,dyn,st,lo,hi

if __name__=='__main__':
    res={}
    for f in sorted(glob.glob('/home/claude/raw/old/*.aiff')):
        art,dyn,st,lo,hi = parse(f); x,sr = load_mono(f)
        s = zero_regions(x,sr)
        if len(s)!=hi-lo+1: print('count',os.path.basename(f),'exp',hi-lo+1,'got',len(s))
        res[os.path.basename(f)] = dict(art=art,dyn=dyn,string=st,lo=lo,hi=hi,segs=s)
    json.dump(res, open('segs_old.json','w'))
    print(sum(len(v['segs']) for v in res.values()),'segments')
