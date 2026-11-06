#pragma once

// Physical Collision VR — mod-support interface, version 005.
//
// ---------------------------------------------------------------------------------------------
// LICENCE FOR THIS HEADER
//
// This file is placed in the public domain by the author of Physical Collision VR. Copy it, ship
// it inside your own plugin, modify it, no credit or permission required.
//
// This grant covers THIS HEADER ONLY. Physical Collision VR itself is closed source and nothing
// here licenses its implementation.
//
// It is stated explicitly because a header published without one is useless: a mod author cannot
// safely include a file in their project, let alone redistribute it, on the strength of "it was
// posted for us to use". No key, token or registration is required either -- and never will be.
// A secret compiled into a distributed DLL is readable by anyone who runs `strings` on it, so it
// could gate nothing; what keeps PCVR private is simply that its source is not published.
//
// Requires CommonLibVR. Built against SkyrimVR 1.4.15.
// ---------------------------------------------------------------------------------------------
//
// Exists so a companion mod can shape how heavy each weapon feels without owning any physics of its
// own. PCVR already simulates the weapon as a real body with real mass and a grip-centred inertia
// tensor; a driver mod supplies only per-hand multipliers on the force and torque budgets, and PCVR
// applies them inside the drive it already runs. Realistic Weapon Weight VR is that driver.
//
// This split exists because the alternative was tried and failed badly: a second mod running its own
// spring filter over the same weapon node fought PCVR for the node, could not see the real inertia
// tensor, and diverged into a violent shake. There must be exactly one integrator, and it is here.
//
// Handshake: PCVR BROADCASTS this interface to every listener at kPostLoad and kPostPostLoad.
// Consumers register `RegisterListener(nullptr, cb)` during plugin load and call AcceptInterfaceOffer.
// A name-addressed Dispatch to "PhysicalCollisionVR" is NOT reliable on SkyrimVR's SKSE -- it fails
// to resolve the receiver even when the plugin is loaded -- so the broadcast is the dependable
// direction and the request below is only a fallback.
//
// ABI rule: never reorder or remove a virtual; only append.

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

namespace PcvrAPI {
    inline constexpr std::uint32_t kMessage_GetInterface = 0x50435601;  // 'PCV\1'
    inline constexpr std::uint32_t kMessage_Interface = 0x50435602;     // 'PCV\2'

    struct PcvrApiOffer {
        void* (*GetApiFunction)(unsigned int revision) = nullptr;
    };

    // Multipliers on PCVR's own open-air budgets for one hand. 1.0 leaves PCVR's figures alone.
    // Lower values mean LESS force or torque available, so the weapon accelerates and turns more
    // slowly -- that is what "heavier" means here.
    struct WeightModifier {
        bool enabled = true;      // false: no weight at all for this hand this frame
        float forceScale = 1.0f;  // linear budget multiplier
        float torqueScale = 1.0f;  // angular budget multiplier -- where most of the feel lives
    };

    // Absolute per-hand weight budgets -- everything that decides how heavy a weapon feels. A driver
    // supplying these owns the whole feel, which is why PCVR's own INI carries no weight settings:
    // a user looking for how heavy their weapons are should find every number in the weight mod.
    struct WeightParams {
        bool enabled = true;
        float force = 400.0f;     // newtons available to accelerate the weapon in open air
        float torque = 13.0f;     // newton-metres available to turn it -- the number that matters
        // The weapon hangs off the grip as a mass on a spring, and these are that oscillator's
        // frequency (rad/s) and damping ratio. Below 1.0 it overshoots and rings down, with the
        // amplitude decaying as exp(-damping*spring*t) -- a smooth fade to nothing that no
        // threshold has to terminate. Around 1.0 it stops dead on arrival, which reads as a snap
        // however gently it is done, so the useful range is roughly 0.15 to 0.4.
        float spring = 12.0f;
        float damping = 0.22f;
        float catchUpCm = 18.0f;  // trailing past this raises the budget to close the gap
        float maxLagCm = 14.0f;   // hard ceilings on how far it may EVER trail
        float maxLagDeg = 30.0f;

