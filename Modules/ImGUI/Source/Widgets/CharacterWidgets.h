#pragma once
#include "ScrubField.h"

// Character and navigation widgets (UX-10), built on Graph like the light, camera, post, physics
// and audio widgets: each is a whole inspector row (a caption in the label column, the widget in
// the value column), returns true when it changed a value and is made of real ImGui items, so the
// inspector turns a gesture into one undo step and automation finds the parts by caption:
// "CharacterController/Capsule/Height", "CharacterController/Walks Up/Slope",
// "CharacterController/Jump/Apex", "CharacterController/Pace/Run".
namespace MeWidgets
{
    // The movement the widgets show, as the physics cores move a CharacterController(2D)
    // (Cores/PhysicsCore.cpp StepCharacter, Physics2DCore.cpp): a capsule Height end to end with
    // its caps (the segment is max(Height / 2 - Radius, 0) each way, so a Height under 2 Radius is
    // a ball), a jump that leaves the ground at sqrt(2 |g| JumpHeight) with g = gravity.y x
    // GravityScale (signed: the cores keep adding it, so an upward g never lands), and surfaces steeper than SlopeLimit counting as walls. Pure: no ImGui, unit
    // tested.
    namespace CharacterMath
    {
        // ---- Capsule ------------------------------------------------------------------------
        // A person to stand the capsule next to.
        constexpr double kHumanHeight = 1.8;
        // The height a capsule of InRadius can have: at least its two caps (2 Radius).
        double CapsuleHeight( double InHeight, double InRadius );
        // A new radius for a capsule of InOutHeight: the height grows to keep both caps.
        void SetCapsuleRadius( double InRadius, double& InOutRadius, double& InOutHeight );
        // The straight part between the caps: max(Height - 2 Radius, 0).
        double CapsuleCylinder( double InHeight, double InRadius );
        // The silhouette's vertical span: the first of 2.5, 3, 4, 5, 6, 8, 10, 15, 20, 25 m
        // that shows the taller of the capsule and the person with an eighth to spare.
        double SilhouetteSpan( double InHeight );

        // ---- Jump ---------------------------------------------------------------------------
        // Gravity on the character, signed as the cores apply it: gravity.y x GravityScale (m/s^2,
        // negative pulls down). The widgets below take the downward pull, its negation.
        double JumpGravity( double InGravityY, double InGravityScale );
        // Takeoff speed for a jump of InHeight: sqrt(2 g h).
        double TakeoffSpeed( double InGravity, double InHeight );
        // Time from takeoff to landing on the same level: 2 v / g. Infinity without gravity
        // (and 0 for no jump).
        double AirTime( double InGravity, double InHeight );
        // How far a jump at InSpeed (horizontal, m/s) carries: speed x air time (0 without
        // gravity, where the jump never leaves the ground).
        double JumpDistance( double InSpeed, double InGravity, double InHeight );
        // The jump's height after InDistance along the ground at InSpeed: h - g t'^2 / 2 with t'
        // the time from the apex. 0 outside the jump.
        double JumpHeightAt( double InDistance, double InSpeed, double InGravity, double InHeight );
        struct HeightMark
        {
            const char* Name;
            double Meters;
            const char* Note;
        };
        // Curb 0.15 m, Table 0.75 m, Human 1.8 m.
        const HeightMark* JumpMarks( int& OutCount );

        // ---- Slope --------------------------------------------------------------------------
        // A slope's grade: rise over run (tan), 1 = 100% at 45 degrees.
        double SlopeGrade( double InDegrees );
        struct SlopeMark
        {
            const char* Name;
            double Degrees;
            const char* Note;
        };
        // Ramp 4.8 (1:12, an accessible ramp), Stairs 35, Steep 60 degrees.
        const SlopeMark* SlopeMarks( int& OutCount );

        // ---- Pace ---------------------------------------------------------------------------
        struct PaceRung
        {
            const char* Name;
            double MetersPerSecond;
            const char* Note;
        };
        // Walk 1.4, Jog 3, Run 5, Sprint 7.5 m/s.
        const PaceRung* PaceRungs( int& OutCount );
        // The pace strip spans 0 .. this; faster values pin to the right end.
        constexpr double kPaceAxis = 10.0;
        // The rung InSpeed sits on (within a millimetre a second), or null.
        const PaceRung* PaceRungAt( double InSpeed );
        // A speed picked by pointing: tenths of a metre a second (InFine: hundredths).
        double RoundPace( double InSpeed, bool InFine );
    }

    // The character's capsule (or, with InCylinder, a navmesh agent's cylinder) from the side,
    // to scale next to a 1.8 m person, on the ground with metre ticks up the left. Drag its
    // "Height" handle (the top) and "Radius" handle (the side) in centimetres (Shift for
    // millimetres); a capsule keeps Height >= 2 Radius (the radius pushes the height up, the
    // height stops at the caps). The scale holds still under a dragged handle, then eases over.
    // Reports "1.8 m x 0.4 m" (height x radius).
    bool CapsuleSilhouette( const char* InCaption, double& InOutHeight, double& InOutRadius, const NumberSpec& InHeightSpec, const NumberSpec& InRadiusSpec, bool InCylinder );

    // The steepest slope the character walks up: a ramp wedge at InOutDegrees with a small
    // figure walking up it, the angle's arc and marks at an accessible ramp, stairs and a steep
    // slope. Drag its "Slope" handle (whole degrees, Shift for tenths). Reports "walks up 45°".
    bool SlopeWedge( const char* InCaption, double& InOutDegrees, const NumberSpec& InSpec );

    // The jump's arc running at InSpeed (m/s) under InGravity (m/s^2, already scaled, the
    // downward pull: 0 or less never lands, and draws no arc but says why): apex
    // height, takeoff speed sqrt(2 g h), air time 2 v / g and how far it lands, with marks at a
    // curb, a table and a person. Drag its "Apex" handle up and down to set the height in
    // centimetres (Shift for millimetres), snapping to the marks. Reports
    // "1.2 m apex, 4.85 m/s takeoff, 0.99 s in the air".
    bool JumpArc( const char* InCaption, double& InOutHeight, const NumberSpec& InSpec, double InSpeed, double InGravity );

    // Speed on a strip of paces (0 .. 10 m/s): Walk, Jog, Run, Sprint. Click a rung, drag along
    // the strip (or from a rung, or the value's marker) in tenths of a metre a second (Shift for
    // hundredths), snapping to the rungs and ticking as it passes them. Double-click the value's
    // marker ("Value") for InDefault. Reports "5 m/s, run".
    bool PaceStrip( const char* InCaption, double& InOutSpeed, const NumberSpec& InSpec, double InDefault );
}
