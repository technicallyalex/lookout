// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <cstdio>
namespace fs=std::filesystem;
namespace {
bool extract(HINSTANCE instance,int id,const fs::path &file){
 const auto resource=FindResourceW(instance,MAKEINTRESOURCEW(id),RT_RCDATA);if(!resource)return false;
 const auto block=LoadResource(instance,resource);const auto data=static_cast<const char*>(LockResource(block));if(!data)return false;
 std::ofstream out(file,std::ios::binary);out.write(data,SizeofResource(instance,resource));return bool(out);
}
std::wstring readResult(const fs::path &file){
 std::ifstream stream(file,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(stream)),{});if(bytes.rfind("\xef\xbb\xbf",0)==0)bytes.erase(0,3);if(bytes.empty() || bytes.size()>8192)return L"lookout setup did not finish. Close OBS and try again.";
 const int length=MultiByteToWideChar(CP_UTF8,0,bytes.data(),int(bytes.size()),nullptr,0);std::wstring result(length,L'\0');MultiByteToWideChar(CP_UTF8,0,bytes.data(),int(bytes.size()),result.data(),length);return result;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int){
 const std::wstring flags=command;const bool verify=flags.find(L"--verify")!=std::wstring::npos,silent=verify || flags.find(L"--silent")!=std::wstring::npos;
 if(!silent && flags.find(L"--elevated")==std::wstring::npos && MessageBoxW(nullptr,L"Install lookout for OBS? Close OBS before continuing. Existing settings will be kept.",L"lookout setup",MB_OKCANCEL|MB_ICONINFORMATION)!=IDOK)return 0;
 int result=1;std::wstring status=L"lookout could not extract its installation files.";
 try{
  const auto parent=fs::canonical(fs::temp_directory_path());GUID guid{};wchar_t token[40]{};CoCreateGuid(&guid);StringFromGUID2(guid,token,40);const auto folder=parent/(std::wstring(L"lookout-setup-")+token);
  if(fs::create_directory(folder)){
   if(extract(instance,101,folder/L"payload.zip") && extract(instance,102,folder/L"install.ps1")){
    wchar_t windows[MAX_PATH]{};GetSystemDirectoryW(windows,MAX_PATH);const auto powershell=fs::path(windows)/L"WindowsPowerShell/v1.0/powershell.exe";
    std::wstring cmd=L"\""+powershell.wstring()+L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \""+(folder/L"install.ps1").wstring()+L"\""+(verify?L" -VerifyOnly":L"");
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(CreateProcessW(powershell.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,folder.c_str(),&startup,&process)){
     WaitForSingleObject(process.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hThread);CloseHandle(process.hProcess);result=int(code);status=readResult(folder/L"result.txt");
    }
   }
   std::error_code error;if(fs::weakly_canonical(folder).parent_path()==parent && folder.filename().wstring().rfind(L"lookout-setup-",0)==0)fs::remove_all(folder,error);
  }
 }catch(const std::exception &){status=L"lookout could not prepare its installation files.";}
 if(result==5 && !verify && flags.find(L"--elevated")==std::wstring::npos){
  wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);const auto args=silent?L"--elevated --silent":L"--elevated";SHELLEXECUTEINFOW run{};run.cbSize=sizeof(run);run.fMask=SEE_MASK_NOCLOSEPROCESS;run.lpVerb=L"runas";run.lpFile=self;run.lpParameters=args;run.nShow=SW_SHOWNORMAL;
  if(ShellExecuteExW(&run) && run.hProcess){WaitForSingleObject(run.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(run.hProcess,&code);CloseHandle(run.hProcess);return int(code);}
 }
 if(!silent)MessageBoxW(nullptr,status.c_str(),L"lookout setup",MB_OK|(result?MB_ICONERROR:MB_ICONINFORMATION));else std::fwprintf(stderr,L"%ls\n",status.c_str());return result;
}
