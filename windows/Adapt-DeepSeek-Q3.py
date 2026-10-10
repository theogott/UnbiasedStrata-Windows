import pathlib,struct,shutil,os,json,argparse
parser=argparse.ArgumentParser(description='Make a compatible view of the vcruz305 Q3_K_M GGUF without changing original weights.')
parser.add_argument('model_directory',type=pathlib.Path)
parser.add_argument('--output-directory',type=pathlib.Path)
args=parser.parse_args()
src=args.model_directory.resolve()
dst=args.output_directory.resolve() if args.output_directory else src.with_name(src.name+'-compatible')
if dst==src: raise RuntimeError('Output must be separate from the original download')
formats={0:'B',1:'b',2:'H',3:'h',4:'I',5:'i',6:'f',7:'B',10:'Q',11:'q',12:'d'}
def pack(fmt,*v): return struct.pack('<'+fmt,*v)
def num(f,fmt): return struct.unpack('<'+fmt,f.read(struct.calcsize(fmt)))[0]
def string(f): return f.read(num(f,'Q')).decode('utf-8')
def encoded(s):
    b=s.encode('utf-8'); return pack('Q',len(b))+b
def value(f,t):
    if t==8:return string(f)
    if t==9:
        element=num(f,'I');count=num(f,'Q')
        return [value(f,element) for _ in range(count)]
    return num(f,formats[t])
first=sorted(src.glob('*.gguf'))[0]
fields={};records=[];tensor_records=[]
with first.open('rb') as f:
    assert f.read(4)==b'GGUF'; version=num(f,'I'); n=num(f,'Q'); count=num(f,'Q')
    for _ in range(count):
        start=f.tell();name=string(f);typ=num(f,'I');v=value(f,typ);end=f.tell()
        fields[name]=v; f.seek(start);records.append(f.read(end-start))
    for _ in range(n):
        start=f.tell();name=string(f);shape=[num(f,'Q') for _ in range(num(f,'I'))];typ=num(f,'I');offset=num(f,'Q');end=f.tell()
        f.seek(start);tensor_records.append(f.read(end-start))
    align=fields.get('general.alignment',32); old_data=(f.tell()+align-1)//align*align
assert fields['general.architecture']=='deepseek41'
assert fields['deepseek41.engram.layer_ids']==[1,14]
newfields={
 'deepseek41.engram.present':(7,1),
 'deepseek41.engram_head_dim':(4,fields['deepseek41.engram.key_length']),
 'deepseek41.engram_n_heads':(4,fields['deepseek41.engram.head_count']),
 'deepseek41.engram_max_ngram_size':(4,fields['deepseek41.engram.max_ngram_size']),
 'deepseek41.engram_pad_token_id':(4,fields['deepseek41.engram.pad_id']),
 'deepseek41.engram.num_embeddings':(9,[fields['deepseek41.engram.offsets'][23]+fields['deepseek41.engram.primes'][23],fields['deepseek41.engram.offsets'][47]+fields['deepseek41.engram.primes'][47]])}
for name,(typ,v) in newfields.items():
    assert name not in fields
    body=pack('I',4)+pack('Q',len(v))+pack('I'*len(v),*v) if typ==9 else pack(formats[typ],v)
    records.append(encoded(name)+pack('I',typ)+body)
constants=[('engram_token_map',fields['deepseek41.engram.token_map'],[len(fields['deepseek41.engram.token_map'])],26,'i'),
 ('engram_primes',fields['deepseek41.engram.primes'],[24,2],27,'q'),
 ('engram_offsets',fields['deepseek41.engram.offsets'],[24,2],27,'q'),
 ('engram_multipliers',fields['deepseek41.engram.multipliers'],[4,2],27,'q')]
# GGML integer type IDs are checked against the pinned backend before running this script.
data_size=first.stat().st_size-old_data
append_offset=(data_size+align-1)//align*align
payload=bytearray()
for name,vals,shape,typ,fmt in constants:
    offset=append_offset+len(payload)
    tensor_records.append(encoded(name+'.weight')+pack('I',len(shape))+pack('Q'*len(shape),*shape)+pack('I',typ)+pack('Q',offset))
    payload.extend(pack(fmt*len(vals),*vals))
    payload.extend(b'\0'*((-len(payload))%align))
# Preserve split validation's total tensor count.
records=[record for record in records if not record.startswith(encoded('split.tensors.count'))]
records.append(encoded('split.tensors.count')+pack('I',5)+pack('i',fields['split.tensors.count']+4))
header=b'GGUF'+pack('IQQ',version,n+4,len(records))+b''.join(records)+b''.join(tensor_records)
header+=b'\0'*((-len(header))%align)
dst.mkdir(exist_ok=True)
output=dst/first.name
assert not output.exists(), 'Compatible first shard already exists'
if shutil.disk_usage(dst).free<first.stat().st_size+10*1024**3:raise RuntimeError('Insufficient disk space; originals preserved')
temporary=output.with_suffix('.adapting')
with first.open('rb') as source,temporary.open('wb') as target:
    target.write(header);source.seek(old_data);shutil.copyfileobj(source,target,16*1024**2)
    target.write(b'\0'*(append_offset-data_size));target.write(payload)
temporary.replace(output)
for sibling in sorted(src.glob('*.gguf'))[1:]:
    target=dst/sibling.name
    if not target.exists(): os.link(sibling,target)
(dst/'compatibility.json').write_text(json.dumps({'original':str(src),'firstShard':str(output),'addedMetadata':list(newfields),'addedTensors':[x[0] for x in constants],'remainingShards':'hardlinks to original verified files'},indent=2))
print('Compatible view created:',output)
