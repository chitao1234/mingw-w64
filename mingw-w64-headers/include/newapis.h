/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the mingw-w64 runtime package.
 * No warranty is given; refer to the file DISCLAIMER.PD within this package.
 */

#include <_mingw_unicode.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WANT_GETDISKFREESPACEEX_WRAPPER

#undef GetDiskFreeSpaceEx
#define GetDiskFreeSpaceEx _GetDiskFreeSpaceEx

  extern WINBOOL (CALLBACK *GetDiskFreeSpaceEx)(LPCTSTR,PULARGE_INTEGER,PULARGE_INTEGER,PULARGE_INTEGER);

#ifdef COMPILE_NEWAPIS_STUBS
  static WINBOOL WINAPI Emulate_GetDiskFreeSpaceEx(LPCTSTR ptszRoot,PULARGE_INTEGER pliQuota,PULARGE_INTEGER pliTotal,PULARGE_INTEGER pliFree) {
    DWORD dwSecPerClus,dwBytesPerSec,dwFreeClus,dwTotalClus;
    WINBOOL fRc;
    fRc = GetDiskFreeSpace(ptszRoot,&dwSecPerClus,&dwBytesPerSec,&dwFreeClus,&dwTotalClus);
    if(fRc) {
      DWORD dwBytesPerClus = dwSecPerClus *dwBytesPerSec;
      *(__int64 *)pliQuota = Int32x32To64(dwBytesPerClus,dwFreeClus);
      if(pliFree) {
	*pliFree = *pliQuota;
      }
      *(__int64 *)pliTotal = Int32x32To64(dwBytesPerClus,dwTotalClus);
    }
    return fRc;
  }

  static WINBOOL WINAPI Probe_GetDiskFreeSpaceEx(LPCTSTR ptszRoot,PULARGE_INTEGER pliQuota,PULARGE_INTEGER pliTotal,PULARGE_INTEGER pliFree) {
    HINSTANCE hinst;
    FARPROC fp;
    WINBOOL fRc;
    WINBOOL (CALLBACK *RealGetDiskFreeSpaceEx) (LPCTSTR,PULARGE_INTEGER,PULARGE_INTEGER,PULARGE_INTEGER);
    hinst = GetModuleHandle(TEXT("KERNEL32"));
    fp = GetProcAddress(hinst,"GetDiskFreeSpaceEx" __MINGW_PROCNAMEEXT_AW);
    if(fp) {
      *(FARPROC *)&RealGetDiskFreeSpaceEx = fp;
      fRc = RealGetDiskFreeSpaceEx(ptszRoot,pliQuota,pliTotal,pliFree);
      if(fRc || GetLastError()!=ERROR_CALL_NOT_IMPLEMENTED) {
	GetDiskFreeSpaceEx = RealGetDiskFreeSpaceEx;
      } else {
	GetDiskFreeSpaceEx = Emulate_GetDiskFreeSpaceEx;
	fRc = GetDiskFreeSpaceEx(ptszRoot,pliQuota,pliTotal,pliFree);
      }
    } else {
      GetDiskFreeSpaceEx = Emulate_GetDiskFreeSpaceEx;
      fRc = GetDiskFreeSpaceEx(ptszRoot,pliQuota,pliTotal,pliFree);
    }
    return fRc;
  }

  WINBOOL (CALLBACK *GetDiskFreeSpaceEx) (LPCTSTR,PULARGE_INTEGER,PULARGE_INTEGER,PULARGE_INTEGER) = Probe_GetDiskFreeSpaceEx;
#endif
#endif

#ifdef WANT_GETLONGPATHNAME_WRAPPER

#undef GetLongPathName
#define GetLongPathName _GetLongPathName

  extern DWORD (CALLBACK *GetLongPathName)(LPCTSTR,LPTSTR,DWORD);

