#pragma once
#include "ScrubField.h"

// Physics feel widgets (UX-8), built on Graph like the light, camera and post widgets: each is a
// whole inspector row (a caption in the label column, the widget in the value column), returns
// true when it changed a value and is made of real ImGui items, so the inspector turns a gesture
// into one undo step and automation finds the parts by caption: "Rigidbody/Weighs Like/Cat",
// "BoxCollider/Slide Angle/Angle", "Rigidbody/Linear Decay/Half-life", "Rigidbody/Lock Position/X".
namespace MeWidgets
{
    // The physics the widgets show, from Box3D / Box2D (ThirdParty/box3d/src/solver.c,
    // physics_world.c; Box2D is the same). Pure: no ImGui, unit tested.
    namespace PhysicsMath
    {
        // ---- Mass ladder: everyday things by weight, on a log strip ------------------------
        struct MassRung
        {
            const char* Name;
            double Kilograms;
            const char* Note;
        };
        // Feather 0.01 kg, Apple 0.1, Cat 4, Human 70, Motorbike 200, Car 1500, Truck 15000.
        const MassRung* MassRungs( int& OutCount );
        // The strip spans this (log): a little room past the lightest and heaviest rungs.
        constexpr double kLadderMinKg = 0.005;
        constexpr double kLadderMaxKg = 30000.0;
        // Kilograms <-> 0..1 along the strip (log10), clamped to the strip.
        double MassToLadder( double InKilograms );
        double MassFromLadder( double InT );
        // A mass picked by pointing: two significant digits (0.37 kg, 4.2 kg, 1500 kg).
        double RoundMass( double InKilograms );

        // ---- Restitution --------------------------------------------------------------------
        // A ball dropped with restitution e leaves each bounce at e times the speed it hit with,
        // so it rises to e^2 of the height it fell from: h_n = h0 e^(2n).
        double BounceRatio( double InRestitution );
        double BounceHeight( double InDropHeight, double InRestitution, int InBounce );
        // The ball's height at InTime after being dropped from height 1, falling to the floor in
        // InFallTime (constant gravity). Bounces under InRest of the drop height count as resting.
        double BallHeight( double InTime, double InRestitution, double InFallTime, double InRest = 0.002 );
        // When bounce InBounce peaks (0: the drop, at time 0), for the same drop.
        double BounceApexTime( int InBounce, double InRestitution, double InFallTime );

        // ---- Friction -----------------------------------------------------------------------
        // A block on a ramp of the same material slides once tan(angle) exceeds the friction
        // coefficient: the slide angle is atan(mu). Box3D mixes two shapes' friction as
        // sqrt(a b), so on a ramp of the same friction the coefficient is mu itself.
        double SlideAngleDegrees( double InFriction );
        double FrictionForAngle( double InDegrees );

        // ---- Damping ------------------------------------------------------------------------
        // Box3D / Box2D damp once per substep h (solver.c b3IntegrateVelocities):
        // v *= 1 / (1 + h d), the Pade approximation of exp(-d h). Over time t that compounds to
        // v(t) = v0 (1 + h d)^(-t / h), which these evaluate continuously (h <= 0: exp(-d t)).
        double DampingStepFactor( double InDamping, double InSubstep );
        double DampedSpeed( double InDamping, double InTime, double InSubstep );
        // The time to lose half the speed: h ln 2 / ln(1 + h d). Infinity without damping.
        double DampingHalfLife( double InDamping, double InSubstep );
        // The damping that halves the speed in InHalfLife: (2^(h / t) - 1) / h. 0 for infinity.
        double DampingForHalfLife( double InHalfLife, double InSubstep );
        // The damping graph's time axis: the first of 3, 5, 10, 20, 30 s that shows InHalfLife
        // with a quarter to spare; 30 s (kDampingMaxAxis) when it is longer or infinite, and the
        // half-life handle then sits pinned at the right edge.
        constexpr double kDampingMaxAxis = 30.0;
        double DampingAxisSeconds( double InHalfLife );
    }

    // Mass on a log strip of everyday things (Feather .. Truck). Click a rung, drag along the
    // strip (or from a rung, or the value's marker) in log space, two significant digits,
    // snapping to the rungs and ticking as it passes them. Double-click the value's marker
    // ("Value") for InDefaultKg. With InAllowAuto, an "Auto" chip before the strip sets 0: the
    // mass then comes from the colliders' densities.
    bool MassLadder( const char* InCaption, double& InOutKilograms, const NumberSpec& InSpec, double InDefaultKg, bool InAllowAuto );

    // A ball dropped on the floor bouncing with InRestitution: it animates through Juice at
    // Subtle and Full and is a still picture of each bounce's apex (h_n = h0 e^(2n)) at Off or
    // with reduce motion. Reports the bounce height ratio ("bounces to 36%"). Shows, never edits.
    void BouncePreview( const char* InCaption, double InRestitution );

    // A ramp at the slide angle atan(friction): "slides above 31°". Drag its "Angle" handle (whole
    // degrees, Shift for tenths) to set friction = tan(angle).
    bool SlideAngleRamp( const char* InCaption, double& InOutFriction, const NumberSpec& InSpec );

    // Speed (or spin) left against time under InOutDamping with the engine's per-substep
    // damping (InSubstep seconds); exp(-d t) faint behind it. The time axis runs 3 s, widening
    // (5, 10, 20, 30 s) to show the half-life. Drag the "Half-life" handle along the half-speed
    // line to set the damping that halves the speed in that time; past 30 s (or with no
    // damping) it sits pinned at the right edge with an arrow and reports the true half-life.
    bool DampingGraph( const char* InCaption, double& InOutDamping, const NumberSpec& InSpec, double InSubstep, const char* InWhat );

    // Axis lock toggles: a chip per axis (InNames, coloured by InAxes: 0 X, 1 Y, 2 Z) showing a
    // closed padlock when locked. Each chip is an item named by its axis ("Lock Position/X").
    bool AxisLocks( const char* InCaption, bool* InOutLocks, const char* const* InNames, const int* InAxes, int InCount, bool InRotation );
}
