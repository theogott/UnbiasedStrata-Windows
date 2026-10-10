import pathlib,struct,hashlib,json,argparse
parser=argparse.ArgumentParser(description='Verify unchanged weights in the compatible first shard.')
parser.add_argument('original_directory',type=pathlib.Path)
parser.add_argument('compatible_directory',type=pathlib.Path)
args=parser.parse_args()
name='DeepSeek-V4.1-Flash-Q3_K_M-00001-of-00009.gguf'
original=args.original_directory/name
adapted=args.compatible_directory/name
sizes={0:1,1:1,2:2,3:2,4:4,5:4,6:4,7:1,10:8,11:8,12:8}
def n(f,t):return struct.unpack('<'+t,f.read(struct.calcsize(t)))[0]
def skip(f,t):
    if t==8:f.seek(n(f,'Q'),1)
    elif t==9:
        sub=n(f,'I'); count=n(f,'Q')
        if sub in sizes:f.seek(count*sizes[sub],1)
        else:
            for _ in range(count):skip(f,sub)
    else:f.seek(sizes[t],1)
def offset(f):
    assert f.read(4)==b'GGUF'; n(f,'I'); tensors=n(f,'Q'); fields=n(f,'Q')
    for _ in range(fields):skip(f,8);skip(f,n(f,'I'))
    for _ in range(tensors):skip(f,8);dimensions=n(f,'I');f.seek(dimensions*8+12,1)
    return (f.tell()+31)//32*32
with original.open('rb') as a,adapted.open('rb') as b:
    start_a=offset(a);start_b=offset(b);a.seek(start_a);b.seek(start_b)
    ha=hashlib.sha256();hb=hashlib.sha256();remaining=original.stat().st_size-start_a
    while remaining:
        size=min(remaining,16*1024**2);aa=a.read(size);bb=b.read(size)
        assert len(aa)==size and len(bb)==size;ha.update(aa);hb.update(bb);remaining-=size
    assert ha.digest()==hb.digest(), 'Weight payload changed'
report={'unchangedOriginalWeightPayload':True,'sha256':ha.hexdigest(),'originalDataOffset':start_a,'compatibleDataOffset':start_b}
(adapted.parent/'weight-payload-verification.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report))
