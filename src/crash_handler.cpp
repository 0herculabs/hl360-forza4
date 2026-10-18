// fm4 - crash diagnostics: on an unhandled exception, writes the symbolized
// host stack (recompiled guest functions show up as sub_XXXXXXXX) and a
// minidump to <exe>/logs/crash_<timestamp>.{txt,dmp}.

#include "crash_handler.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>

namespace fm4 {
namespace {

std::filesystem::path g_log_dir;
std::once_flag g_once;

const char* ExceptionName(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
    case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
    case 0xC0000409: return "STACK_BUFFER_OVERRUN / fail-fast";
    default: return "UNKNOWN";
  }
}

void WriteStack(FILE* out, EXCEPTION_POINTERS* ep) {
  HANDLE process = GetCurrentProcess();
  HANDLE thread = GetCurrentThread();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  SymInitialize(process, nullptr, TRUE);

  CONTEXT context = *ep->ContextRecord;
  STACKFRAME64 frame = {};
  frame.AddrPC.Offset = context.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Offset = context.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;

  alignas(SYMBOL_INFO) char symbol_buf[sizeof(SYMBOL_INFO) + 256];
  for (int i = 0; i < 64; ++i) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
        frame.AddrPC.Offset == 0) {
      break;
    }
    DWORD64 pc = frame.AddrPC.Offset;
    char module[MAX_PATH] = "?";
    if (DWORD64 base = SymGetModuleBase64(process, pc)) {
      GetModuleFileNameA(reinterpret_cast<HMODULE>(base), module, MAX_PATH);
      std::filesystem::path p(module);
      std::snprintf(module, sizeof(module), "%s", p.filename().string().c_str());
    }
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buf);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    DWORD64 displacement = 0;
    if (SymFromAddr(process, pc, &displacement, symbol)) {
      IMAGEHLP_LINE64 line = {sizeof(line)};
      DWORD line_disp = 0;
      if (SymGetLineFromAddr64(process, pc, &line_disp, &line)) {
        std::fprintf(out, "  #%02d %s!%s+0x%llx  (%s:%lu)\n", i, module, symbol->Name,
                     static_cast<unsigned long long>(displacement), line.FileName,
                     line.LineNumber);
      } else {
        std::fprintf(out, "  #%02d %s!%s+0x%llx\n", i, module, symbol->Name,
                     static_cast<unsigned long long>(displacement));
      }
    } else {
      std::fprintf(out, "  #%02d %s!0x%016llx\n", i, module, static_cast<unsigned long long>(pc));
    }
  }
  SymCleanup(process);
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ep) {
  std::call_once(g_once, [ep] {
    std::error_code ec;
    std::filesystem::create_directories(g_log_dir, ec);
    char stamp[32];
    std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&now));
    auto base = g_log_dir / (std::string("crash_") + stamp);

    if (FILE* out = _wfopen((base.wstring() + L".txt").c_str(), L"w")) {
      const EXCEPTION_RECORD* rec = ep->ExceptionRecord;
      std::fprintf(out, "Exception 0x%08lX (%s) at 0x%016llx, thread %lu\n", rec->ExceptionCode,
                   ExceptionName(rec->ExceptionCode),
                   reinterpret_cast<unsigned long long>(rec->ExceptionAddress),
                   GetCurrentThreadId());
      if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
        std::fprintf(out, "%s of host address 0x%016llx\n",
                     rec->ExceptionInformation[0] == 1 ? "Write" : "Read",
                     static_cast<unsigned long long>(rec->ExceptionInformation[1]));
      }
      const CONTEXT* c = ep->ContextRecord;
      std::fprintf(out,
                   "rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n"
                   "rsi=%016llx rdi=%016llx rbp=%016llx rsp=%016llx\n"
                   "r8 =%016llx r9 =%016llx r10=%016llx r11=%016llx\n"
                   "r12=%016llx r13=%016llx r14=%016llx r15=%016llx\n\nStack:\n",
                   c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->Rbp, c->Rsp, c->R8, c->R9,
                   c->R10, c->R11, c->R12, c->R13, c->R14, c->R15);
      WriteStack(out, ep);
      std::fclose(out);
    }

    HANDLE dump = CreateFileW((base.wstring() + L".dmp").c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
      MINIDUMP_EXCEPTION_INFORMATION info = {GetCurrentThreadId(), ep, FALSE};
      MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                        static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo |
                                                   MiniDumpWithIndirectlyReferencedMemory),
                        &info, nullptr, nullptr);
      CloseHandle(dump);
    }
  });
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void InstallCrashHandler(const std::filesystem::path& log_dir) {
  g_log_dir = log_dir;
  SetUnhandledExceptionFilter(UnhandledFilter);
}

}  // namespace fm4

#else

namespace fm4 {
void InstallCrashHandler(const std::filesystem::path&) {}
}  // namespace fm4

#endif