#ifdef COMPILE_NEWAPIS_STUBS
  static LPTSTR Emulate_GetLongPathName_NextSeparator(LPTSTR path) {
    while(*path && *path!=TEXT('\\')) {
#ifndef UNICODE
      if(IsDBCSLeadByte((BYTE)*path) && path[1])
	++path;
#endif
      ++path;
    }
    return path;
  }

  static DWORD WINAPI Emulate_GetLongPathName(LPCTSTR ptszShort,LPTSTR ptszLong,DWORD ctchBuf) {
    TCHAR tsz[MAX_PATH],tszLong[MAX_PATH];
    WIN32_FIND_DATA wfd;
    LPTSTR start,end;
    DWORD dwRc,length,count,i;
    HANDLE hfind;
    TCHAR separator;

    if(GetFileAttributes(ptszShort)==0xFFFFFFFF) return 0;
    dwRc = GetFullPathName(ptszShort,MAX_PATH,tsz,NULL);
    if(dwRc==0) return 0;
    if(dwRc >= MAX_PATH) {
      SetLastError(ERROR_BUFFER_OVERFLOW);
      return 0;
    }

    /* Keep the drive root or UNC server and share outside the search. */
    if(tsz[0]==TEXT('\\') && tsz[1]==TEXT('\\')) {
      start = Emulate_GetLongPathName_NextSeparator(tsz + 2);
      if(*start)
	start = Emulate_GetLongPathName_NextSeparator(start + 1);
      if(*start)
	++start;
    } else {
      start = tsz + 3;
    }
    length = (DWORD)(start - tsz);
    for(i = 0; i < length; ++i)
      tszLong[i] = tsz[i];

    while(*start) {
      end = Emulate_GetLongPathName_NextSeparator(start);
      separator = *end;
      *end = 0;
      hfind = FindFirstFile(tsz,&wfd);
      *end = separator;
      if(hfind==INVALID_HANDLE_VALUE) return 0;
      FindClose(hfind);

      count = lstrlen(wfd.cFileName);
      if(length + count + (separator!=0) >= MAX_PATH) {
	SetLastError(ERROR_BUFFER_OVERFLOW);
	return 0;
      }
      for(i = 0; i < count; ++i)
	tszLong[length++] = wfd.cFileName[i];
      if(separator)
	tszLong[length++] = separator;
      start = separator ? end + 1 : end;
    }
    tszLong[length] = 0;

    if(length >= ctchBuf) {
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return length + 1;
    }
    lstrcpy(ptszLong,tszLong);
    return length;
  }

  static DWORD WINAPI Probe_GetLongPathName(LPCTSTR ptszShort,LPTSTR ptszLong,DWORD ctchBuf) {
    HINSTANCE hinst;
    FARPROC fp;
    DWORD dwRc;
    DWORD (CALLBACK *RealGetLongPathName)(LPCTSTR,LPTSTR,DWORD);
    hinst = GetModuleHandle(TEXT("KERNEL32"));

    fp = GetProcAddress(hinst,"GetLongPathName" __MINGW_PROCNAMEEXT_AW);
    if(fp) {
      *(FARPROC *)&RealGetLongPathName = fp;
      dwRc = RealGetLongPathName(ptszShort,ptszLong,ctchBuf);
      if(dwRc || GetLastError()!=ERROR_CALL_NOT_IMPLEMENTED) {
	GetLongPathName = RealGetLongPathName;
      } else {
	GetLongPathName = Emulate_GetLongPathName;
	dwRc = GetLongPathName(ptszShort,ptszLong,ctchBuf);
      }
    } else {
      GetLongPathName = Emulate_GetLongPathName;
      dwRc = GetLongPathName(ptszShort,ptszLong,ctchBuf);
    }
    return dwRc;

  }

  DWORD (CALLBACK *GetLongPathName)(LPCTSTR,LPTSTR,DWORD) = Probe_GetLongPathName;
#endif
#endif

#ifdef WANT_GETFILEATTRIBUTESEX_WRAPPER

#undef GetFileAttributesEx
#define GetFileAttributesEx _GetFileAttributesEx

  extern WINBOOL (CALLBACK *GetFileAttributesEx)
    (LPCTSTR,GET_FILEEX_INFO_LEVELS,LPVOID);

