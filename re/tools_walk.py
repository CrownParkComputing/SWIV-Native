import struct,sys
sys.path.insert(0,'/home/jon/swiv-amiga-re/tools'); import map as m
d=m.Disk('/home/jon/swiv-amiga-re/SWIVFIX.ADF')
fast=open(sys.argv[1] if len(sys.argv)>1 else 'trace/fast_2700.bin','rb').read(); chip=open(sys.argv[2] if len(sys.argv)>2 else 'trace/chip_2700.bin','rb').read()
A6=0x2016DC
def mem(a,n):
    if a>=0x200000: return fast[a-0x200000:a-0x200000+n]
    return chip[a:a+n]
def L(a): return struct.unpack('>I',mem(a,4))[0]
def W(a): return struct.unpack('>h',mem(a,2))[0]
def UW(a): return struct.unpack('>H',mem(a,2))[0]
def By(a): return mem(a,1)[0]
hn={}
for line in open('/home/jon/SWIV-Native/re/handlers.txt'):
    p=line.split(); hn[int(p[2].split('=')[1],16)]=p[0]
def walk(head,verbose=True):
    out=[]
    n=L(head); cnt=0
    while n!=head and cnt<200:
        sp=L(n+14); pc=L(sp+2) if sp else 0
        gfx=UW(n+368); name=d.order[gfx&0x1ff] if (gfx&0x1ff)<len(d.order) else '?'
        rec=dict(obj=n,prio=UW(n+274),pc=pc,gfx=gfx,name=name,x=W(n+320),y=W(n+324),z=W(n+328),vx=L(n+332),vy=L(n+336),hp=W(n+360),type=W(n+276),f367=By(n+367),f397=By(n+397),ang=UW(n+358),parent=L(n+308))
        out.append(rec)
        if verbose: print('obj %06x prio %3d pc %06x gfx %04x %-12s#%-2d x=%5d y=%5d z=%5d vx=%08x vy=%08x hp=%3d type=%2d f367=%02x ang=%04x par=%06x'%(
            n,rec['prio'],pc,gfx,name,gfx>>9,rec['x'],rec['y'],rec['z'],rec['vx']&0xffffffff,rec['vy']&0xffffffff,rec['hp'],rec['type'],rec['f367'],rec['ang'],rec['parent']))
        n=L(n); cnt+=1
    return out
if __name__=='__main__':
    for head in (A6-390,A6-698,A6-1006):
        print('--- list at %x'%head); walk(head)
    print('-66=',UW(A6-66),'-72=',L(A6-72),'3530=%04x 3542=%04x'%(UW(A6+3530),UW(A6+3542)))
