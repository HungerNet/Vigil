#pragma once

#ifndef VIGIL_DEBUG_LOG
#define VIGIL_DEBUG_LOG 0
#endif

#ifdef VIGIL_LUNAR

void ClassFinderLogInit(void* moduleHandle);
void ClassFinderLogShutdown();
void ClassFinderLogLine(const char* line);
void ClassFinderLogClassResolve(const char* requestedMcpSlash, const char* via, bool ok);
void ClassFinderLogMemberMiss(const char* kind, const char* ownerHint, const char* name, const char* signature);

#else

inline void ClassFinderLogInit(void*) {}
inline void ClassFinderLogShutdown() {}
inline void ClassFinderLogLine(const char*) {}
inline void ClassFinderLogClassResolve(const char*, const char*, bool) {}
inline void ClassFinderLogMemberMiss(const char*, const char*, const char*, const char*) {}

#endif
