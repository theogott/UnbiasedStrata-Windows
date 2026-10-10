#pragma once
#include "deepseek_gguf.h"
#include "windows_backend_process.h"
#include "local_key.h"
#include <iostream>

namespace deepseek_entry {
inline HANDLE cancel_event=nullptr;
inline BOOL WINAPI cancel(DWORD kind) { if(kind==CTRL_C_EVENT || kind==CTRL_BREAK_EVENT || kind==CTRL_CLOSE_EVENT || kind==CTRL_LOGOFF_EVENT || kind==CTRL_SHUTDOWN_EVENT) { if(cancel_event)SetEvent(cancel_event);return TRUE; } return FALSE; }
inline int bounded(const std::string& input,int low,int high) { size_t end=0; const auto value=std::stoll(input,&end); if(end!=input.size() || value<low || value>high)throw std::runtime_error("numeric option is out of range"); return (int)value; }
inline void usage() {
    std::fputs("DeepSeek-V4.1-Flash (experimental native Windows backend):\n"
        "  unbiased-strata --backend deepseek41 --model C:\\models\\MODEL-00001-of-00011.gguf --api-key-file C:\\private\\key.txt [options]\n"
        "  --backend-exe PATH   absolute path to our hardened unbiased-deepseek-server.exe\n"
        "  --port N             API port (default 8080, loopback only)\n"
        "  --ctx N              context 1024..8192 (default 4096)\n"
        "  --threads N          CPU workers 1..64 (default 4)\n"
        "  --expert-vram-gib N   streaming VRAM cache 1..22 (default 18)\n"
        "  --expert-ram-gib N    pinned RAM cache 0..72 (default 64)\n"
        "  --io-threads N       SSD I/O workers 1..16 (default 4)\n"
        "  --check-only         verify backend, key, shard metadata and RAM without loading weights\n"
        "No Qwen pack or MTP files are used; model download and GPU inference are separate steps.\n",stderr);
}
inline int run(int argc,char** argv) {
    try {
        std::string model,keyfile,backend;int port=8080,ctx=4096,threads=4,vram=18,ram=64,io=4;bool check=false;
        for(int i=1;i<argc;++i) { const std::string name(argv[i]);auto next=[&](){if(i+1>=argc)throw std::runtime_error(name+" needs a value");return std::string(argv[++i]);};
            if(name=="--backend") { if(next()!="deepseek41")throw std::runtime_error("unknown backend"); }
            else if(name=="--model")model=next();else if(name=="--api-key-file")keyfile=next();else if(name=="--backend-exe")backend=next();
            else if(name=="--port")port=bounded(next(),1,65535);else if(name=="--ctx")ctx=bounded(next(),1024,8192);else if(name=="--threads")threads=bounded(next(),1,64);
            else if(name=="--expert-vram-gib")vram=bounded(next(),1,22);else if(name=="--expert-ram-gib")ram=bounded(next(),0,72);else if(name=="--io-threads")io=bounded(next(),1,16);
            else if(name=="--host") { if(next()!="127.0.0.1")throw std::runtime_error("DeepSeek binds only to 127.0.0.1"); }
            else if(name=="--check-only")check=true;else if(name=="--help" || name=="-h"){usage();return 0;}else throw std::runtime_error("unsupported DeepSeek option: "+name);
        }
        std::string key;if(!read_local_api_key(keyfile,key))throw std::runtime_error("requires a regular local API-key file with 32..512 printable characters");
        std::wstring executable;
        if(backend.empty()) { wchar_t self[32768];const auto n=GetModuleFileNameW(nullptr,self,32768);if(!n || n>=32768)throw std::runtime_error("cannot locate frontend");std::wstring path(self,n);executable=path.substr(0,path.find_last_of(L"\\/"))+L"\\unbiased-deepseek-server.exe"; }
        else executable=deepseek_gguf::local_path(backend);
        const auto attrs=GetFileAttributesW(executable.c_str());if(attrs==INVALID_FILE_ATTRIBUTES || (attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_OFFLINE)))throw std::runtime_error("missing or invalid hardened DeepSeek backend; build/install it first");
        const auto shards=deepseek_gguf::check(model);
        MEMORYSTATUSEX memory{sizeof(memory)};if(!GlobalMemoryStatusEx(&memory))throw std::runtime_error("cannot determine free RAM");
        const double free=(double)memory.ullAvailPhys/1073741824.0;const bool ready=free>=ram+32;
        std::fprintf(stderr,"DeepSeek-V4.1-Flash: %zu GGUF shards, %.1f GiB RAM free, %d GiB required; inference untested\n",shards.size(),free,ram+32);
        if(check)return ready?0:3;
        if(!ready)throw std::runtime_error("insufficient free RAM; no model started (reduce pinned cache or free memory)");
        // Fail closed when a plain/unpatched llama-server was selected by mistake.
        { WindowsBackendProcess probe; std::string error; DWORD code=0;
          if(!probe.start(executable,{"--unbiased-policy-check"},error) || !probe.finish(10000,code) || code!=41)
              throw std::runtime_error("backend does not identify our local text HTTP policy; use the packaged hardened backend"); }
        std::vector<std::string> args={"--model",model,"--api-key-file",keyfile,"--host","127.0.0.1","--port",std::to_string(port),"--ctx-size",std::to_string(ctx),"--threads",std::to_string(threads),"--parallel","1","--alias","DeepSeek-V4.1-Flash","--moe-stream","--moe-stream-cache",std::to_string(vram),"--moe-stream-l2",std::to_string(ram),"--moe-stream-io-threads",std::to_string(io),"--moe-stream-direct","--reasoning","off","--no-webui","--no-ui-mcp-proxy"};
        cancel_event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!cancel_event)throw std::runtime_error("cannot create cancellation event");
        if(!SetConsoleCtrlHandler(cancel,TRUE)){CloseHandle(cancel_event);cancel_event=nullptr;throw std::runtime_error("cannot install console cancellation handler");}
        WindowsBackendProcess child;std::string error;
        int result=1;if(child.start(executable,args,error)) {std::fprintf(stderr,"Loading experimental DeepSeek backend; Ctrl+C stops its process tree. API: http://127.0.0.1:%d/v1\n",port);result=(int)child.wait(cancel_event);}else std::fprintf(stderr,"%s\n",error.c_str());
        child.stop();SetConsoleCtrlHandler(cancel,FALSE);CloseHandle(cancel_event);cancel_event=nullptr;return result;
    }catch(const std::exception& ex){std::fprintf(stderr,"unbiased-strata (DeepSeek): %s\n",ex.what());return 2;}
}
}