        // How much of the weapon's lag the HAND takes with it. 0 keeps the arm perfectly 1:1 with
        // the controller and puts every bit of weight in the weapon; above 0 the hand trails a
        // little too, so the controller visibly arrives somewhere before the arm does.
        //
        // Kept as a fraction with a hard ceiling on purpose. Free-running hand lag is what makes a
        // weight system read as input lag rather than as mass, and past a few centimetres the
        // mismatch between where your real hand is and where you see it becomes a comfort problem.
        float handFollow = 0.0f;
        float maxHandLagCm = 6.0f;
        // The same ceiling for the moment the weapon HITS something. A blocked weapon carries the
        // arm in full -- that is what makes a parry read as an impact rather than a stop -- but the
        // only bound on it is `fMaxOffsetCm`, 60cm, ten times the open-air figure. The arm therefore
        // lurches on contact while open-air weight stays tight, which is felt as the arm lagging
        // only when you attack. Applied ONLY while a weight driver is running, so an install without
        // one keeps the original unbounded-to-60cm behaviour.
        float maxHandLagHitCm = 12.0f;
        // Whether the wrist turns with the weapon as well as trailing it. Off by default: rotation
        // mismatch is the least comfortable kind in VR, and a twisting wrist was disliked on test.
        bool handRotation = false;

        // Ask PCVR to write its per-PHYSICS-STEP weight trace ([wtrace]) to its own log. Lives here
        // rather than in PCVR's INI for the same reason every other number does: a user looking for
        // anything to do with weapon weight should find it in the weight mod's config, and PCVR's
        // INI deliberately carries no [Weight] section at all.
        //
        // Expensive and very noisy -- one line per hand per physics step, which is what makes it
        // useful, because a snap is a single-step event and any sampled log has missed it.
        bool trace = false;
    };

    struct IPcvrInterface001 {
        virtual std::uint32_t GetBuildNumber() = 0;

        // False on a build without the weight drive, so a driver can say so plainly instead of
        // silently doing nothing.
        virtual bool IsWeightSupported() = 0;

        // Push this hand's multipliers. Cheap; intended to be called every frame.
        virtual void SetWeightModifier(bool isLeft, const WeightModifier& mod) = 0;

        // Stop overriding: PCVR falls back to its own INI figures for this hand.
        virtual void ClearWeightModifier(bool isLeft) = 0;

        // How far the weapon is currently trailing its target, for a driver's diagnostics.
        virtual void GetWeightLag(bool isLeft, float& outCm, float& outDeg) = 0;

        // Full control: the driver supplies the ABSOLUTE budgets rather than multipliers on PCVR's
        // own figures. Preferred over SetWeightModifier. Same call rate: every frame, per hand.
        virtual void SetWeightParams(bool isLeft, const WeightParams& params) = 0;

        // What PCVR has actually MEASURED about the weapon in this hand, rather than what its form
        // record claims. `outLengthM` is the longest dimension of the real collision shape, in
        // metres -- a far better description of a weapon's length than the form's `reach`, which is
        // an attack-range multiplier.
        //
        // False when there is no weapon twin for that hand yet.
        virtual bool GetWeaponMetrics(bool isLeft, float& outMassKg, float& outLengthM,
                                      float& outInertia) = 0;
    };

    // ---- revision 2 -------------------------------------------------------------------
    //
    // Everything above is the weight drive. This is the general extension surface: what a patch
    // author needs in order to co-operate with PCVR without owning any of its physics.
    //
    // NO KEY, NO TOKEN, NO REGISTRATION. This header is the entire public contract and it is meant
    // to be copied into other people's projects. PCVR's source stays private simply by not being
    // published -- that is what keeps it closed, and nothing here weakens it. A secret compiled into
    // a distributed DLL would be readable by anyone who ran `strings` on it, so it could never gate
    // anything; it would only create the false impression that something was protected.

    // Events PCVR broadcasts as they happen. Sent to EVERY listener, so receiving them needs no
    // interface pointer and no version handshake at all -- register a listener and read `type`.
    inline constexpr std::uint32_t kMessage_Event = 0x50435603;  // 'PCV\3'