#ifdef COMPILE_NEWAPIS_STUBS

  static WINBOOL WINAPI Emulate_GetFileAttributesEx(LPCTSTR ptszFile,GET_FILEEX_INFO_LEVELS level,LPVOID pv) {
    WINBOOL fRc;
    if(level==GetFileExInfoStandard) {
      if(GetFileAttributes(ptszFile)!=0xFFFFFFFF) {
	HANDLE hfind;
	WIN32_FIND_DATA wfd;
	hfind = FindFirstFile(ptszFile,&wfd);
	if(hfind!=INVALID_HANDLE_VALUE) {
	  LPWIN32_FILE_ATTRIBUTE_DATA pfad = pv;
	  FindClose(hfind);
	  pfad->dwFileAttributes = wfd.dwFileAttributes;
	  pfad->ftCreationTime = wfd.ftCreationTime;
	  pfad->ftLastAccessTime = wfd.ftLastAccessTime;
	  pfad->ftLastWriteTime = wfd.ftLastWriteTime;
	  pfad->nFileSizeHigh = wfd.nFileSizeHigh;
	  pfad->nFileSizeLow = wfd.nFileSizeLow;

	  fRc = TRUE;
	} else {
	  fRc = FALSE;
	}
      } else {
	fRc = FALSE;
      }
    } else {
      SetLastError(ERROR_INVALID_PARAMETER);
      fRc = FALSE;
    }
    return fRc;
  }

  static WINBOOL WINAPI Probe_GetFileAttributesEx(LPCTSTR ptszFile,GET_FILEEX_INFO_LEVELS level,LPVOID pv) {
    HINSTANCE hinst;
    FARPROC fp;
    WINBOOL fRc;
    WINBOOL (CALLBACK *RealGetFileAttributesEx)(LPCTSTR,GET_FILEEX_INFO_LEVELS,LPVOID);
    hinst = GetModuleHandle(TEXT("KERNEL32"));
    fp = GetProcAddress(hinst,"GetFileAttributesEx" __MINGW_PROCNAMEEXT_AW);
    if(fp) {
      *(FARPROC *)&RealGetFileAttributesEx = fp;
      fRc = RealGetFileAttributesEx(ptszFile,level,pv);
      if(fRc || GetLastError()!=ERROR_CALL_NOT_IMPLEMENTED) {
	GetFileAttributesEx = RealGetFileAttributesEx;
      } else {
	GetFileAttributesEx = Emulate_GetFileAttributesEx;
	fRc = GetFileAttributesEx(ptszFile,level,pv);
      }
    } else {
      GetFileAttributesEx = Emulate_GetFileAttributesEx;
      fRc = GetFileAttributesEx(ptszFile,level,pv);
    }
    return fRc;
  }

  WINBOOL (CALLBACK *GetFileAttributesEx)(LPCTSTR,GET_FILEEX_INFO_LEVELS,LPVOID) = Probe_GetFileAttributesEx;
#endif
#endif

#ifdef WANT_ISDEBUGGERPRESENT_WRAPPER
#define IsDebuggerPresent _IsDebuggerPresent

  extern WINBOOL (CALLBACK *IsDebuggerPresent)(VOID);

#ifdef COMPILE_NEWAPIS_STUBS
  static WINBOOL WINAPI Emulate_IsDebuggerPresent(VOID) { return FALSE; }
  static WINBOOL WINAPI Probe_IsDebuggerPresent(VOID) {
    HINSTANCE hinst;
    FARPROC fp;
    WINBOOL (CALLBACK *RealIsDebuggerPresent)(VOID);
    hinst = GetModuleHandle(TEXT("KERNEL32"));
    fp = GetProcAddress(hinst,"IsDebuggerPresent");
    if(fp) {
      *(FARPROC *)&IsDebuggerPresent = fp;
    } else {
      IsDebuggerPresent = Emulate_IsDebuggerPresent;
    }
    return IsDebuggerPresent();
  }

  WINBOOL (CALLBACK *IsDebuggerPresent)(VOID) = Probe_IsDebuggerPresent;
#endif
#endif

#ifdef __cplusplus
}
#endif
