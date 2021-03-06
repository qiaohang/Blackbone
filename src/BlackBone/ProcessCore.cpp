#include "Config.h"
#include "ProcessCore.h"
#include "DynImport.h"
#include "Macro.h"

namespace blackbone
{

#ifdef COMPILER_GCC
#define PROCESS_DEP_ENABLE  0x00000001
#endif

ProcessCore::ProcessCore()
    : _native( nullptr )
{
    DynImport::load( "GetProcessDEPPolicy", L"kernel32.dll" );
}

ProcessCore::~ProcessCore()
{
    Close();
}

/// <summary>
/// Attach to existing process
/// </summary>
/// <param name="pid">Process ID</param>
/// <param name="access">Access mask</param>
/// <returns>Status</returns>
NTSTATUS ProcessCore::Open( DWORD pid, DWORD access )
{
    // Prevent handle leak
    Close();

    // Handle current process differently
    _hProcess = (pid == GetCurrentProcessId()) ? GetCurrentProcess() : OpenProcess( access, false, pid );

    if (_hProcess != NULL)
    {
        _pid = pid;

        // Detect x86 OS
        SYSTEM_INFO info = { 0 };
        GetNativeSystemInfo( &info );

        if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
        {
            _native.reset( new x86Native( _hProcess ) );
        }
        else
        {
            // Detect wow64 barrier
            BOOL wowSrc = FALSE;
            IsWow64Process( GetCurrentProcess(), &wowSrc );

            if (wowSrc == TRUE)
                _native.reset( new NativeWow64( _hProcess ) );
            else
                _native.reset( new Native( _hProcess ) );
        }

        // Get DEP info
        // For native x64 processes DEP is always enabled
        if (_native->GetWow64Barrier().targetWow64 == false)
        {
            _dep = true;
        }
        else
        {
            DWORD flags = 0;
            BOOL perm = 0;

            if (GET_IMPORT(GetProcessDEPPolicy)( _hProcess, &flags, &perm ))
                _dep = (flags & PROCESS_DEP_ENABLE) != 0;
        }

        return STATUS_SUCCESS;
    }

    return LastNtStatus();
}

/// <summary>
/// Close current process handle
/// </summary>
void ProcessCore::Close()
{
    if (_hProcess)
    {
        CloseHandle( _hProcess );

        _hProcess = NULL;
        _pid = 0;
        _native.reset( nullptr );
    }
}

bool ProcessCore::isProtected()
{
    if (_hProcess)
    {
        _PROCESS_EXTENDED_BASIC_INFORMATION_T<DWORD64> info = { 0 };
        info.Size = sizeof( info );
        
        _native->QueryProcessInfoT( ProcessBasicInformation, &info, sizeof( info ) );
        return info.Flags.IsProtectedProcess;
    }

    return false;
}


}