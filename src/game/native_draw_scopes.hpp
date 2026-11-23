#pragma once
// Reversible native draw adapters share one D3D callback. Begin in order,
// finish in reverse order even when the native draw or inner restore failed.
// Nested device draws keep the outer adapters and do not acquire them again.
struct NativeDrawScopes {
    bool active=false;void* first=nullptr;void* second=nullptr;void* third=nullptr;void* fourth=nullptr;
    template<class Device,class BeginFirst,class BeginSecond>void* Begin(Device* device,BeginFirst beginFirst,BeginSecond beginSecond){
        return Begin(device,beginFirst,beginSecond,[](Device*)->void*{return nullptr;});
    }
    template<class Device,class BeginFirst,class BeginSecond,class BeginThird>void* Begin(Device* device,BeginFirst beginFirst,BeginSecond beginSecond,BeginThird beginThird){
        return Begin(device,beginFirst,beginSecond,beginThird,[](Device*)->void*{return nullptr;});
    }
    template<class Device,class BeginFirst,class BeginSecond,class BeginThird,class BeginFourth>void* Begin(Device* device,BeginFirst beginFirst,BeginSecond beginSecond,BeginThird beginThird,BeginFourth beginFourth){
        if(active)return nullptr;active=true;first=beginFirst(device);second=beginSecond(device);third=beginThird(device);fourth=beginFourth(device);
        if(!first&&!second&&!third&&!fourth){active=false;return nullptr;}return this;
    }
    template<class Result,class EndFirst,class EndSecond>Result Finish(void* token,Result result,EndFirst endFirst,EndSecond endSecond){
        return Finish(token,result,endFirst,endSecond,[](void*,Result value){return value;});
    }
    template<class Result,class EndFirst,class EndSecond,class EndThird>Result Finish(void* token,Result result,EndFirst endFirst,EndSecond endSecond,EndThird endThird){
        return Finish(token,result,endFirst,endSecond,endThird,[](void*,Result value){return value;});
    }
    template<class Result,class EndFirst,class EndSecond,class EndThird,class EndFourth>Result Finish(void* token,Result result,EndFirst endFirst,EndSecond endSecond,EndThird endThird,EndFourth endFourth){
        if(token!=this||!active)return result;
        if(fourth)result=endFourth(fourth,result);
        if(third)result=endThird(third,result);
        if(second)result=endSecond(second,result);
        if(first)result=endFirst(first,result);
        first=second=third=fourth=nullptr;active=false;return result;
    }
};
