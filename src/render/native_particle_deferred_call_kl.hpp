#pragma once
// Exact native Type7 deferred-sprite CALL. Its eight original stack arguments
// remain in place. Recreate CALL's return address, tail-enter the matching
// fastcall wrapper and let its RET32 resume the untouched native continuation.
extern "C" {
inline void* KL1ParticleDeferredReturn=nullptr;
inline void* KL1ParticleDeferredTarget=nullptr;
#if defined(__i386__)
__attribute__((naked,used,noinline)) inline void KL1ParticleDeferredCallHook(){
    __asm__ __volatile__("pushl _KL1ParticleDeferredReturn\n\tjmp *_KL1ParticleDeferredTarget\n\t");
}
#else
inline void KL1ParticleDeferredCallHook(){}
#endif
}