    // ALL FIVE ARE SENT. (They were not always: 3.0.0 through 3.4.1 declared five and only ever
    // dispatched Parry and Clash, so a consumer wiring up ProjectileParry waited forever for
    // something that was never coming. If you add a value here, add the dispatch in the same
    // change.)
    enum class EventType : std::uint32_t {
        Parry = 1,            // a committed parry landed on an opponent's weapon
        Block = 2,            // their weapon caught on your guard without a committed swing
        Clash = 3,            // your own two weapons struck each other
        ProjectileParry = 4,  // an arrow or spell was struck out of the air
        HitRefused = 5,       // a blow aimed at the player was judged not to have reached them
    };

    // THREADING: every event reaches your listener on the MAIN THREAD, always.
    //
    // Worth stating because it is not free. HitRefused is decided inside the game's hit processing,
    // which is not reliably the main thread, so PCVR parks those and sends them on the next frame
    // rather than calling you from wherever the engine happened to be. You may cast spells, touch
    // forms and call the interface from a listener without checking anything.
    //
    // `speed` carries the swing speed in units/second for Parry, Block and ProjectileParry, the
    // hands' closing speed for Clash, and for HitRefused the distance in CENTIMETRES that the blow
    // came within -- it is a refusal, so there is no swing speed worth reporting.

    // Deliberately plain data: no RE types, no containers, no pointers. That keeps it readable by a
    // plugin built against a different CommonLib, and keeps the ABI trivially stable.
    struct PcvrEvent {
        std::uint32_t version = 1;  // of this struct; check it before reading anything below
        EventType type{};
        bool isLeft = false;           // which hand, where that means anything
        float speed = 0.0f;            // u/s: swing speed, or closing speed for a clash
        float x = 0.0f;                // where it happened, world position in Skyrim units
        float y = 0.0f;
        float z = 0.0f;
        std::uint32_t actorFormID = 0;  // the other party, or 0 when there isn't one
    };

    struct IPcvrInterface002 : IPcvrInterface001 {
        // Which revision you actually got. Cheap way to confirm the cast was real.
        virtual std::uint32_t GetApiRevision() = 0;

        // TAKE OVER A HAND. While claimed, PCVR stops driving that hand's weapon and plays no
        // impact effects from it -- exactly what a mod that lodges, throws or grabs a weapon needs.
        //
        // This exists because it was learned the hard way: PCVR already stood down for another mod
        // in its DRIVE path but not in its EFFECTS path, so a blade lodged in a body by Immersive
        // Weapon Penetration VR threw parry sparks for as long as it was held there. A mod that can
        // say "this hand is mine" gets both, and does not have to hope PCVR guesses right.
        //
        // `frames` is a lease, not a lock: it counts down, so a mod that crashes or forgets cannot
        // leave the hand disabled forever. Re-assert it every frame while you want it.
        virtual void ClaimHand(bool isLeft, std::uint32_t frames) = 0;
        virtual void ReleaseHand(bool isLeft) = 0;
        virtual bool IsHandClaimed(bool isLeft) = 0;

        // Silence sparks and the clang for a moment WITHOUT giving up collision. For when you want
        // PCVR's physics but your own feedback. Also a countdown lease.
        virtual void SuppressImpactEffects(std::uint32_t frames) = 0;

        // Is there a live physics twin for this hand's weapon right now? False while sheathed, or
        // while the weapon has no collision to borrow.
        virtual bool IsWeaponTwinLive(bool isLeft) = 0;
    };

