// Included inside ReloadDiagnostic after the shared contacts, view and feedback.
namespace ShotgunReload {
namespace Tube = ShotgunReloadKL;

void Observe(Contacts& c,const ReloadPartsKL::Part& part,const CameraPose& skin,
             const CameraPose& wrist,DWORD now) {
    if(c.category!=Tube::Category&&c.category!=15)return;
    auto& p=c.shotgun;
    if(!std::strcmp(part.name,"w_bone_frame")){
        if(c.category==Tube::Category)Tube::FindSockets(image,part,ReadBytes,p);
        else p.resourceModel=part.model;
        p.frameSkin=ReloadMathKL::ToLocal(wrist,skin);
        p.frameTick=now;
    }
    if(!std::strcmp(part.name,"w_bone_slide"))p.slideTick=now;
}

void SuppressShell(const View& v,const ReloadPartsKL::Part& part) {
    // Some native shells bind the engine's white placeholder material. The
    // owned loading prop has its own finish, using the measured shell shape.
    if(v.loader==PhysicalReloadKL::Loader::Shell&&v.held&&!std::strcmp(part.name,"w_bone_shell")){
        hidePending=true;++hidden;
    }
}

struct Selection {
    NativeReloadAmmoKL::Snapshot owner{};
    Tube::State state{};
    struct Breech {
        bool valid=false,palmValid=false;
        float units=0;
        std::uint32_t recenter=0;
        CameraPose gun{},pump{},palm{};
        Tube::Rig rig{};
        Vec3 pumpHalf{};
    } breech;
};
Selection selected;

bool IntegratedTube(const NativeReloadAmmoKL::Snapshot& source,const Contacts& c){
    return source.category==Tube::Category&&source.ammo.capacity==12&&
        !(c.item==source.item&&c.model==source.model&&c.reference==source.reference&&c.clip);
}

bool WaitForIntegrated(const NativeReloadAmmoKL::Snapshot& source,const Contacts& c,DWORD now){
    if(!IntegratedTube(source,c))return false;
    const auto previous=Current();
    if(previous.loader==PhysicalReloadKL::Loader::Shell&&previous.player==source.player&&previous.item==source.item&&
       previous.model==source.model&&previous.reference==source.reference&&now-previous.tick<=150)return true;
    View waiting;
    waiting.loader=PhysicalReloadKL::Loader::Shell;waiting.category=source.category;waiting.player=source.player;
    waiting.item=source.item;waiting.model=source.model;waiting.reference=source.reference;waiting.tick=now;
    waiting.blocked=source.ammo.count==0;
    Publish(waiting);
    return true;
}

bool Tick(const NativeReloadAmmoKL::Snapshot& source,const Contacts& c,
          const NativeHandsKL::VisualFrame& hands,DWORD now,bool scoped) {
    const auto& p=c.shotgun;
    const bool breech=source.category==15&&source.ammo.capacity<=5;
    const bool shotgun=source.category==Tube::Category;
    const bool sameParts=c.item==source.item&&c.model==source.model&&c.reference==source.reference&&!c.clip;
    const bool sameFrame=sameParts&&c.frame&&p.frameTick&&now-p.frameTick<=150;
    const bool sameSlide=sameParts&&c.slide&&p.slideTick&&now-p.slideTick<=150;
    const bool partsFresh=sameFrame&&sameSlide;
    const bool tube=shotgun&&partsFresh&&p.catalogRead&&p.shell.bindValid&&p.hatch.bindValid;
    const bool integrated=IntegratedTube(source,c);
    const bool knownOwner=NativeReloadAmmoKL::SameOwner(selected.owner,source);
    if(!knownOwner)selected={source,{},{}};
    auto& cached=selected.breech;
    const bool useBreech=scoped&&breech&&cached.valid&&cached.recenter==hands.recenterAck&&cached.units==hands.unitsPerMetre;
    if(!tube&&!integrated&&!(breech&&(partsFresh||useBreech))){
        if(scoped)return false;
        selected.state.Suspend();
        return false;
    }
    const auto wrist=hands.finalWrist[1];
    const auto skin=ReloadMathKL::ToWorld(wrist,p.frameSkin);
    CameraPose shell,hatch;
    Tube::Frame f;f.valid=true;f.breech=breech;f.integrated=integrated;f.scoped=scoped;
    f.tick=now;f.sample=hands.sourcePoseTick;f.reference=hands.recenterAck;
    f.units=hands.unitsPerMetre;f.grip=hands.grip[0];f.supportHeld=hands.supportLatched;
    const bool palmValid=useBreech?cached.palmValid:hands.palmValid[0];
    const auto palmLocal=useBreech?cached.palm:hands.palmLocal[0];
    f.palm=palmValid?ReloadMathKL::ToWorld(hands.controllerWrist[0],palmLocal):hands.controllerWrist[0];
    f.driver=hands.controllerWrist[0].position;
    bool tubeMeasured=false;
    const auto& controller=hands.controllerWrist[1];
    if(useBreech){
        f.gun=ReloadMathKL::ToWorld(controller,cached.gun);
        f.pump.box={ReloadMathKL::ToWorld(controller,cached.pump),cached.pumpHalf};
        f.rig={ReloadMathKL::ToWorld(controller,cached.rig.contact),ReloadMathKL::ToWorld(controller,cached.rig.seated),cached.rig.shellHalf};
    }else{
        NativeWeaponKL::AimFrame aim;
        if(!SnapshotAim(aim)||aim.player!=source.player||aim.weapon!=source.item||
           !ReloadPresentationKL::Gun(wrist,aim.direction,f.gun)){
            return integrated&&WaitForIntegrated(source,c,now);
        }
        G::SlideContact observedPump;
        const bool pumpMeasured=sameSlide&&G::Slide(ReloadMathKL::ToWorld(wrist,c.slideBone),c.slidePart.centre,c.slidePart.half,
            f.units,observedPump,source.category,&f.gun);
        if(pumpMeasured)f.pump=observedPump;
        tubeMeasured=tube&&pumpMeasured&&G::BoneWorld(skin,p.shell.bind,shell)&&G::BoneWorld(skin,p.hatch.bind,hatch)&&
            Tube::Measure(shell,p.shell.centre,p.shell.half,hatch,f.gun,f.units,f.rig);
        if(shotgun&&!tubeMeasured){
            G::Bounds frame=c.frameBox;frame.world=ReloadMathKL::ToWorld(wrist,frame.world);
            const bool sameSupport=hands.foregripItem==source.item&&hands.foregripModel==source.model&&
                hands.foregripReference==source.reference;
            const auto foregrip=Add(wrist.position,ReloadMathKL::Transform(wrist.basis,hands.foregripSocket));
            if(!integrated||!Tube::TubeWithoutParts(f.gun,aim.origin,f.units,f.rig,f.pump,sameFrame?&frame:nullptr,
                sameSupport?&foregrip:nullptr))return integrated&&WaitForIntegrated(source,c,now);
            if(pumpMeasured)f.pump=observedPump;
        }else if(breech){
            if(!pumpMeasured)return false;
            const auto& box=f.pump.box;
            f.rig.seated={f.gun.basis,Add(box.world.position,Scale(f.gun.basis.forward,box.half.z*.65f))};
            f.rig.contact=f.rig.seated;
            f.rig.contact.position=Add(f.rig.contact.position,Scale(f.gun.basis.up,.035f*f.units));
            f.rig.shellHalf=Scale(Vec3{.005f,.005f,.035f},f.units);
        }
    }
    // The cargo-borrow and attachment transition can temporarily leave the
    // whole mesh away from the owning hand. Such a socket cannot receive a
    // tracked shell and must not claim the original reload.
    const auto reach=Scale(Subtract(f.rig.contact.position,wrist.position),1.f/f.units);
    if(Dot(reach,reach)>.85f*.85f){
        return integrated&&WaitForIntegrated(source,c,now);
    }
    if(breech&&!useBreech){
        cached={true,palmValid,f.units,f.reference,ReloadMathKL::ToLocal(controller,f.gun),
            ReloadMathKL::ToLocal(controller,f.pump.box.world),palmLocal,
            {ReloadMathKL::ToLocal(controller,f.rig.contact),ReloadMathKL::ToLocal(controller,f.rig.seated),f.rig.shellHalf},
            f.pump.box.half};
    }
    f.integrated=integrated&&!tubeMeasured;
    f.cartridge=Tube::ShellGrip(f.palm,f.rig.shellHalf,f.units,breech);
    ReloadBodyPocketKL::Input body;
    body.player=source.player;body.scene=source.reference;body.reference=f.reference;body.pairId=hands.pairId;
    body.tick=now;body.sample=f.sample;body.units=f.units;body.head=hands.centre;
    body.rootAnchor=hands.rawRoot.position;body.rootAnchorValid=hands.bodyAvailableForMesh;
    ReloadBodyPocketKL::Volume volume;
    f.supply=pocket.Step(body,volume)&&(breech?
        ReloadBodyPocketKL::ShoulderContains(volume,f.palm.position)||ReloadBodyPocketKL::Contains(volume,f.palm.position):
        ReloadBodyPocketKL::CanSupply(volume,f.palm.position,f.gun.position));

    const auto before=selected.state;
    auto& state=selected.state;
    auto ammo=source.ammo;
    const DWORD tap=breech?ReloadTap(now):0;
    f.boltButton=tap!=0;
    const auto events=state.Step(f,ammo);
    if(tap&&state.lastSample!=before.lastSample)consumedReload=tap;
    if(state.held&&BackgroundDiagnostic::Controls()){
        static DWORD lastTrace=0;
        if(now-lastTrace>=100){
            lastTrace=now;
            const auto delta=Scale(ReloadMathKL::Inverse(f.rig.contact.basis,
                Subtract(f.cartridge.position,f.rig.contact.position)),1.f/f.units);
            wchar_t folder[32768]{};
            if(GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",folder,32768))
                if(FILE* file=_wfopen((std::wstring(folder)+L"\\shotgun-contact.jsonl").c_str(),L"ab")){
                    const auto metres=[&](Vec3 point){return Scale(ReloadMathKL::Inverse(hands.centre.basis,
                        Subtract(point,hands.centre.position)),1.f/f.units);};
                    const auto port=metres(f.rig.contact.position), right=metres(wrist.position), left=metres(f.palm.position);
                    const auto origin=metres(skin.position);
                    fprintf(file,"{\"ageMs\":%lu,\"phase\":%u,\"grip\":%.3f,\"contactMetres\":[%.5f,%.5f,%.5f],\"alignment\":%.6f,\"units\":%.3f,\"headToPort\":[%.5f,%.5f,%.5f],\"headToRight\":[%.5f,%.5f,%.5f],\"headToLeft\":[%.5f,%.5f,%.5f],\"headToSkin\":[%.5f,%.5f,%.5f],\"palm\":%s,\"recenter\":%u,\"sampleAgeMs\":%lu}\n",
                        BackgroundDiagnostic::Age(),unsigned(state.phase),f.grip,delta.x,delta.y,delta.z,
                        Dot(f.cartridge.basis.forward,f.rig.contact.basis.forward),f.units,port.x,port.y,port.z,
                        right.x,right.y,right.z,left.x,left.y,left.z,origin.x,origin.y,origin.z,
                        hands.palmValid[0]?"true":"false",f.reference,static_cast<unsigned long>(now-f.sample));
                    fclose(file);
                }
        }
    }
    if(ammo.count!=source.ammo.count||ammo.reserve!=source.ammo.reserve){
        const auto capacity=[](std::uintptr_t item,std::uintptr_t method){
            using Get=int(__thiscall*)(void*);return reinterpret_cast<Get>(method)(reinterpret_cast<void*>(item));
        };
        const auto setter=[](std::uintptr_t method,std::uintptr_t item,int value){
            using Set=void(__thiscall*)(void*,int);reinterpret_cast<Set>(method)(reinterpret_cast<void*>(item),value);
        };
        if(!NativeReloadAmmoKL::Commit(image,source,ammo,ReadBytes,capacity,setter)){
            state=before;++rejected;
            Invalidate();
            return true;
        }
    }
    if(events){
        Feedback(events,source.category,now);
        if(events&M::Take)++taken;
        if(events&M::Insert)++inserted;
        if(events&M::Forward)++racked;
        if(BackgroundDiagnostic::Active()){
            wchar_t folder[32768]{};
            if(GetEnvironmentVariableW(L"KL1_BACKGROUND_RUN",folder,32768))
                if(FILE* file=_wfopen((std::wstring(folder)+L"\\manual-reload.jsonl").c_str(),L"ab")){
                    fprintf(file,"{\"ageMs\":%lu,\"loader\":\"shell\",\"events\":%u,\"countBefore\":%d,\"countAfter\":%d,\"reserveBefore\":%d,\"reserveAfter\":%d,\"scoped\":%s,\"nativeAcknowledged\":true,\"scriptedContacts\":false}\n",
                        BackgroundDiagnostic::Age(),events,source.ammo.count,ammo.count,source.ammo.reserve,ammo.reserve,scoped?"true":"false");
                    fclose(file);
                }
        }
    }
    View next;
    next.loader=PhysicalReloadKL::Loader::Shell;next.category=source.category;next.breech=breech;next.scoped=scoped;
    next.integratedTube=integrated&&!tubeMeasured;
    next.ready=true;next.player=source.player;next.item=source.item;next.model=source.model;
    next.reference=source.reference;next.recenter=f.reference;next.tick=now;next.inserted=true;next.held=state.held;
    next.shellRounds=state.held?state.carried-state.committed:0;
    if(next.shellRounds){
        next.magazine=state.shellPoses[state.committed];
        for(unsigned i=1;i<next.shellRounds;++i)next.spareShells[i-1]=state.shellPoses[state.committed+i];
    }
    next.slideReturning=state.returning||state.opening;next.boltOpen=state.boltOpen;next.reloadSocket=f.rig.contact;next.magHalf=f.rig.shellHalf;
    next.slideTravel=state.slideTravel;next.phase=unsigned(state.phase);next.wrist=wrist;next.units=f.units;
    next.rounds=ammo.count;next.reserve=ammo.reserve;next.needsChamber=state.needsPump;
    next.slideAllowed=!scoped&&!state.held&&!state.opening&&!state.returning&&
        (state.phase==PhysicalReloadKL::Phase::Slide||Tube::CanGrabPump(f.integrated,state.needsPump,ammo.count));
    if(next.slideAllowed){
        auto contact=f.pump.box.world;
        if(state.boltOpen)contact.position=Subtract(contact.position,Scale(f.gun.basis.forward,state.slideTravel*f.units));
        next.slideLocal.box={ReloadMathKL::ToLocal(wrist,contact),f.pump.box.half};
    }
    next.blocked=!ammo.ready||(state.phase==PhysicalReloadKL::Phase::Slide&&state.slideTravel>.003f);
    next.supportBusy=state.held||state.phase==PhysicalReloadKL::Phase::Slide||(state.needsPump&&(breech||ammo.count>0));
    next.barrelLocal=ReloadMathKL::Inverse(wrist.basis,f.gun.basis.forward);
    const auto previous=Current();
    next.slideAnchor=f.pump.box.world;
    if(state.phase==PhysicalReloadKL::Phase::Slide&&!next.slideReturning)
        next.slideAnchor.position=Add(f.gun.position,ReloadMathKL::Transform(f.gun.basis,state.pumpAnchorLocal));
    next.slideHand=before.phase==PhysicalReloadKL::Phase::Slide&&previous.item==next.item&&previous.reference==next.reference?
        previous.slideHand:ReloadMathKL::ToLocal(next.slideAnchor,hands.finalWrist[0]);
    next.palmLocal=palmLocal;next.palmValid=palmValid;
    Publish(next);
    return true;
}

bool ReplayPose(const View& v,const NativeHandsKL::VisualFrame& hands,CameraPose& palm,float& grip) {
    const DWORD age=BackgroundDiagnostic::Age();
    const auto phase=static_cast<PhysicalReloadKL::Phase>(v.phase);
    if(age<30900||age>37200)return false;
    static bool delivered=false;
    static DWORD alignedAt=0,heldAt=0;
    const auto right=hands.centre.basis.right;
    const auto wrist=hands.finalWrist[1];
    if(v.held){
        if(!heldAt)heldAt=age;
        if(age-heldAt<225){
            palm.position=Subtract(wrist.position,Scale(right,.32f*v.units));
            grip=1;
            return true;
        }
        auto socket=ReloadMathKL::ToWorld(wrist,ReloadMathKL::ToLocal(v.wrist,v.reloadSocket));
        socket.position=Subtract(socket.position,Scale(socket.basis.up,.015f*v.units));
        if(v.integratedTube&&!v.breech)socket.position=Subtract(socket.position,Scale(socket.basis.right,.06f*v.units));
        palm=Tube::PalmForShell(socket,v.magHalf,v.units,v.breech);
        if(phase==PhysicalReloadKL::Phase::Seated&&!alignedAt)alignedAt=age;
        grip=phase==PhysicalReloadKL::Phase::Holding||
            (phase==PhysicalReloadKL::Phase::Seated&&age-alignedAt<110)?1.f:0.f;
        if(phase==PhysicalReloadKL::Phase::Finishing)delivered=true;
        return true;
    }
    if(v.breech&&BackgroundDiagnostic::Rack()&&!v.boltOpen&&!delivered&&age<31800){
        palm.position=ReloadMathKL::ToWorld(wrist,ReloadMathKL::ToLocal(v.wrist,v.slideAnchor)).position;
        const float pull=std::clamp((float(age)-31250.f)/400.f,0.f,1.f);
        palm.position=Subtract(palm.position,Scale(ReloadMathKL::Transform(wrist.basis,v.barrelLocal),.073f*pull*v.units));
        grip=age>=31100?1.f:0.f;
        return true;
    }
    if(!v.breech&&delivered&&BackgroundDiagnostic::Rack()&&age>=33700){
        static DWORD approachedAt=0,grabbedAt=0;
        if(!approachedAt)approachedAt=age;
        palm.position=ReloadMathKL::ToWorld(wrist,ReloadMathKL::ToLocal(v.wrist,v.slideAnchor)).position;
        if(!grabbedAt&&phase==PhysicalReloadKL::Phase::Slide)grabbedAt=age;
        if(!grabbedAt){
            grip=(age-approachedAt)%1050>=250?1.f:0.f;
        }else{
            const auto elapsed=age-grabbedAt;
            const float t=float(elapsed);
            const float first=std::clamp(std::min((t-100.f)/190.f,(710.f-t)/220.f),0.f,1.f);
            const float second=std::clamp(std::min((t-820.f)/190.f,(1430.f-t)/220.f),0.f,1.f);
            const float pull=.073f*std::max(first,second);
            palm.position=Subtract(palm.position,Scale(ReloadMathKL::Transform(wrist.basis,v.barrelLocal),pull*v.units));
            grip=elapsed<1570?1.f:0.f;
        }
        return true;
    }
    grip=0;
    if(delivered||age>=32600)return true;
    ReloadBodyPocketKL::Input in;
    in.player=v.player;in.scene=v.reference;in.reference=hands.recenterAck;in.pairId=hands.pairId;
    in.tick=GetTickCount();in.sample=hands.sourcePoseTick;in.units=hands.unitsPerMetre;in.head=hands.centre;
    in.rootAnchor=hands.rawRoot.position;in.rootAnchorValid=hands.bodyAvailableForMesh;
    ReloadBodyPocketKL::State pocket;
    ReloadBodyPocketKL::Volume volume;
    if(!pocket.Step(in,volume))return false;
    palm.position=Subtract(v.breech?volume.shoulder:volume.pose.position,Scale(hands.centre.basis.right,(v.breech?.35f:.29f)*in.units));
    grip=age>=(v.breech?32050:31300)&&(!v.breech||(v.boltOpen&&phase!=PhysicalReloadKL::Phase::Slide))?1.f:0.f;
    return true;
}
}
