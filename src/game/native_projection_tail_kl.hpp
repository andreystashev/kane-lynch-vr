#pragma once
#include <cstdint>

namespace NativeProjectionTailKL {
using Observer=void(*)(std::uintptr_t);
inline Observer observer=nullptr;
using PacketObserver=void(*)(std::uintptr_t state,std::uintptr_t packet,unsigned phase);
// 0: queued projection copy, 1: queued projection-stack pop. This is an
// optional, separate observer; the historical cinematic callback ABI and
// its copy-only call order remain unchanged.
inline PacketObserver packetObserver=nullptr;
}
// Hook only the five-byte MOV ECX,16 at 4B54D0. The shared FSTP at
// 4B54DB is also a default-packet entry and MUST remain unmodified.
// Reproduce MOV/REP here, then resume the original MOV/FSTP at 4B54D7.
// Preserve all integer registers,
// flags, x87 stack/control and XMM registers: original code still has one
// live x87 value to pop at the relocated FSTP immediately after this hook.
extern "C" {
__attribute__((used)) inline void* KL1ProjectionTailOriginal=nullptr;
__attribute__((used)) inline void* KL1ProjectionPopOriginal=nullptr;
__attribute__((used,noinline)) inline void __cdecl KL1ProjectionTailObserved(std::uintptr_t end) {
    if(end>=0x10438u&&NativeProjectionTailKL::observer)NativeProjectionTailKL::observer(end-0x438u);
}
__attribute__((used,noinline)) inline void __cdecl KL1ProjectionPacketObserved(std::uintptr_t end,std::uintptr_t packet,unsigned phase) {
    if(end>=0x10438u&&packet>=0x10000u&&phase<=1&&NativeProjectionTailKL::packetObserver)
        NativeProjectionTailKL::packetObserver(end-0x438u,packet,phase);
}
__attribute__((naked,used,noinline)) inline void KL1ProjectionTailHook() {
    __asm__ __volatile__(
        "movl $16,%ecx\n\trep movsl\n\t"
        "pushfl\n\tpushal\n\tmovl %esp,%ebp\n\t"
        "subl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tfninit\n\t"
        "subl $12,%esp\n\tpushl 0(%ebp)\n\tcall _KL1ProjectionTailObserved\n\taddl $16,%esp\n\t"
        // PUSHAD stores original EBP (the native queued packet) at +8;
        // original EDI at +0 is the end of renderer-state projection.
        "subl $4,%esp\n\tpushl $0\n\tpushl 8(%ebp)\n\tpushl 0(%ebp)\n\t"
        "call _KL1ProjectionPacketObserved\n\taddl $16,%esp\n\t"
        "fxrstor (%esp)\n\tmovl %ebp,%esp\n\tpopal\n\tpopfl\n\t"
        "jmp *_KL1ProjectionTailOriginal\n\t");
}
// The stack-pop decoder uses a different REP at4B556E and does not flow
// through the copy hook. Replace only its five-byte MOV ECX,16 at4B5569,
// reproduce REP and resume the untouched MOV EDI,[ESP+10] at4B5570.
__attribute__((naked,used,noinline)) inline void KL1ProjectionPopHook() {
    __asm__ __volatile__(
        "movl $16,%ecx\n\trep movsl\n\t"
        "pushfl\n\tpushal\n\tmovl %esp,%ebp\n\t"
        "subl $528,%esp\n\tandl $-16,%esp\n\tfxsave (%esp)\n\tfninit\n\t"
        "subl $4,%esp\n\tpushl $1\n\tpushl 8(%ebp)\n\tpushl 0(%ebp)\n\t"
        "call _KL1ProjectionPacketObserved\n\taddl $16,%esp\n\t"
        "fxrstor (%esp)\n\tmovl %ebp,%esp\n\tpopal\n\tpopfl\n\t"
        "jmp *_KL1ProjectionPopOriginal\n\t");
}
}
