#pragma once
#include <algorithm>
#include <cmath>
struct DoorMomentum {
    bool sampled=false,wasTouched=false,wasHeld=false;
    float angle=0,lastSpeed=0;
    double time=0;
    void reset(){*this={};}
    float Step(double now,float currentAngle,float physicalSpeed,bool touched,bool blocked,float cap,bool held=false){
        if(!std::isfinite(now+currentAngle+physicalSpeed+cap)){reset();return 0;}
        double dt=now-time;
        float speed=std::clamp(physicalSpeed,-cap,cap);
        bool continuous=sampled&&dt>0&&dt<.10;
        if(blocked){lastSpeed=0;wasTouched=wasHeld=false;sampled=false;return 0;}
        const bool release=(wasHeld&&!held)||(wasTouched&&!touched);
        if(continuous&&wasHeld&&!held){
            // A palm impulse already lives in Havok: keep its current velocity
            // (including friction), rather than adding energy on separation.
            speed=lastSpeed;
            lastSpeed=speed;
        }else if(touched){
            if(continuous){
                float measured=std::clamp((currentAngle-angle)/float(dt),-cap,cap);
                // Pose motion remains measurable even when a grabbed/keyframed body reports zero velocity.
                lastSpeed=held?measured:speed;
            }else lastSpeed=speed;
            // HIGGS may keyframe a held body or publish zero angular velocity.
            // Carry corrected pose velocity through the held frame as well.
            if(held)speed=continuous?std::clamp((currentAngle-angle)/float(dt),-cap,cap):speed;
            if(held)lastSpeed=speed;
        }else lastSpeed=speed;
        sampled=true;wasTouched=touched;wasHeld=held;angle=currentAngle;time=now;
        return speed;
    }
};
