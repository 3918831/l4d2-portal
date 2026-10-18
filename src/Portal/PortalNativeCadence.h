#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace PortalNativeCadence
{
    struct Vec3 { float x = 0, y = 0, z = 0; };
    struct RowBudget
    {
        int used = 0, dropped = 0;
        bool Take()
        {
            if (used >= 160) { ++dropped; return false; }
            ++used;
            return true;
        }
    };
    struct Sample
    {
        int frame = -1, tick = -1;
        float realTime = 0, frameTime = 0, speed = 0;
        Vec3 originalEye, finalEye;
        bool originalValid = false, onGround = false, rollActive = false;
        bool eyeStateValid = false, eyeHistory = false, eyeReleased = false, shadow = false;
        unsigned nativeClass = 0;
        unsigned mapClass = 0;
    };
    struct Summary
    {
        int frames = 0, emitted = 0, suppressed = 0, originalComparableFrames = 0;
        float totalFinalFreezeMs = 0, maxFinalFreezeMs = 0;
        float totalOriginalFreezeMs = 0, maxOriginalFreezeMs = 0;
        float maxFinalStep = 0, maxOriginalStep = 0;
    };
    struct Result
    {
        bool accepted = false, emit = false, completed = false;
        bool finalFrozen = false, originalFrozen = false, originalComparable = false;
        float finalDelta = 0, originalDelta = 0;
    };
    class Tracker
    {
    public:
        void Reset() { *this = {}; }
        void ObserveCommand(int command, int frame)
        {
            if (command > m_command) { m_command = command; m_commandFrame = frame; }
        }
        int LastCommand() const { return m_command; }
        int LastCommandFrame() const { return m_commandFrame; }
        void Begin(std::uint32_t, int, int tick, float realTime)
        {
            m_summary = {}; m_previous = {}; m_hasPrevious = false;
            m_finalFreezeMs = m_originalFreezeMs = 0;
            m_previousFinalFrozen = m_previousOriginalFrozen = false;
            m_startTick = tick; m_startTime = realTime; m_active = true;
        }
        Result Record(const Sample& s)
        {
            Result r;
            if (!m_active || s.frame < 0 || s.tick < 0
                || (m_hasPrevious && s.frame <= m_previous.frame)
                || !std::isfinite(s.frameTime) || s.frameTime < 0
                || !std::isfinite(s.realTime) || !std::isfinite(s.speed)
                || !Finite(s.finalEye) || (s.originalValid && !Finite(s.originalEye)))
                return r;
            r.accepted = true;
            r.emit = !m_hasPrevious;
            if (m_hasPrevious)
            {
                r.finalDelta = Distance(s.finalEye, m_previous.finalEye);
                r.originalComparable = s.originalValid && m_previous.originalValid;
                if (r.originalComparable)
                {
                    r.originalDelta = Distance(s.originalEye, m_previous.originalEye);
                    ++m_summary.originalComparableFrames;
                }
                const bool moving = s.speed * s.frameTime > .001f;
                r.finalFrozen = moving && r.finalDelta <= .001f;
                r.originalFrozen = moving && r.originalComparable && r.originalDelta <= .001f;
                const float ms = s.frameTime * 1000.0f;
                m_finalFreezeMs = r.finalFrozen ? m_finalFreezeMs + ms : 0;
                m_originalFreezeMs = r.originalFrozen ? m_originalFreezeMs + ms : 0;
                if (r.finalFrozen) m_summary.totalFinalFreezeMs += ms;
                if (r.originalFrozen) m_summary.totalOriginalFreezeMs += ms;
                m_summary.maxFinalFreezeMs = std::max(m_summary.maxFinalFreezeMs, m_finalFreezeMs);
                m_summary.maxOriginalFreezeMs = std::max(m_summary.maxOriginalFreezeMs, m_originalFreezeMs);
                m_summary.maxFinalStep = std::max(m_summary.maxFinalStep, r.finalDelta);
                m_summary.maxOriginalStep = std::max(m_summary.maxOriginalStep, r.originalDelta);
                r.emit = s.tick != m_previous.tick
                    || s.nativeClass != m_previous.nativeClass || s.mapClass != m_previous.mapClass
                    || r.finalFrozen != m_previousFinalFrozen
                    || r.originalFrozen != m_previousOriginalFrozen
                    || s.originalValid != m_previous.originalValid
                    || s.onGround != m_previous.onGround
                    || s.rollActive != m_previous.rollActive
                    || s.eyeStateValid != m_previous.eyeStateValid
                    || (s.eyeStateValid && (s.eyeReleased
                        || s.eyeHistory != m_previous.eyeHistory
                        || s.shadow != m_previous.shadow));
            }
            ++m_summary.frames;
            r.completed = s.tick - m_startTick >= 40 || s.tick < m_startTick
                || s.realTime - m_startTime >= 5.0f || s.realTime < m_startTime
                || m_summary.frames >= 2048;
            r.emit = r.emit || r.completed;
            if (r.emit && !r.completed && m_summary.emitted >= 128)
            {
                r.emit = false;
                ++m_summary.suppressed;
            }
            if (r.emit) ++m_summary.emitted;
            m_previous = s; m_hasPrevious = true;
            m_previousFinalFrozen = r.finalFrozen;
            m_previousOriginalFrozen = r.originalFrozen;
            if (r.completed) m_active = false;
            return r;
        }
        bool Active() const { return m_active; }
        const Summary& GetSummary() const { return m_summary; }
    private:
        Summary m_summary;
        static bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
        static float Distance(const Vec3& a, const Vec3& b)
        {
            const float x = a.x-b.x, y = a.y-b.y, z = a.z-b.z;
            return std::sqrt(x*x+y*y+z*z);
        }
        Sample m_previous;
        int m_command = 0, m_commandFrame = -1, m_startTick = -1;
        float m_startTime = 0, m_finalFreezeMs = 0, m_originalFreezeMs = 0;
        bool m_active = false, m_hasPrevious = false;
        bool m_previousFinalFrozen = false, m_previousOriginalFrozen = false;
    };
}
