#include "deepseek_http.h"
#include "deepseek_gguf.h"
#include "windows_backend_process.h"
#include "deepseek_entry.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>

template<class T> void number(std::ofstream& f,T n){f.write((const char*)&n,sizeof(n));}
void string(std::ofstream& f,const std::string& s){number<uint64_t>(f,s.size());f.write(s.data(),s.size());}
void gguf(const std::string& path,const std::string& arch,int count=1,int index=0){std::ofstream f(path,std::ios::binary);number<uint32_t>(f,0x46554747);number<uint32_t>(f,3);number<uint64_t>(f,0);number<uint64_t>(f,3);string(f,"general.architecture");number<uint32_t>(f,8);string(f,arch);string(f,"split.count");number<uint32_t>(f,2);number<uint16_t>(f,(uint16_t)count);string(f,"split.no");number<uint32_t>(f,2);number<uint16_t>(f,(uint16_t)index);}
int main(int argc,char** argv){
    if(argc==2 && std::string(argv[1])=="--child") {if(GetEnvironmentVariableW(L"LLAMA_ARG_HOST",nullptr,0))return 9;Sleep(200);return 0;}
    if(argc==2 && std::string(argv[1])=="--long-child") {Sleep(60000);return 0;}
    int failures=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS: ":"FAIL: ")<<label<<'\n';if(!ok)++failures;};
    auto fails=[](auto op){try{op();return false;}catch(...){return true;}};
    check(deepseek_http::text_request(R"({"model":"DeepSeek-V4.1-Flash","messages":[{"role":"user","content":"hello"}],"stream":true})"),"valid text chat");
    check(!deepseek_http::text_request(R"({"messages":[{"role":"user","content":[{"type":"image_url","image_url":{"url":"http://127.0.0.1/secret"}}]}]})"),"media URL rejected");
    check(!deepseek_http::text_request(R"({"messages":[{"role":"user","content":"hello"}],"lora":[{"path":"C:/secret"}]})"),"backend path controls rejected");
    check(!deepseek_http::text_request(R"({"messages":[]})") && !deepseek_http::text_request("[1]") && !deepseek_http::text_request("garbage"),"malformed bodies rejected");
    check(!deepseek_http::text_request(std::string(1000,'[')+std::string(1000,']')),"deep JSON nesting rejected before parsing");
    check(deepseek_http::text_request(R"({"messages":[{"role":"user","content":"[{}]"}]})"),"brackets inside text remain valid");
    const std::string key(64,'a');httplib::Server http;const auto port=http.bind_to_any_port("127.0.0.1");if(port<1)return 1;
    deepseek_http::configure(http,port,key,[](const auto& req,auto& res){if(req.get_header_value("X-Test-Loading")=="yes"){res.status=503;return false;}return true;});
    http.Get("/health",[](const auto&,auto& res){res.set_content("ok","text/plain");});
    http.Post("/v1/chat/completions",[](const auto& req,auto& res){if(!deepseek_http::body_allowed(req,res))return;res.set_chunked_content_provider("text/event-stream",[](size_t,httplib::DataSink& sink){const std::string data="data: {\"choices\":[]}\n\ndata: [DONE]\n\n";sink.write(data.data(),data.size());sink.done();return true;});});
    std::thread serving([&]{http.listen_after_bind();});while(!http.is_running())std::this_thread::sleep_for(std::chrono::milliseconds(5));httplib::Client client("127.0.0.1",port);httplib::Headers auth{{"Authorization","Bearer "+key}};
    auto result=client.Get("/health");check(result && result->status==401,"health requires authentication");
    result=client.Get("/health",httplib::Headers{{"Authorization","Bearer "+key},{"Origin","https://evil.example"}});check(result && result->status==403,"foreign Origin rejected");
    result=client.Get("/health",httplib::Headers{{"Authorization","Bearer "+key},{"Host","evil.example:"+std::to_string(port)}});check(result && result->status==403,"rebound Host rejected");
    result=client.Get("/slots",auth);check(result && result->status==404,"extra backend routes blocked");
    result=client.Get("/health",httplib::Headers{{"Authorization","Bearer "+key},{"X-Test-Loading","yes"}});check(result && result->status==503,"backend readiness preserved");
    result=client.Post("/v1/chat/completions",auth,std::string(1048577,'x'),"application/json");check(result && result->status==413,"body above one MiB rejected");
    result=client.Post("/v1/chat/completions",auth,R"({"messages":[{"role":"user","content":[{"type":"image_url","image_url":{"url":"http://127.0.0.1/secret"}}]}]})","application/json");check(result && result->status==400,"HTTP media request rejected after bounded body read");
    result=client.Post("/v1/chat/completions",auth,R"({"messages":[{"role":"user","content":"hello"}],"stream":true})","application/json");check(result && result->status==200 && result->body.find("[DONE]")!=std::string::npos,"SSE reaches client");
    http.stop();serving.join();
    char temp[MAX_PATH],file[MAX_PATH];GetTempPathA(MAX_PATH,temp);GetTempFileNameA(temp,"ds4",0,file);const std::string prefix(file);DeleteFileA(file);
    auto first=prefix+"-00001-of-00002.gguf",second=prefix+"-00002-of-00002.gguf";
    gguf(first,"deepseek41",2,0);gguf(second,"deepseek41",2,1);check(deepseek_gguf::check(first).size()==2,"all split GGUF headers checked");
    DeleteFileA(second.c_str());check(fails([&]{deepseek_gguf::check(first);}),"missing shard rejected");
    gguf(second,"deepseek4",2,1);check(fails([&]{deepseek_gguf::check(first);}),"mixed model architectures rejected");
    gguf(first,"qwen3next");check(fails([&]{deepseek_gguf::check(first);}),"Qwen rejected for DeepSeek backend");
    {std::ofstream f(first,std::ios::binary);number<uint32_t>(f,0x46554747);number<uint32_t>(f,3);number<uint64_t>(f,0);number<uint64_t>(f,1);number<uint64_t>(f,UINT64_MAX);}
    check(fails([&]{deepseek_gguf::check(first);}),"oversized GGUF string rejected");DeleteFileA(first.c_str());DeleteFileA(second.c_str());
    check(fails([]{deepseek_entry::bounded("4096junk",1,8192);}) && fails([]{deepseek_entry::bounded("-1",1,22);}),"invalid tuning numbers rejected");
    wchar_t self[32768];GetModuleFileNameW(nullptr,self,32768);WindowsBackendProcess child;std::string error;SetEnvironmentVariableW(L"LLAMA_ARG_HOST",L"0.0.0.0");
    HANDLE cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);const bool started=child.start(self,{"--child"},error);const DWORD exit=started?child.wait(cancel):1;child.stop();CloseHandle(cancel);SetEnvironmentVariableW(L"LLAMA_ARG_HOST",nullptr);
    check(started && exit==0,"child starts with sanitized environment");
    const bool long_started=child.start(self,{"--long-child"},error);const auto before=std::chrono::steady_clock::now();child.stop();check(long_started && !child.alive() && std::chrono::steady_clock::now()-before<std::chrono::seconds(6),"owned process job terminates backend");
    return failures?1:0;
}
