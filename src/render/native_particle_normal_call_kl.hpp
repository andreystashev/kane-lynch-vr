#pragma once
// A detour owns the entire five-byte native CALL, not its shared helper.
// Before this site the caller already pushed its one matrix argument.
// Recreate CALL's return-address push, then tail-enter the matching fastcall
// wrapper. Its RET 4 consumes precisely that original argument and resumes
// at the untouched native +5 target (also the normal-null branch target).
extern "C" {
inline void* KL1ParticleNormalReturn=nullptr;
inline void* KL1ParticleNormalTarget=nullptr;
#if defined(__i386__)
__attribute__((naked,used,noinline)) inline void KL1ParticleNormalCallHook(){
    __asm__ __volatile__("pushl _KL1ParticleNormalReturn\n\tjmp *_KL1ParticleNormalTarget\n\t");
}
#else
inline void KL1ParticleNormalCallHook(){}
#endif
}
