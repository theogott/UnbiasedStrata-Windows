"""Prepare a pinned backend and apply only exact, reviewable source hunks. Python 3.12+."""
import hashlib, json, pathlib, re, shutil, tarfile, urllib.request

root=pathlib.Path(__file__).resolve().parent.parent
backend=root/'backends'/'deepseek41'
pin=json.loads((backend/'source-pin.json').read_text(encoding='utf-8'))
sha=pin['commit']
archive=backend/('source-'+sha+'.tar.gz')
if not archive.exists():
    temporary=archive.with_suffix('.partial')
    urllib.request.urlretrieve('https://codeload.github.com/JigSawPT/llama.cpp/tar.gz/'+sha,temporary)
    if hashlib.file_digest(temporary.open('rb'),'sha256').hexdigest()!=pin['archive_sha256']: raise RuntimeError('Source archive checksum mismatch')
    temporary.replace(archive)
with archive.open('rb') as stream:
    if hashlib.file_digest(stream,'sha256').hexdigest()!=pin['archive_sha256']: raise RuntimeError('Source archive checksum mismatch')
source=backend/'source'
with tarfile.open(archive) as tar:
    prefix='llama.cpp-'+sha+'/'
    originals={}
    for name in ['CMakeLists.txt', 'common/arg.cpp', 'tools/server/server-http.cpp', 'tools/server/main.cpp', 'src/llama-arch.h', 'src/llama-arch.cpp', 'src/models/deepseek4.cpp', 'src/llama-graph.cpp']:
        originals[name]=tar.extractfile(prefix+name).read().decode('utf-8-sig').replace('\r\n','\n')
    if not source.exists():
        source.mkdir()
        for member in tar.getmembers():
            if not member.name.startswith(prefix): continue
            relative=member.name[len(prefix):]
            if not relative: continue
            if member.issym() or member.islnk(): raise RuntimeError('Unexpected link in pinned source archive')
            member.name=relative
            tar.extract(member,source,filter='data')

lines=((backend/'windows-hardening.patch').read_text(encoding='utf-8')+(backend/'q3-compatibility.patch').read_text(encoding='utf-8')).splitlines(True)
i=0
while i<len(lines):
    if not lines[i].startswith('--- a/'): raise RuntimeError('Unexpected patch format')
    name=lines[i][6:].rstrip('\n');i+=1
    if name not in originals or lines[i]!='+++ b/'+name+'\n': raise RuntimeError('Unexpected patch path')
    i+=1;original=originals[name].splitlines(True);result=[];cursor=0
    while i<len(lines) and lines[i].startswith('@@ '):
        header=re.fullmatch(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@.*\n',lines[i])
        if not header: raise RuntimeError('Invalid hunk')
        index=int(header[1])-1
        if index<cursor: raise RuntimeError('Overlapping hunk')
        result+=original[cursor:index];cursor=index;i+=1
        removed=added=0
        while i<len(lines) and not lines[i].startswith(('@@ ','--- a/')):
            line=lines[i];i+=1
            if line.startswith((' ','-')):
                if cursor>=len(original) or original[cursor]!=line[1:]: raise RuntimeError('Pinned source hunk mismatch: '+name)
                cursor+=1;removed+=1
            if line.startswith((' ','+')):result.append(line[1:]);added+=1
            elif not line.startswith('-'):raise RuntimeError('Unsupported patch line')
        if removed!=int(header[2] or 1) or added!=int(header[4] or 1):raise RuntimeError('Incorrect hunk length')
    result+=original[cursor:];updated=''.join(result);target=source/name
    existing=target.read_text(encoding='utf-8-sig').replace('\r\n','\n')
    if existing not in (originals[name],updated): raise RuntimeError('Preserving locally edited backend file: '+name)
    target.write_text(updated,encoding='utf-8',newline='\n')

headers=source/'unbiased-windows';headers.mkdir(exist_ok=True)
for name in ['http_access.h','deepseek_http.h','local_key.h','deepseek_test.cpp','deepseek_entry.h','deepseek_gguf.h','windows_backend_process.h','windows_engine_pipe.h']:
    shutil.copy2(root/'frontend'/name,headers/name)
print('Verified pinned DeepSeek-V4.1 source and applied the local Windows patch. No model loaded.')