    // ---- revision 3 -------------------------------------------------------------------
    //
    // Appended inside namespace PcvrAPI, below IPcvrInterface002.
    // Revision 3: the true-pose weight drive. A NEW struct + NEW method, NOT new fields on
    // WeightParams -- an old driver passing the old struct to the old method stays safe, and the
    // new method's argument carries its own layout. Same append-only discipline as revision 2.
    struct WeightParams3 {
        WeightParams base{};              // everything revision 1 already carried
        bool truePose = false;            // master switch: render the twin's true pose, no feel caps
        float gravityAccel = 0.0f;        // m/s^2 downward on the twin, ALREADY policy-scaled by the
                                          // driver (0 for daggers); PCVR applies it, never invents it
        float twoHandForceMult = 2.0f;    // budget multipliers while HIGGS reports two-handing AND
        float twoHandTorqueMult = 2.2f;   // this hand is the sole armed hand -- the other hand must
                                          // carry no live weapon twin of its own, or the multiplier
                                          // is not applied; see docs/MODDER-API.md for why (a
                                          // strapped shield counts as "armed" too, see there)
        std::uint32_t handFollowMode = 2; // 0 = hand stays on controller, 1 = position-only grip
                                          // follow, 2 = full grip-ride (position + rotation, but the
                                          // rotation half additionally depends on PCVR's own INI --
                                          // bHandFollowsRotation -- see docs/MODDER-API.md)
                                          //
                                          // ⚠ THERE IS NO "ROTATION-ONLY" MODE, AND ONE MUST NOT BE
                                          // ADDED. It was tried on 2026-08-28 and reverted the same
                                          // day: the hand's POSITIONAL offset is not merely lag to
                                          // be minimised, it IS how collision is displayed. Zeroing
                                          // it while still reporting that the hand carried the
                                          // transform makes the post-VRIK weapon write skip itself,
                                          // so nothing renders the displacement at all -- weapons
                                          // pass visually through walls tip-first, the hand no
                                          // longer stops on contact, and side hits sink in.
        float momentumWeight = 0.0f;      // 0 = pure vanilla damage .. 1 = pure momentum
        float momentumRefSpeed = 350.0f;  // u/s blade-tip speed that maps to factor 1.0
        float momentumCurve = 1.2f;       // factor = clamp((speed/ref)^curve, min, max)
        float momentumMin = 0.35f;
        float momentumMax = 2.5f;
        float hitStopStrength = 0.0f;     // 0 = off; twin velocity bled on a landed player hit
    };

    struct IPcvrInterface003 : IPcvrInterface002 {
        virtual void SetWeightParams3(bool isLeft, const WeightParams3& prm) = 0;
    };

    // ---- revision 4 -------------------------------------------------------------------
    //
    // Appended inside namespace PcvrAPI, below IPcvrInterface003. Same discipline as revision 3:
    // a NEW struct through a NEW method, so a revision-3 driver keeps calling SetWeightParams3
    // with a layout that has not moved a byte.
    //
    // Revision 4 splits the drive's one shared oscillator into its two real channels. In-headset
    // testing against Blade & Sorcery (2026-08-24) settled what a held weapon actually is: the
    // HAND imposes position -- the hilt stays in the fist -- while the blade's moment of inertia
    // is what fights the WRIST, so the visible weight is rotation lagging about the grip point.
    // One shared spring cannot express that: soft enough for the blade to sweep behind, it also
    // lets the grip itself trail tens of centimetres out of the hand, which reads as telekinesis
    // rather than a held object. The linear channel therefore gets its own stiff, critically-
    // damped spring (the grip pin), and the per-class angular spring stays the weight.
    struct WeightParams4 {
        WeightParams3 base3{};  // everything revision 3 already carried
        // The grip-pin oscillator: frequency (rad/s) and damping ratio for the LINEAR channel
        // only. 0 means "no split" -- the linear channel uses base3.base.spring/damping exactly
        // as revision 3 did, so a driver that sends zeros has changed nothing.
        float springLinear = 0.0f;
        float dampingLinear = 0.0f;
    };

    struct IPcvrInterface004 : IPcvrInterface003 {
        virtual void SetWeightParams4(bool isLeft, const WeightParams4& prm) = 0;
    };

    // ---- revision 5 -------------------------------------------------------------------
    //
    // Appended inside namespace PcvrAPI, below IPcvrInterface004. Same discipline again: a NEW
    // struct through a NEW method.
    //
    // Revision 5 is the pendulum schedule. In a linear oscillator the overshoot size and the
    // die-off rate are the same knob (the damping ratio), and in-headset testing wanted them
    // apart: a tight, controlled return (damping ~0.65 read as right) that still dies off as a
    // slight, visibly diminishing pendulum (which needs ~0.25 -- but 0.25 everywhere reads as
    // "extremely bouncy"). So the ANGULAR damping is scheduled on the size of the remaining
    // error: base3.base.damping while the weapon is genuinely out, easing to dampingSettle over
    // the last few degrees. The tail's amplitude is bounded by the return's own small overshoot,
    // which is what keeps the pendulum subtle by construction.
    struct WeightParams5 {
        WeightParams4 base4{};  // everything revision 4 already carried
        // Damping ratio for the last few degrees of the ANGULAR settle. 0 means "no schedule" --
        // the angular channel uses base4.base3.base.damping at every amplitude, exactly as
        // revision 4 did, so a driver that sends zero has changed nothing.
        float dampingSettle = 0.0f;
    };

