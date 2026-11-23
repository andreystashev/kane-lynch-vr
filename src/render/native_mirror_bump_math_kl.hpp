#pragma once
#include "native_effects_math_kl.hpp"
#include <algorithm>
#include <cmath>
namespace NativeMirrorBumpMathKL {
struct TextureViewport {unsigned x=0,y=0,width=0,height=0,targetWidth=0,targetHeight=0;};
// c0..3 are columns of row-vector WORLD*VIEW*P. c4..7 are the rows of
// WORLD*VIEW*P*textureBias (native bias copies W into the unused Z column).
// This exact dual convention is in the native VS and captured constants.
// Certify both against the accepted eye, so another pass cannot borrow an
// otherwise live main camera authorization and modify its material constants.
inline bool Matches(const float world[12],const NativeCameraMath::CameraPose& eye,
                    const float projection[16],const float shader[32],float* expected=nullptr,float* bounds=nullptr,
                    const TextureViewport* texture=nullptr,bool* mainMatches=nullptr,
                    float modelScale=1.f,float modelDepthBias=0.f) {
    if(mainMatches)*mainMatches=false;
    if(!world||!projection||!shader||!std::isfinite(modelScale)||modelScale<=0||
       !std::isfinite(modelDepthBias)||modelDepthBias<0||modelDepthBias>255||projection[0]<=0||projection[5]<=0||
       projection[14]>=0||std::fabs(projection[11]-1)>1e-5f||std::fabs(projection[15])>1e-5f)return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(projection[i]))return false;
    if(texture&&(!texture->width||!texture->height||!texture->targetWidth||!texture->targetHeight||
       texture->x>texture->targetWidth||texture->width>texture->targetWidth-texture->x||
       texture->y>texture->targetHeight||texture->height>texture->targetHeight-texture->y))return false;
    if(!NativeCameraMath::ValidatePose(eye))return false;
    for(unsigned i=0;i<12;++i)if(!std::isfinite(world[i]))return false;
    // The native path converts the eye to a float VIEW, then multiplies
    // WORLD*VIEW*P (and texture bias) through D3DX. A fixed translation epsilon
    // rejects legitimate cancellation at large world coordinates. Bound every
    // rounding by gamma_n=sum-product error, using absolute input products;
    // this covers either association of the native matrix multiplications.
    constexpr double unit=0x1p-24;
    const auto gamma=[](unsigned n){return n*unit/(1.-n*unit);};
    const NativeCameraMath::Vec3 axes[]{eye.basis.right,eye.basis.up,eye.basis.forward};
    const double object[4][3]={{world[6],world[7],world[8]},{world[3],world[4],world[5]},
        {world[0],world[1],world[2]},{world[9],world[10],world[11]}};
    const double position[]{eye.position.x,eye.position.y,eye.position.z};
    double wv[16]{},absolute[16]{},inputError[16]{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c){
        const double axis[]{axes[c].x,axes[c].y,axes[c].z};
        for(unsigned k=0;k<3;++k){
            wv[r*4+c]+=(object[r][k]-(r==3?position[k]:0))*axis[k];
            absolute[r*4+c]+=(std::fabs(object[r][k])+(r==3?std::fabs(position[k]):0))*std::fabs(axis[k]);
            if(r==3)inputError[r*4+c]+=gamma(5)*std::fabs(position[k]*axis[k]);
        }
    }
    wv[15]=absolute[15]=1;
    // Retail semantic1 (48F763..48F850) scales the first three WV columns
    // by state+3D4, then contracts its translation radially by byte(3D8).
    // This belongs to raster depth placement only: the mirror UV semantic7
    // is computed from the unmodified WV. Applying either operation to UV
    // would make the reflected image slide across the floor.
    double modelWV[16]{},modelAbsolute[16]{},modelError[16]{};
    for(unsigned i=0;i<16;++i){
        if(i%4==3){modelWV[i]=wv[i];modelAbsolute[i]=absolute[i];modelError[i]=inputError[i];}
        else {modelWV[i]=wv[i]*modelScale;modelAbsolute[i]=absolute[i]*modelScale;
            modelError[i]=inputError[i]*modelScale+gamma(1)*std::fabs(modelWV[i]);}
    }
    const unsigned bias=static_cast<unsigned>(modelDepthBias);
    if(bias){
        const double distance=std::sqrt(modelWV[12]*modelWV[12]+modelWV[13]*modelWV[13]+modelWV[14]*modelWV[14]);
        if(!std::isfinite(distance))return false;
        if(distance>bias){
            const double factor=(distance-bias)/distance;
            // The radial map x*(1-b/|x|) is nonexpansive outside the bias
            // sphere. Bound the inherited three-component dot errors and
            // the native length/subtract/divide/multiply stores separately.
            const double inherited=std::sqrt(modelError[12]*modelError[12]+modelError[13]*modelError[13]+modelError[14]*modelError[14]);
            for(unsigned i=12;i<15;++i){modelWV[i]*=factor;
                modelError[i]=inherited+gamma(10)*(distance+bias);
                modelAbsolute[i]+=bias;}
        }
    }
    double m[16]{},condition[16]{},propagated[16]{},model[16]{},modelCondition[16]{},modelPropagated[16]{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
        for(unsigned k=0;k<4;++k){m[r*4+c]+=wv[r*4+k]*projection[k*4+c];
            condition[r*4+c]+=absolute[r*4+k]*std::fabs(projection[k*4+c]);
            propagated[r*4+c]+=inputError[r*4+k]*std::fabs(projection[k*4+c]);
            model[r*4+c]+=modelWV[r*4+k]*projection[k*4+c];
            modelCondition[r*4+c]+=modelAbsolute[r*4+k]*std::fabs(projection[k*4+c]);
            modelPropagated[r*4+c]+=modelError[r*4+k]*std::fabs(projection[k*4+c]);}
    bool matches=true,main=true;
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c) {
        double uv=c==0?.5*(m[r*4]+m[r*4+3]):c==1?.5*(-m[r*4+1]+m[r*4+3]):m[r*4+3];
        const double a=shader[c*4+r],b=shader[16+r*4+c];
        double uvCondition=c==0?.5*(condition[r*4]+condition[r*4+3]):c==1?.5*(condition[r*4+1]+condition[r*4+3]):condition[r*4+3];
        double uvInput=c==0?.5*(propagated[r*4]+propagated[r*4+3]):c==1?.5*(propagated[r*4+1]+propagated[r*4+3]):propagated[r*4+3];
        // Retail semantic7 (48F9A2..48FC42) composes W*V*P*B*Q.
        // Q is the actual D3D viewport scaled/offset into RT0, not scissor.
        if(texture&&c<2){
            const auto size=c==0?texture->targetWidth:texture->targetHeight;
            const double scale=float(double(c==0?texture->width:texture->height)/size);
            const double offset=float(double(c==0?texture->x:texture->y)/size);
            uv=uv*scale+m[r*4+3]*offset;
            uvCondition=uvCondition*scale+condition[r*4+3]*offset;
            uvInput=uvInput*scale+propagated[r*4+3]*offset;
        }
        const double mainLimit=gamma(15)*modelCondition[r*4+c]+(1+gamma(15))*modelPropagated[r*4+c];
        // Three/four dot4 stages (7 rounded ops each); Q adds one division.
        const auto uvOps=texture?29u:21u;
        const double uvLimit=gamma(uvOps)*uvCondition+(1+gamma(uvOps))*uvInput;
        if(expected){expected[c*4+r]=float(model[r*4+c]);expected[16+r*4+c]=float(uv);}
        if(bounds){bounds[c*4+r]=float(mainLimit);bounds[16+r*4+c]=float(uvLimit);}
        const bool mainOkay=std::isfinite(a)&&std::isfinite(mainLimit)&&std::fabs(a-model[r*4+c])<=mainLimit;
        main=main&&mainOkay;
        if(!mainOkay||!std::isfinite(b)||!std::isfinite(uvLimit)||std::fabs(b-uv)>uvLimit)matches=false;
    }
    if(mainMatches)*mainMatches=main;
    return matches;
}
// A planar RT stores the reflected camera image. Projecting it by the main
// eye is equivalent only for points exactly ON the reflector plane. Authored
// strips/steps outside that plane require the RT producer's actual VIEW/P.
// inverseView is the packed native reflected VIEW (improper transform).
inline bool TargetProjection(const float world[12],const NativeCameraMath::CameraPose& inverseView,
                             const float projection[16],const TextureViewport& texture,float output[16]) {
    if(!world||!projection||!output||!texture.width||!texture.height||!texture.targetWidth||!texture.targetHeight||
       texture.x>texture.targetWidth||texture.width>texture.targetWidth-texture.x||
       texture.y>texture.targetHeight||texture.height>texture.targetHeight-texture.y)return false;
    const auto& b=inverseView.basis;
    const double w[16]{world[6],world[7],world[8],0,world[3],world[4],world[5],0,
        world[0],world[1],world[2],0,world[9],world[10],world[11],1};
    const double v[16]{b.right.x,b.right.y,b.right.z,0,b.up.x,b.up.y,b.up.z,0,
        b.forward.x,b.forward.y,b.forward.z,0,inverseView.position.x,inverseView.position.y,inverseView.position.z,1};
    double wv[16]{},m[16]{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)for(unsigned k=0;k<4;++k)wv[r*4+c]+=w[r*4+k]*v[k*4+c];
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)for(unsigned k=0;k<4;++k)m[r*4+c]+=wv[r*4+k]*projection[k*4+c];
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        const auto size=c==0?texture.targetWidth:texture.targetHeight;
        const double scale=float(double(c==0?texture.width:texture.height)/size);
        const double offset=float(double(c==0?texture.x:texture.y)/size);
        double value=c==0?.5*(m[r*4]+m[r*4+3]):c==1?.5*(-m[r*4+1]+m[r*4+3]):m[r*4+3];
        if(c<2)value=value*scale+m[r*4+3]*offset;
        if(!std::isfinite(value)||std::fabs(value)>1.e12)return false;
        output[r*4+c]=float(value);
    }
    return true;
}

}
