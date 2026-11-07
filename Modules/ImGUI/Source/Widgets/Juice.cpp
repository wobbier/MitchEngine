#include "Juice.h"

#include <imgui_internal.h>
#include "ImGuiTheme.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace MeWidgets::Juice
{
    namespace
    {
        constexpr uint32_t kSeed = 0x9E3779B9u;
        constexpr double kForgetAfter = 1.0;    // seconds an ID's state outlives its last use
        constexpr double kFlashTime = 0.45;
        constexpr double kShakeTime = 0.4;
        constexpr double kTickTime = 0.3;
        constexpr double kParticleTime = 0.55;

        struct SpringState
        {
            float Position = 0.f;
            float Velocity = 0.f;
            float Target = 0.f;
            uint64_t Frame = 0;
            double Touched = 0.0;
        };

        struct Event
        {
            double Start = -1.0;
        };

        struct Particle
        {
            ImVec2 Origin;
            ImVec2 Velocity;
            ImU32 Color = 0;
            double Start = 0.0;
            ImGuiID Viewport = 0;
            float Size = 2.f;
        };

        struct State
        {
            Level CurrentLevel = Level::Full;
            float FixedDelta = 0.f;
            bool ReduceMotion = false;
            double Time = 0.0;
            uint64_t Frame = 1;
            std::unordered_map<ImGuiID, SpringState> Springs;
            std::unordered_map<ImGuiID, Event> Flashes;
            std::unordered_map<ImGuiID, Event> Shakes;
            std::unordered_map<ImGuiID, Event> Ticks;
            std::vector<Particle> Particles;
            uint32_t Seed = kSeed;
            std::function<void( Cue )> Player;
            std::vector<ImGuiContext*> Hooked;
        };

        State& Get()
        {
            static State state;
            return state;
        }

        // Deterministic sparks: the same burst looks the same in every capture.
        float NextRandom()
        {
            uint32_t& seed = Get().Seed;
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>( ( seed >> 8 ) & 0xFFFF ) / 65535.f;
        }

        float FontSize()
        {
            return ImGui::GetCurrentContext() ? ImGui::GetFontSize() : 18.f;
        }

        void EnsureHook()
        {
            ImGuiContext* context = ImGui::GetCurrentContext();
            State& state = Get();
            if( !context || std::find( state.Hooked.begin(), state.Hooked.end(), context ) != state.Hooked.end() )
            {
                return;
            }
            ImGuiContextHook hook;
            hook.Type = ImGuiContextHookType_EndFramePre;
            hook.Callback = []( ImGuiContext* InContext, ImGuiContextHook* ) {
                Update( Get().FixedDelta > 0.f ? Get().FixedDelta : InContext->IO.DeltaTime );
                DrawParticles();
            };
            ImGui::AddContextHook( context, &hook );
            ImGuiContextHook shutdown;
            shutdown.Type = ImGuiContextHookType_Shutdown;
            shutdown.Callback = []( ImGuiContext* InDying, ImGuiContextHook* ) {
                auto& hooked = Get().Hooked;
                hooked.erase( std::remove( hooked.begin(), hooked.end(), InDying ), hooked.end() );
                Get().Particles.clear();
            };
            ImGui::AddContextHook( context, &shutdown );
            state.Hooked.push_back( context );
        }

        // 0..1 that falls off over InDuration after InEvent started, or 0.
        float Decay( const std::unordered_map<ImGuiID, Event>& InEvents, ImGuiID InId, double InDuration )
        {
            const auto found = InEvents.find( InId );
            if( found == InEvents.end() )
            {
                return 0.f;
            }
            const double age = Get().Time - found->second.Start;
            if( age < 0.0 || age >= InDuration )
            {
                return 0.f;
            }
            const float left = static_cast<float>( 1.0 - age / InDuration );
            return left * left;
        }
    }

    void SetLevel( Level InLevel )
    {
        if( Get().CurrentLevel != InLevel )
        {
            Get().CurrentLevel = InLevel;
            Reset();
        }
    }

    Level GetLevel()
    {
        return Get().CurrentLevel;
    }

    void SetReduceMotion( bool InReduce )
    {
        if( Get().ReduceMotion != InReduce )
        {
            Get().ReduceMotion = InReduce;
            Reset();
        }
    }

    bool GetReduceMotion()
    {
        return Get().ReduceMotion;
    }

    float Strength()
    {
        switch( Get().CurrentLevel )
        {
        case Level::Off: return 0.f;
        case Level::Subtle: return 0.5f;
        default: return 1.f;
        }
    }

    float Motion()
    {
        return Get().ReduceMotion ? 0.f : Strength();
    }

    float Spring( ImGuiID InId, float InTarget, float InFrequency )
    {
        EnsureHook();
        State& state = Get();
        if( Motion() <= 0.f || InId == 0 )
        {
            state.Springs.erase( InId );
            return InTarget;
        }
        auto [entry, created] = state.Springs.try_emplace( InId );
        SpringState& spring = entry->second;
        if( created )
        {
            spring.Position = InTarget;
            spring.Frame = state.Frame;
            spring.Touched = state.Time;
        }
        spring.Target = InTarget;
        if( spring.Frame != state.Frame )
        {
            // Step from the last frame this spring was seen: semi-implicit Euler in small steps.
            double dt = std::clamp( state.Time - spring.Touched, 0.0, 1.0 / 15.0 );
            const float omega = 6.2831853f * InFrequency;
            // Full overshoots a little; Subtle settles without overshoot.
            const float zeta = state.CurrentLevel == Level::Full ? 0.55f : 1.f;
            while( dt > 0.0 )
            {
                const float step = static_cast<float>( std::min( dt, 1.0 / 240.0 ) );
                const float accel = -omega * omega * ( spring.Position - spring.Target ) - 2.f * zeta * omega * spring.Velocity;
                spring.Velocity += accel * step;
                spring.Position += spring.Velocity * step;
                dt -= step;
            }
            const float scale = std::max( std::abs( spring.Target ), 1.f );
            if( std::abs( spring.Position - spring.Target ) < 1e-4f * scale && std::abs( spring.Velocity ) < 1e-3f * scale )
            {
                spring.Position = spring.Target;
                spring.Velocity = 0.f;
            }
            spring.Frame = state.Frame;
        }
        spring.Touched = state.Time;
        return spring.Position;
    }

    void Snap( ImGuiID InId, float InTarget )
    {
        auto found = Get().Springs.find( InId );
        if( found != Get().Springs.end() )
        {
            found->second.Position = InTarget;
            found->second.Target = InTarget;
            found->second.Velocity = 0.f;
            found->second.Touched = Get().Time;
            found->second.Frame = Get().Frame;
        }
    }

    bool IsMoving( ImGuiID InId )
    {
        const auto found = Get().Springs.find( InId );
        return found != Get().Springs.end() && ( found->second.Position != found->second.Target || found->second.Velocity != 0.f );
    }

    void Flash( ImGuiID InId )
    {
        EnsureHook();
        if( Strength() > 0.f )
        {
            Get().Flashes[InId].Start = Get().Time;
        }
    }

    float FlashAmount( ImGuiID InId )
    {
        return Strength() * Decay( Get().Flashes, InId, kFlashTime );
    }

    void Shake( ImGuiID InId )
    {
        EnsureHook();
        if( Motion() > 0.f )
        {
            Get().Shakes[InId].Start = Get().Time;
        }
        Play( Cue::Refuse );
    }

    float ShakeOffset( ImGuiID InId )
    {
        const float left = Decay( Get().Shakes, InId, kShakeTime );
        if( left <= 0.f || Motion() <= 0.f )
        {
            return 0.f;
        }
        const double age = Get().Time - Get().Shakes[InId].Start;
        return Motion() * FontSize() * 0.35f * left * static_cast<float>( std::sin( age * 55.0 ) );
    }

    void Tick( ImGuiID InId, ImVec2 InPos )
    {
        EnsureHook();
        if( Strength() > 0.f )
        {
            Get().Ticks[InId].Start = Get().Time;
            Burst( InPos, ImGui::GetCurrentContext() ? ImGui::GetColorU32( ImGuiTheme::AccentText() ) : IM_COL32_WHITE, 6 );
        }
        Play( Cue::Tick );
    }

    float TickAmount( ImGuiID InId )
    {
        return Strength() * Decay( Get().Ticks, InId, kTickTime );
    }

    void Burst( ImVec2 InPos, ImU32 InColor, int InCount )
    {
        EnsureHook();
        State& state = Get();
        const float motion = Motion();
        if( motion <= 0.f )
        {
            return;
        }
        const int count = std::max( 1, static_cast<int>( std::round( InCount * motion ) ) );
        const float speed = FontSize() * 7.f;
        ImGuiContext* context = ImGui::GetCurrentContext();
        const ImGuiID viewport = context && context->CurrentWindow && context->CurrentWindow->Viewport ? context->CurrentWindow->Viewport->ID : 0;
        for( int i = 0; i < count; ++i )
        {
            // Mostly upwards, fanned out.
            const float angle = -1.5707963f + ( NextRandom() - 0.5f ) * 2.6f;
            const float magnitude = speed * ( 0.5f + NextRandom() * 0.6f );
            Particle particle;
            particle.Origin = InPos;
            particle.Velocity = ImVec2( std::cos( angle ) * magnitude, std::sin( angle ) * magnitude );
            particle.Color = InColor;
            particle.Start = state.Time;
            particle.Viewport = viewport;
            particle.Size = std::max( 1.5f, FontSize() * ( 0.08f + NextRandom() * 0.08f ) );
            state.Particles.push_back( particle );
        }
        // A cap, so a held scrub over many detents can't flood the draw list.
        if( state.Particles.size() > 256 )
        {
            state.Particles.erase( state.Particles.begin(), state.Particles.begin() + ( state.Particles.size() - 256 ) );
        }
    }

    int LiveParticles()
    {
        return static_cast<int>( Get().Particles.size() );
    }

    void SetSoundPlayer( std::function<void( Cue )> InPlayer )
    {
        Get().Player = std::move( InPlayer );
    }

    void Play( Cue InCue )
    {
        if( Get().Player && Strength() > 0.f )
        {
            Get().Player( InCue );
        }
    }

    void Update( float InDeltaTime )
    {
        State& state = Get();
        state.Time += std::max( 0.f, InDeltaTime );
        ++state.Frame;
        auto forget = [&]( auto& InMap, double InLife ) {
            for( auto it = InMap.begin(); it != InMap.end(); )
            {
                it = state.Time - it->second.Start > InLife ? InMap.erase( it ) : std::next( it );
            }
        };
        forget( state.Flashes, kFlashTime );
        forget( state.Shakes, kShakeTime );
        forget( state.Ticks, kTickTime );
        for( auto it = state.Springs.begin(); it != state.Springs.end(); )
        {
            it = state.Time - it->second.Touched > kForgetAfter ? state.Springs.erase( it ) : std::next( it );
        }
        state.Particles.erase( std::remove_if( state.Particles.begin(), state.Particles.end(),
            [&]( const Particle& InParticle ) { return state.Time - InParticle.Start >= kParticleTime; } ), state.Particles.end() );
    }

    void DrawParticles()
    {
        State& state = Get();
        ImGuiContext* context = ImGui::GetCurrentContext();
        if( !context || state.Particles.empty() )
        {
            return;
        }
        const float gravity = FontSize() * 30.f;
        for( const Particle& particle : state.Particles )
        {
            ImGuiViewport* viewport = particle.Viewport ? ImGui::FindViewportByID( particle.Viewport ) : ImGui::GetMainViewport();
            if( !viewport )
            {
                continue;
            }
            const float age = static_cast<float>( state.Time - particle.Start );
            const float life = 1.f - age / static_cast<float>( kParticleTime );
            const ImVec2 pos( particle.Origin.x + particle.Velocity.x * age, particle.Origin.y + particle.Velocity.y * age + 0.5f * gravity * age * age );
            ImVec4 color = ImGui::ColorConvertU32ToFloat4( particle.Color );
            color.w *= life;
            ImGui::GetForegroundDrawList( viewport )->AddCircleFilled( pos, particle.Size * ( 0.6f + 0.4f * life ), ImGui::GetColorU32( color ), 8 );
        }
    }

    void SetFixedDelta( float InSeconds )
    {
        Get().FixedDelta = std::max( 0.f, InSeconds );
    }

    void Reset()
    {
        State& state = Get();
        state.Springs.clear();
        state.Flashes.clear();
        state.Shakes.clear();
        state.Ticks.clear();
        state.Particles.clear();
        // Bursts after a reset repeat exactly (screenshot regression).
        state.Seed = kSeed;
    }
}