    struct IPcvrInterface005 : IPcvrInterface004 {
        virtual void SetWeightParams5(bool isLeft, const WeightParams5& prm) = 0;
    };

    // ---- revision 6 ----------------------------------------------------------------------
    // Appended inside namespace PcvrAPI, below IPcvrInterface005. Adjustable grip: the driver
    // may slide the captured grip along the twin's dominant shape axis (the handle). The slide
    // is applied ON TOP of the natural capture -- PCVR keeps the unslid base and rebuilds
    // weaponInHand from base + slide, then re-runs the grip-centred COM/inertia recompute, so
    // lever arm and felt weight follow the grip automatically. It is deliberately NOT a
    // re-capture: no live pose is read when a slide is applied, which is what keeps it clear
    // of the mid-swing re-capture snap the rest-frames debounce exists to prevent.
    //
    // THREADING: both calls are frame-thread only (call them from a HIGGS frame callback, the
    // same place every other policy call is made). They read and write twin state that the
    // frame thread owns; calling from anywhere else is a data race.
    struct IPcvrInterface006 : IPcvrInterface005 {
        // Command a slide of `metres` along the handle axis, positive toward the blade end.
        // `endPaddingMetres` is how far past the shape bound at each end the grip may go
        // (policy travels with the call; PCVR stores the last value for GetGripInfo).
        // Returns the value actually accepted after clamping to the handle range, which is
        // also what a subsequent GetGripInfo reports as slideNow. 0 restores the natural
        // grip. No live twin -> returns 0 and does nothing.
        virtual float SetGripSlide(bool isLeft, float metres, float endPaddingMetres) = 0;

        // Everything a driver needs for slide math and boundary feedback. axisWorld: the
        // handle axis in world space, unit length, same sign convention as SetGripSlide.
        // slideNow: the currently applied slide. slideMin/slideMax: the clamp range for this
        // twin under the last padding supplied. Returns false when no twin is live.
        virtual bool GetGripInfo(bool isLeft, float axisWorld[3], float& slideNow,
                                 float& slideMin, float& slideMax) = 0;
    };

    // ---- revision 7 ----------------------------------------------------------------------
    // Appended inside namespace PcvrAPI, below IPcvrInterface006. The grip window, measured
    // from the weapon's own collision: where the grip may actually sit along the handle.
    // PCVR derives it at twin creation by ray-profiling the collision shape along its dominant
    // axis -- the true geometry ends (finer than the AABB: empty stations inside the box do
    // not count), and, where the cross-section visibly widens above the natural grip, the
    // underside of the crossguard or axe head becomes the blade-side stop. A shape with no
    // usable profile (a plain box) falls back to its true ends. The 2cm hand inset is already
    // applied to both stops.
    //
    // With this revision SetGripSlide's endPaddingMetres parameter is IGNORED (the window now
    // comes from the weapon itself, not from driver policy); it stays in the signature because
    // this ABI never changes a shipped method.
    struct IPcvrInterface007 : IPcvrInterface006 {
        // Weapon-local axial coordinates in metres along the dominant axis -- the same axis
        // and sign GetGripInfo reports. gripAxialM: where the BASE (natural) grip sits.
        // nearStopM/farStopM: the allowed window for the grip point, nearStop < farStop,
        // insets and any crossguard stop already applied. A commanded slide of s puts the
        // grip at gripAxialM + s, so the legal slide range is
        // [nearStopM - gripAxialM, farStopM - gripAxialM]. Returns false when no twin is live.
        virtual bool GetGripBounds(bool isLeft, float& gripAxialM, float& nearStopM,
                                   float& farStopM) = 0;
    };

