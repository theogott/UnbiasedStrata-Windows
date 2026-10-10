#pragma once
#include "windows_engine_pipe.h"
#include <stdexcept>
#include <cwctype>

class WindowsBackendProcess {
    HANDLE process_ = nullptr, job_ = nullptr;
public:
    ~WindowsBackendProcess() { stop(); }
    WindowsBackendProcess() = default;
    WindowsBackendProcess(const WindowsBackendProcess&) = delete;
    bool alive() const { return process_ && WaitForSingleObject(process_,0)==WAIT_TIMEOUT; }
    bool start(const std::wstring& executable, const std::vector<std::string>& args, std::string& error) {
        if(process_) { error="backend already running"; return false; }
        std::wstring command=WindowsEnginePipe::quote(executable);
        for(const auto& arg:args) { int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,arg.data(),(int)arg.size(),nullptr,0); if(!n && !arg.empty()) { error="invalid UTF-8 argument"; return false; }
            std::wstring wide(n,L'\0'); if(n) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,arg.data(),(int)arg.size(),wide.data(),n); command+=L" "+WindowsEnginePipe::quote(wide); }
        if(command.size()>=32767) { error="backend command too long"; return false; }
        // Ignore inherited llama/cloud/proxy settings. Only our explicit arguments select the model and API.
        std::vector<wchar_t> environment;
        wchar_t* original=GetEnvironmentStringsW();
        if(!original) { error="cannot read process environment"; return false; }
        for(const wchar_t* entry=original; *entry; entry+=wcslen(entry)+1) {
            std::wstring text(entry), upper=text; for(auto& c:upper)c=(wchar_t)towupper(c);
            if(upper.rfind(L"LLAMA_",0)==0 || upper.rfind(L"AIP_",0)==0 || upper.rfind(L"HF_",0)==0 || upper.rfind(L"HTTP_PROXY=",0)==0 || upper.rfind(L"HTTPS_PROXY=",0)==0 || upper.rfind(L"ALL_PROXY=",0)==0)continue;
            environment.insert(environment.end(),text.begin(),text.end()); environment.push_back(0);
        }
        FreeEnvironmentStringsW(original); environment.push_back(0);
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr), log=nullptr;
        const HANDLE parent=GetStdHandle(STD_ERROR_HANDLE);
        if(!parent || parent==INVALID_HANDLE_VALUE || !DuplicateHandle(GetCurrentProcess(),parent,GetCurrentProcess(),&log,0,TRUE,DUPLICATE_SAME_ACCESS))
            log=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
        auto close=[](HANDLE h){if(h && h!=INVALID_HANDLE_VALUE)CloseHandle(h);};
        if(input==INVALID_HANDLE_VALUE || log==INVALID_HANDLE_VALUE) { error="cannot open backend standard handles"; close(input);close(log);return false; }
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput=input;startup.StartupInfo.hStdOutput=log;startup.StartupInfo.hStdError=log;
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);std::vector<unsigned char> storage(size);startup.lpAttributeList=(LPPROC_THREAD_ATTRIBUTE_LIST)storage.data();
        if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&size)){error="cannot initialize backend handles";close(input);close(log);return false;}
        HANDLE handles[]{input,log};
        if(!UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr)){error="cannot restrict backend handle inheritance";DeleteProcThreadAttributeList(startup.lpAttributeList);close(input);close(log);return false;}
        job_=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job_ || !SetInformationJobObject(job_,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){error="cannot create backend lifetime job";DeleteProcThreadAttributeList(startup.lpAttributeList);close(input);close(log);stop();return false;}
        PROCESS_INFORMATION pi{};
        BOOL created=CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_SUSPENDED|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,environment.data(),nullptr,&startup.StartupInfo,&pi);
        const DWORD failure=GetLastError();DeleteProcThreadAttributeList(startup.lpAttributeList);close(input);close(log);
        if(!created){error="cannot start backend: "+std::to_string(failure);stop();return false;}
        process_=pi.hProcess;
        if(!AssignProcessToJobObject(job_,process_) || ResumeThread(pi.hThread)==(DWORD)-1){TerminateProcess(process_,1);WaitForSingleObject(process_,5000);CloseHandle(pi.hThread);error="cannot supervise backend";stop();return false;}
        CloseHandle(pi.hThread);return true;
    }
    void stop() { if(job_)TerminateJobObject(job_,1); if(process_){WaitForSingleObject(process_,5000);CloseHandle(process_);process_=nullptr;} if(job_){CloseHandle(job_);job_=nullptr;} }
    bool finish(DWORD timeout, DWORD& code) { return process_ && WaitForSingleObject(process_,timeout)==WAIT_OBJECT_0 && GetExitCodeProcess(process_,&code); }
    DWORD wait(HANDLE cancel) { HANDLE handles[]{process_,cancel}; WaitForMultipleObjects(2,handles,FALSE,INFINITE); DWORD code=1;if(WaitForSingleObject(process_,0)==WAIT_OBJECT_0)GetExitCodeProcess(process_,&code);else {stop();code=0;}return code; }
};