    // ---- revision 8 ------------------------------------------------------------------------
    //
    // Two-handed independent grips. While HIGGS is two-handing, one hand always anchors the
    // weapon and the other's grip travels along it; which hand anchors is a single HIGGS setting,
    // and PCVR borrows it for the length of a slide and always gives it back. A driver drives
    // this by telling PCVR when the OFF hand's slide begins and ends -- there is nothing to send
    // per frame, because while the off hand is sliding the weapon is simply held still by the
    // main hand and the player's real hand travels along it.
    //
    // The main hand's slide needs no call at all: PCVR already sees SetGripSlide and hands the
    // anchor to the off hand for the duration by itself.
    //
    // All three are frame-thread only, and all three are inert unless PCVR proved HIGGS's private
    // layout on this process at runtime (it is build-locked to HIGGS 1.10.10 and disarmed
    // otherwise). Treat a false return as "not available", never as an error.
    struct IPcvrInterface008 : IPcvrInterface007 {
        // Which hand is which right now. `offIsLeft` is the hand that reached out and took hold
        // of the weapon the other hand has equipped; `mainIsLeft` is the equipped hand. False if
        // nobody is two-handing, or if the internals are unavailable. This is a question HIGGS's
        // own public IsTwoHanding() cannot answer -- it ORs both hands.
        virtual bool GetTwoHandingHands(bool& mainIsLeft, bool& offIsLeft) = 0;

        // The off hand's trigger went down. The main hand takes the anchor, so the weapon stays
        // where it is and this hand slides freely along it. False if the internals are disarmed,
        // this hand is not two-handing, or the other hand's slide already holds the anchor -- in
        // which case the driver should leave the gesture alone rather than do something else.
        virtual bool BeginOffHandSlide(bool offIsLeft) = 0;

        // Released. PCVR adopts the hold where the hand actually ended up and then returns the
        // anchor, so the weapon does not move. Safe to call unmatched; it does nothing if no
        // slide was running. `save` is accepted for symmetry with the equipped grip and is
        // currently ignored: an off-hand hold is transient by design and is never persisted.
        virtual void EndOffHandSlide(bool offIsLeft, bool save) = 0;
    };

    // ---- revision 9 ------------------------------------------------------------------------
    //
    // Either hand, and the driver names it. Revision 8 assumed PCVR could tell on its own when the
    // MAIN hand was sliding, by watching the slide command change; it cannot, because a saved grip
    // being re-sent on equip looks exactly the same, and the anchor moved with no trigger held.
    //
    // What a slide does, now that it is understood: while two-handing, HIGGS draws BOTH hands from
    // the weapon through their grab relations (hand.cpp:3671-3676), so a hand's place on the weapon
    // IS its relation. A slide therefore moves the sliding hand's relation to follow that hand's
    // real controller along the handle, while the other hand anchors the weapon -- the weapon does
    // not move, and only the one hand travels.
    //
    // Call Begin when the gesture engages on either hand and End when it releases. Inert unless
    // PCVR proved HIGGS's private layout on this process. Revision 8's off-hand pair still works
    // and now forwards here.
    struct IPcvrInterface009 : IPcvrInterface008 {
        // `handIsLeft` is the hand the player is sliding, off or main. False if nobody is
        // two-handing, the internals are disarmed, or the other hand is already sliding -- two
        // hands cannot both travel, because one of them has to hold the weapon still.
        virtual bool BeginTwoHandSlide(bool handIsLeft) = 0;

        // Released. PCVR pins both grips to the weapon exactly where it is drawn and returns the
        // anchor, so nothing moves. Safe to call unmatched.
        virtual void EndTwoHandSlide(bool handIsLeft) = 0;
    };

    // ---- revision 10 -----------------------------------------------------------------------
    //
    // Appended inside namespace PcvrAPI, below IPcvrInterface009. What a DAMAGE AUTHORITY needs
    // from the weapon PCVR simulates, and a way to tell PCVR's own momentum scaling to stand aside
    // while one is present. Realistic Kill VR is the first consumer.
    //
    // Same discipline as every revision before it: a NEW struct through NEW methods, nothing
    // reordered, nothing grown after the fact.
    struct SwingState {
        std::uint32_t version = 1;
        bool twinLive = false;
        float tipSpeed = 0.0f;       // u/s, the twin's blade tip -- what the [swing] log line reports
                                     // (a peak over PCVR's own short window, so a single physics
                                     // step's noise is not what a consumer reads)
        float lagCm = 0.0f;          // how far the twin trails its command
        float massKg = 0.0f;         // the twin body's mass -- the same figure GetWeaponMetrics gives
        float lengthM = 0.0f;        // longest dimension of the real collision shape
        float axisWorld[3] = { 0.0f, 0.0f, 0.0f };  // handle axis in world space, unit, tip-ward
                                                    // (GetGripInfo's axis); all zero when unknown
    };

    struct IPcvrInterface010 : IPcvrInterface009 {
        // Frame-thread only, like the grip calls. False when there is no live twin for that hand
        // (a sheathed weapon, an empty hand, a shape with no collision); `out` is untouched then.
        virtual bool GetSwingState(bool isLeft, SwingState& out) = 0;

        // A lease, not a lock -- the same shape as ClaimHand, capped at 600 frames and counted
        // down on the frame thread: while it is live, PCVR's momentum damage hook forwards every
        // blow untouched, whatever a weight driver asked for. Re-assert it every frame while your
        // mod owns the player's strike damage; a mod that crashes or forgets hands it back.
        virtual void ClaimDamageAuthority(std::uint32_t frames) = 0;
    };

    // ---- revision 11 -----------------------------------------------------------------------
    //
    // Appended inside namespace PcvrAPI, below IPcvrInterface010. OWNERSHIP: a driver may claim a
    // DOMAIN, and while it holds one PCVR uses the driver's value for that domain and its own ini
    // key for it is inert.
    //
    // Why this exists. PCVR and a weight driver had two places where both had an opinion and
    // PCVR's silently won -- bHandFollowsRotation vetoing half of a requested handFollowMode, and
    // an "is the other hand empty" test dropping the driver's two-hand multipliers whenever a
    // torch was lit. Neither was visible to the driver. Rather than add a flag per quarrel, the
    // driver now says which decisions are its own, once, and PCVR steps aside for those.
    //
    // An UNCLAIMED domain behaves exactly as it did before this revision. That is the whole
    // compatibility story: PCVR alone, and PCVR with a driver that predates revision 11, are
    // byte-for-byte unchanged.
    //
    // THREADING: frame-thread only, like the grip calls. The mask is read from the physics thread
    // through an atomic, so a claim lands on the next step and never mid-step.
    namespace OwnDomain {
        constexpr std::uint32_t kHandFollow = 1u << 0;    // handFollowMode, rotation half included
        constexpr std::uint32_t kTwoHandBonus = 1u << 1;  // the two-hand force/torque multipliers
        constexpr std::uint32_t kGripWindow = 1u << 2;    // the slide clamp window
        constexpr std::uint32_t kWeightDials = 1u << 3;   // PCVR's own [Weight] ini keys
    }

    struct IPcvrInterface011 : IPcvrInterface010 {
        // Claim one or more domains. Returns the mask ACTUALLY granted -- a build may refuse a
        // domain it has no way to yield, so check the return rather than assuming.
        virtual std::uint32_t ClaimOwnership(std::uint32_t domainMask) = 0;

        // Hand a domain back. PCVR's own key governs it again from the next frame.
        virtual void ReleaseOwnership(std::uint32_t domainMask) = 0;

        // ownedMask: what is claimed right now. availableMask: what this build can yield at all,
        // so a driver can report honestly instead of guessing what its claim achieved.
        virtual bool GetOwnershipState(std::uint32_t& ownedMask, std::uint32_t& availableMask) = 0;
    };

    inline IPcvrInterface001* g_pcvr = nullptr;
    inline IPcvrInterface002* g_pcvr2 = nullptr;
    inline IPcvrInterface003* g_pcvr3 = nullptr;
    inline IPcvrInterface004* g_pcvr4 = nullptr;
    inline IPcvrInterface005* g_pcvr5 = nullptr;
    inline IPcvrInterface006* g_pcvr6 = nullptr;
    inline IPcvrInterface007* g_pcvr7 = nullptr;
    inline IPcvrInterface008* g_pcvr8 = nullptr;
    inline IPcvrInterface009* g_pcvr9 = nullptr;
    inline IPcvrInterface010* g_pcvr10 = nullptr;
    inline IPcvrInterface011* g_pcvr11 = nullptr;

    // Read an event out of a broadcast message. Returns nullptr if this isn't one.
    inline const PcvrEvent* ReadEvent(SKSE::MessagingInterface::Message* msg) {
        if (!msg || msg->type != kMessage_Event || !msg->data || msg->dataLen < sizeof(PcvrEvent)) {
            return nullptr;
        }
        return static_cast<const PcvrEvent*>(msg->data);
    }

    // Consumers: call from a listener registered for ALL senders. This is the path to rely on.
    inline bool AcceptInterfaceOffer(SKSE::MessagingInterface::Message* msg) {
        if (!msg || msg->type != kMessage_Interface || !msg->data ||
            msg->dataLen < sizeof(PcvrApiOffer)) {
            return false;
        }
        auto* offer = static_cast<PcvrApiOffer*>(msg->data);
        if (!offer->GetApiFunction) {
            return false;
        }
        // Ask for the newest first and fall back. An older PCVR returns nullptr for revision 11
        // (or 10, 9, 8, 7, 6, 5, 4, 3, 2) and still hands over what it has, so a consumer built
        // against this header keeps working on it.
        g_pcvr11 = static_cast<IPcvrInterface011*>(offer->GetApiFunction(11));
        g_pcvr10 = g_pcvr11 ? static_cast<IPcvrInterface010*>(g_pcvr11)
                            : static_cast<IPcvrInterface010*>(offer->GetApiFunction(10));
        g_pcvr9 = g_pcvr10 ? static_cast<IPcvrInterface009*>(g_pcvr10)
                           : static_cast<IPcvrInterface009*>(offer->GetApiFunction(9));
        g_pcvr8 = g_pcvr9 ? static_cast<IPcvrInterface008*>(g_pcvr9)
                          : static_cast<IPcvrInterface008*>(offer->GetApiFunction(8));
        g_pcvr7 = g_pcvr8 ? static_cast<IPcvrInterface007*>(g_pcvr8)
                          : static_cast<IPcvrInterface007*>(offer->GetApiFunction(7));
        g_pcvr6 = g_pcvr7 ? static_cast<IPcvrInterface006*>(g_pcvr7)
                          : static_cast<IPcvrInterface006*>(offer->GetApiFunction(6));
        g_pcvr5 = g_pcvr6 ? static_cast<IPcvrInterface005*>(g_pcvr6)
                          : static_cast<IPcvrInterface005*>(offer->GetApiFunction(5));
        g_pcvr4 = g_pcvr5 ? static_cast<IPcvrInterface004*>(g_pcvr5)
                          : static_cast<IPcvrInterface004*>(offer->GetApiFunction(4));
        g_pcvr3 = g_pcvr4 ? static_cast<IPcvrInterface003*>(g_pcvr4)
                          : static_cast<IPcvrInterface003*>(offer->GetApiFunction(3));
        g_pcvr2 = g_pcvr3 ? static_cast<IPcvrInterface002*>(g_pcvr3)
                          : static_cast<IPcvrInterface002*>(offer->GetApiFunction(2));
        g_pcvr = g_pcvr2 ? static_cast<IPcvrInterface001*>(g_pcvr2)
                         : static_cast<IPcvrInterface001*>(offer->GetApiFunction(1));
        return g_pcvr != nullptr;
    }

    // Fallback only; treat a null return as "not yet", never as "not installed".
    inline IPcvrInterface001* RequestInterface() {
        if (g_pcvr) {
            return g_pcvr;
        }
        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            return nullptr;
        }
        PcvrApiOffer msg{};
        messaging->Dispatch(kMessage_GetInterface, &msg, sizeof(msg), "PhysicalCollisionVR");
        if (!msg.GetApiFunction) {
            return nullptr;
        }
        g_pcvr = static_cast<IPcvrInterface001*>(msg.GetApiFunction(1));
        return g_pcvr;
    }
}
