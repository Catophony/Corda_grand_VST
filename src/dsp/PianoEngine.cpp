#include "dsp/PianoEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace corda
{

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kLn1000 = 6.907755278982137f;
constexpr int kMaxModes = 56;
constexpr int kNumBanks = 3;
constexpr int kMaxVoices = PianoEngine::kMaxVoices;
constexpr int kSympatheticModes = 36;
constexpr int kSympatheticNotes = 12;

inline float clampf(float x, float lo, float hi) noexcept
{
    return x < lo ? lo : (x > hi ? hi : x);
}

inline float softClip(float x) noexcept
{
    constexpr float knee = 0.96f;
    const float ax = std::abs(x);
    if (ax <= knee)
        return x;
    const float over = (ax - knee) / (1.0f - knee);
    const float y = knee + (1.0f - knee) * std::tanh(over);
    return std::copysign(y, x);
}

inline float midiToFreq(float note, float a4) noexcept
{
    return a4 * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

// Railsback stretch: the bass is tuned flat and the treble sharp. This is one
// of the reasons a real piano sounds in tune with itself but not with a
// perfectly equal-tempered oscillator bank.
inline float stretchCents(int note) noexcept
{
    const float d = (float) note - 60.0f;
    const float s = d < 0.0f ? -1.0f : 1.0f;
    return s * 0.085f * std::pow(std::abs(d), 1.55f);
}

struct Rng
{
    uint32_t s = 0x12345678u;
    void seed(uint32_t v) noexcept { s = v == 0 ? 0x9e3779b9u : v; }
    float next() noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float) (s & 0x00ffffffu) * (2.0f / 16777216.0f) - 1.0f;
    }
};

struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process(float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setHighPass(float sr, float f, float q) noexcept
    {
        const float w = kTwoPi * f / sr;
        const float c = std::cos(w), s = std::sin(w);
        const float alpha = s / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0 = (1.0f + c) * 0.5f / a0;
        b1 = -(1.0f + c) / a0;
        b2 = b0;
        a1 = -2.0f * c / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setLowPass(float sr, float f, float q) noexcept
    {
        const float w = kTwoPi * f / sr;
        const float c = std::cos(w), s = std::sin(w);
        const float alpha = s / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0 = (1.0f - c) * 0.5f / a0;
        b1 = (1.0f - c) / a0;
        b2 = b0;
        a1 = -2.0f * c / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setPeak(float sr, float f, float q, float gainDb) noexcept
    {
        const float A = std::pow(10.0f, gainDb / 40.0f);
        const float w = kTwoPi * f / sr;
        const float c = std::cos(w), s = std::sin(w);
        const float alpha = s / (2.0f * q);
        const float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = -2.0f * c / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = -2.0f * c / a0;
        a2 = (1.0f - alpha / A) / a0;
    }
};

struct Mode
{
    float x1 = 0.0f, x2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float b0 = 0.0f;
    float phi = 0.0f;

    void reset() noexcept { x1 = x2 = 0.0f; }
};

struct StringBank
{
    int numModes = 0;
    float detune = 0.0f;
    float panL = 0.7071f, panR = 0.7071f;
    float weight = 1.0f;
    Mode modes[kMaxModes];

    void reset()
    {
        for (int i = 0; i < numModes; ++i)
            modes[i].reset();
    }

    void configure(float sr, float frequency, float detuneCents, float baseT60,
                   float strikePosition, int modeCount, float bankWeight,
                   float highRoll, float fs, uint32_t seed, bool preserveState,
                   float brightnessTilt)
    {
        numModes = modeCount;
        detune = detuneCents;
        weight = bankWeight;
        const float ratio = std::pow(2.0f, detuneCents / 1200.0f);
        const float f0 = frequency * ratio;
        const float b = inharmonicity;

        for (int n = 1; n <= numModes; ++n)
        {
            const float nn = (float) n;
            const float freq = nn * f0 * std::sqrt(1.0f + b * nn * nn);
            if (freq >= fs * 0.475f)
            {
                numModes = n - 1;
                break;
            }

            const float ratioToF0 = freq / f0;
            const float t60 = baseT60 / std::pow(nn, 0.63f);
            const float sigma = kLn1000 / std::max(0.05f, t60);
            const float r = std::exp(-sigma / sr);
            const float w = kTwoPi * freq / sr;

            const float phi = std::sin(kPi * nn * strikePosition);
            const float roll = 1.0f / (1.0f + std::pow(freq / highRoll, 1.6f));
            const float ampExp = 1.08f - 0.20f * std::clamp((f0 - 27.5f) / 4158.0f, 0.0f, 1.0f);
            const float amp = (0.22f + 0.78f * std::abs(phi))
                            / std::pow(ratioToF0, ampExp + brightnessTilt)
                            * roll * (phi < 0.0f ? -1.0f : 1.0f);

            auto& m = modes[n - 1];
            if (!preserveState)
                m.x1 = m.x2 = 0.0f;
            m.a1 = 2.0f * r * std::cos(w);
            m.a2 = -r * r;
            m.b0 = amp * std::sin(w);
            m.phi = phi;
        }
        (void) seed;
    }

    void setInharmonicity(float b) noexcept { inharmonicity = b; }

    float process(float force) noexcept
    {
        float sum = 0.0f;
        for (int i = 0; i < numModes; ++i)
        {
            auto& m = modes[i];
            const float x = m.a1 * m.x1 + m.a2 * m.x2 + m.b0 * force;
            m.x2 = m.x1;
            m.x1 = x;
            sum += x;
        }
        return sum * weight;
    }

    float processAndDisplacement(float force, float& displacement) noexcept
    {
        float sum = 0.0f;
        float d = 0.0f;
        for (int i = 0; i < numModes; ++i)
        {
            auto& m = modes[i];
            const float x = m.a1 * m.x1 + m.a2 * m.x2 + m.b0 * force;
            m.x2 = m.x1;
            m.x1 = x;
            sum += x;
            if (i < 18)
                d += x * m.phi;
        }
        displacement = d;
        return sum * weight;
    }

    float inharmonicity = 0.0002f;
};

struct Voice
{
    bool active = false;
    bool keyDown = false;
    int note = -1;
    int stringCount = 1;
    float velocity = 0.0f;
    float damperGain = 1.0f;
    float damperTau = 0.3f;
    float panL = 0.7071f, panR = 0.7071f;
    float outputGain = 1.0f;
    int age = 0;

    float contactTimeTarget = 0.003f;

    float noiseEnv = 0.0f;
    float noiseDecay = 0.0f;
    float noiseFilter = 0.0f;
    float noiseHp = 0.0f;
    float noiseHigh = 0.0f;
    float thumpEnv = 0.0f;
    float thumpPhase = 0.0f;
    float thumpInc = 0.0f;

    float strikeImpulse = 0.0f;
    bool struck = false;
    Rng rng;

    StringBank banks[kNumBanks];

    void resetStringState()
    {
        for (auto& b : banks)
            b.reset();
    }
};

struct SympatheticBank
{
    Mode modes[kSympatheticModes];
    int count = 0;
    float sampleRate = 48000.0f;

    void configure(float sr, float a4)
    {
        sampleRate = sr;
        count = 0;
        // A2..E4: the register where pedal-down sympathetic resonance is most
        // useful. Keeping this bank out of the sub-bass avoids a muddy drone.
        const int roots[kSympatheticNotes] = { 45, 46, 48, 50, 52, 53, 55, 57, 59, 60, 62, 64 };
        for (int h = 1; h <= 3; ++h)
        {
            for (int r = 0; r < kSympatheticNotes; ++r)
            {
                if (count >= kSympatheticModes)
                    break;
                const float f0 = midiToFreq((float) roots[r], a4) * std::pow(2.0f, stretchCents(roots[r]) / 1200.0f);
                const float freq = f0 * h * std::sqrt(1.0f + 0.00018f * h * h);
                if (freq >= sr * 0.45f)
                    continue;
                const float t60 = 8.0f + 4.0f / (float) h;
                const float sigma = kLn1000 / t60;
                const float rr = std::exp(-sigma / sr);
                const float w = kTwoPi * freq / sr;
                auto& m = modes[count++];
                m.x1 = m.x2 = 0.0f;
                m.a1 = 2.0f * rr * std::cos(w);
                m.a2 = -rr * rr;
                m.b0 = std::sin(w) / std::pow((float) h, 1.4f) * 0.35f;
                m.phi = 0.0f;
            }
        }
    }

    float process(float input) noexcept
    {
        float sum = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            auto& m = modes[i];
            const float x = m.a1 * m.x1 + m.a2 * m.x2 + m.b0 * input;
            m.x2 = m.x1;
            m.x1 = x;
            sum += x;
        }
        return sum;
    }
};

struct LookaheadLimiter
{
    static constexpr int kLookahead = 128;
    float bufferL[kLookahead] {};
    float bufferR[kLookahead] {};
    int index = 0;
    float gain = 1.0f;
    float attack = 0.0f;
    float release = 0.0f;
    float threshold = 0.80f;

    void prepare(float sr) noexcept
    {
        attack = std::exp(-1.0f / (sr * 0.0005f));
        release = std::exp(-1.0f / (sr * 0.120f));
        reset();
    }

    void reset() noexcept
    {
        std::fill(std::begin(bufferL), std::end(bufferL), 0.0f);
        std::fill(std::begin(bufferR), std::end(bufferR), 0.0f);
        index = 0;
        gain = 1.0f;
    }

    void process(float& inL, float& inR) noexcept
    {
        bufferL[index] = inL;
        bufferR[index] = inR;
        const int outIndex = (index + 1) % kLookahead;
        float outL = bufferL[outIndex];
        float outR = bufferR[outIndex];

        const float peak = std::max(std::abs(inL), std::abs(inR));
        const float target = peak > threshold ? threshold / peak : 1.0f;
        const float coefficient = target < gain ? attack : release;
        gain = target + coefficient * (gain - target);
        outL *= gain;
        outR *= gain;

        index = outIndex;
        inL = outL;
        inR = outR;
    }
};

struct DelayLine
{
    std::vector<float> data;
    int writeIndex = 0;

    void prepare(int lengthSamples)
    {
        data.assign((size_t) std::max(4, lengthSamples), 0.0f);
        writeIndex = 0;
    }

    void clear()
    {
        std::fill(data.begin(), data.end(), 0.0f);
        writeIndex = 0;
    }

    float read(int delay) const noexcept
    {
        int i = writeIndex - delay;
        if (i < 0)
            i += (int) data.size();
        return data[(size_t) i];
    }

    void write(float x) noexcept
    {
        data[(size_t) writeIndex] = x;
        if (++writeIndex >= (int) data.size())
            writeIndex = 0;
    }
};

struct Room
{
    static constexpr int kLines = 4;
    DelayLine lines[kLines];
    int lengths[kLines] {};
    float damp[kLines] {};
    float lowState[kLines] {};
    Biquad preHp, preLp;
    float sampleRate = 48000.0f;
    float preDelay = 0.0f;
    std::vector<float> preBuffer;
    int preIndex = 0;

    void prepare(float sr)
    {
        sampleRate = sr;
        lengths[0] = (int) (sr * 0.0297f);
        lengths[1] = (int) (sr * 0.0371f);
        lengths[2] = (int) (sr * 0.0411f);
        lengths[3] = (int) (sr * 0.0437f);
        for (int i = 0; i < kLines; ++i)
        {
            lines[i].prepare(lengths[i]);
            damp[i] = 0.22f + 0.08f * (float) i;
            lowState[i] = 0.0f;
        }
        preBuffer.assign((size_t) (sr * 0.018f) + 4, 0.0f);
        preIndex = 0;
        preHp.setHighPass(sr, 140.0f, 0.707f);
        preLp.setLowPass(sr, 6800.0f, 0.707f);
    }

    void clear()
    {
        for (auto& l : lines)
            l.clear();
        std::fill(preBuffer.begin(), preBuffer.end(), 0.0f);
        preIndex = 0;
        for (float& v : lowState)
            v = 0.0f;
        preHp.reset();
        preLp.reset();
    }

    void process(float inL, float inR, float mix, float& wetL, float& wetR) noexcept
    {
        const float input = 0.5f * (inL + inR);
        preBuffer[(size_t) preIndex] = input;
        if (++preIndex >= (int) preBuffer.size())
            preIndex = 0;
        float delayed = preBuffer[(size_t) preIndex];

        const float pre = preHp.process(preLp.process(delayed)) * 0.25f;
        float d[kLines];
        for (int i = 0; i < kLines; ++i)
            d[i] = lines[i].read(lengths[i] - 1);

        // Hadamard feedback matrix.
        const float f0 = 0.5f * (d[0] + d[1] + d[2] + d[3]);
        const float f1 = 0.5f * (d[0] - d[1] + d[2] - d[3]);
        const float f2 = 0.5f * (d[0] + d[1] - d[2] - d[3]);
        const float f3 = 0.5f * (d[0] - d[1] - d[2] + d[3]);
        const float fb[kLines] = { f0, f1, f2, f3 };

        constexpr float feedback = 0.78f;
        for (int i = 0; i < kLines; ++i)
        {
            lowState[i] += damp[i] * (fb[i] - lowState[i]);
            const float v = pre + feedback * lowState[i];
            lines[i].write(v);
        }

        const float wl = 0.5f * (d[0] + d[2]);
        const float wr = 0.5f * (d[1] + d[3]);
        wetL = wl * mix;
        wetR = wr * mix;
    }
};

struct Body
{
    Biquad peak1, peak2, peak3, peak4, hp, lp;
    float amount = 0.5f;

    void prepare(float sr)
    {
        hp.setHighPass(sr, 42.0f, 0.707f);
        peak1.setPeak(sr, 90.0f, 1.8f, 2.4f);
        peak2.setPeak(sr, 127.0f, 2.2f, 1.8f);
        peak3.setPeak(sr, 187.0f, 2.6f, 1.4f);
        peak4.setPeak(sr, 245.0f, 3.0f, 1.0f);
        lp.setLowPass(sr, 10200.0f, 0.65f);
    }

    void reset()
    {
        peak1.reset(); peak2.reset(); peak3.reset(); peak4.reset(); hp.reset(); lp.reset();
    }

    float process(float x) noexcept
    {
        float y = hp.process(x);
        const float resonances = 0.62f * peak1.process(y)
                               + 0.44f * peak2.process(y)
                               + 0.30f * peak3.process(y)
                               + 0.20f * peak4.process(y);
        y = y + amount * resonances;
        y = lp.process(y);
        return y;
    }
};
} // namespace

struct PianoEngine::Impl
{
    float sampleRate = 48000.0f;
    int maxBlock = 512;
    Params params;

    Voice voices[kMaxVoices];
    SympatheticBank sympathetic;
    LookaheadLimiter limiter;
    Room room;
    Biquad sympatheticHp;
    Body bodyL;
    Body bodyR;
    Rng globalRng;

    float pedal = 0.0f;
    float pedalSmoothed = 0.0f;
    float configuredTuning = 440.0f;
    float pedalNoiseEnv = 0.0f;
    float pedalNoiseFilter = 0.0f;

    float viewL = 0.0f, viewR = 0.0f;
    float meter = 0.0f;
    int activeCount = 0;

    uint32_t randomCounter = 1;

    float outputScale = 0.0044f;

    float notePan(int note, float width) const noexcept
    {
        const float p = clampf((float) (note - 21) / 87.0f, 0.0f, 1.0f);
        return (p - 0.5f) * 1.35f * width;
    }

    void setPan(float pan, float& l, float& r) const noexcept
    {
        const float a = (clampf(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
        l = std::cos(a);
        r = std::sin(a);
    }

    int modeCountForNote(int note) const noexcept
    {
        if (note <= 33) return 56;
        if (note <= 45) return 44;
        if (note <= 57) return 34;
        if (note <= 69) return 27;
        if (note <= 81) return 22;
        if (note <= 93) return 17;
        return 13;
    }

    int stringCountForNote(int note) const noexcept
    {
        if (note <= 28) return 1;
        if (note <= 36) return 2;
        return 3;
    }

    float baseT60(int note) const noexcept
    {
        const float p = (float) (note - 21) / 87.0f;
        return 25.0f * std::exp(-2.42f * p) + 0.9f;
    }

    float brightnessForNote(int note) const noexcept
    {
        const float p = (float) (note - 21) / 87.0f;
        const float f0 = midiToFreq((float) note, params.tuning);
        return std::max(7200.0f, f0 * 9.0f) * (0.7f + 0.6f * params.tone)
             + 2800.0f * (1.0f - p);
    }

    void configureVoice(Voice& v, int midiNote, float vel, bool reuseStringState)
    {
        const uint32_t seed = ((uint32_t) midiNote * 1315423911u) ^ (randomCounter++ * 2654435761u);
        v.rng.seed(seed);
        v.note = midiNote;
        v.velocity = clampf(vel, 0.0f, 1.0f);
        v.keyDown = true;
        v.damperGain = 1.0f;
        v.age = 0;
        v.active = true;

        const float p = (float) (midiNote - 21) / 87.0f;
        const float a4 = params.tuning;
        float f0 = midiToFreq((float) midiNote, a4);
        f0 *= std::pow(2.0f, stretchCents(midiNote) / 1200.0f);

        const float notePanValue = notePan(midiNote, params.stereoWidth);
        setPan(notePanValue, v.panL, v.panR);

        v.stringCount = stringCountForNote(midiNote);
        const int modes = modeCountForNote(midiNote);
        const float t60 = baseT60(midiNote);
        const float strike = clampf(0.118f + 0.026f * p
                                    + 0.0025f * std::sin((float) midiNote * 12.9898f), 0.10f, 0.16f);
        const float highRoll = brightnessForNote(midiNote);
        const float detuneBase = params.detuneCents;
        const float panSpread = 0.16f * params.stereoWidth;

        // Contact shortens from ~4 ms in the bass to well below 1 ms in the
        // treble: the hammer is in touch with the string for less than one
        // period at the top of the compass.
        v.contactTimeTarget = clampf(0.55f / std::max(20.0f, f0), 0.00065f, 0.0045f)
                            * (1.15f - 0.45f * params.tone)
                            * (1.05f - 0.15f * v.velocity);

        // Soft strikes have a longer felt contact and a darker spectrum; hard
        // strikes are shorter, brighter and richer in upper partials.
        const float contactTilt = (v.contactTimeTarget - 0.0018f) * 55.0f;
        const float brightnessTilt = 0.32f - 0.52f * v.velocity + contactTilt;
        float detunes[kNumBanks] = { -detuneBase * 0.55f, detuneBase * 0.35f, detuneBase * 0.55f };
        const float t60scale1[kNumBanks] = { 1.0f, 1.0f, 1.0f };
        const float t60scale2[kNumBanks] = { 1.28f, 0.58f, 1.00f };
        const float t60scale3[kNumBanks] = { 1.30f, 0.55f, 1.02f };
        const float weights1[kNumBanks] = { 1.0f, 1.0f, 1.0f };
        const float weights2[kNumBanks] = { 0.70f, 0.80f, 0.70f };
        const float weights3[kNumBanks] = { 0.58f, 0.82f, 0.72f };
        const float* t60scale = v.stringCount == 1 ? t60scale1 : (v.stringCount == 2 ? t60scale2 : t60scale3);
        const float* weights = v.stringCount == 1 ? weights1 : (v.stringCount == 2 ? weights2 : weights3);
        const float bankPans[kNumBanks] = { -panSpread, 0.0f, panSpread };
        if (v.stringCount == 1)
            detunes[0] = 0.0f;
        if (v.stringCount == 2)
        {
            detunes[0] = -detuneBase * 0.5f;
            detunes[1] = detuneBase * 0.5f;
        }

        for (int b = 0; b < kNumBanks; ++b)
        {
            auto& bank = v.banks[b];
            if (b >= v.stringCount)
            {
                bank.numModes = 0;
                continue;
            }
            bank.setInharmonicity(inharmonicityForNote(midiNote));
            bank.configure(sampleRate, f0, detunes[b], t60 * t60scale[b], strike,
                           modes, weights[b], highRoll, sampleRate, seed + (uint32_t) b * 17u,
                           reuseStringState, brightnessTilt);
            float bl, br;
            setPan(notePanValue + bankPans[b], bl, br);
            bank.panL = bl;
            bank.panR = br;
            if (!reuseStringState)
                bank.reset();
        }

        if (!reuseStringState)
            v.resetStringState();

        // The initial hammer velocity spans roughly 0.25 m/s (pp) to
        // 5.5 m/s (ff); the modal impulse scales with its nonlinear transfer
        // through the felt.
        const float velocityExponent = 0.85f + 1.4f * params.dynamics
                                     + 0.8f * (0.5f - params.velocityCurve);
        v.strikeImpulse = 15.0f * std::pow(v.velocity, clampf(velocityExponent, 0.65f, 2.9f));
        v.struck = false;

        v.noiseEnv = params.mechanical * (0.14f + 1.7f * std::pow(v.velocity, 1.55f));
        v.noiseDecay = std::exp(-1.0f / (sampleRate * 0.035f));
        v.noiseFilter = 0.0f;
        v.noiseHp = 0.0f;
        v.noiseHigh = 0.0f;
        v.thumpEnv = params.mechanical * 0.55f * std::pow(v.velocity, 1.3f);
        v.thumpPhase = 0.0f;
        v.thumpInc = kTwoPi * (48.0f + 60.0f * (1.0f - p)) / sampleRate;
        v.outputGain = 1.0f;
    }

    static float inharmonicityForNote(int note) noexcept
    {
        // Measured-style B curve for a concert grand (A0..C8), minimum near
        // A2 in the wound bass and rising steeply through the treble.
        static constexpr int anchors[] = { 21, 33, 45, 57, 69, 81, 93, 105, 108 };
        static constexpr float values[] = { 0.00043f, 0.00019f, 0.000064f, 0.00024f,
                                            0.00082f, 0.0027f, 0.0075f, 0.025f, 0.050f };
        if (note <= anchors[0]) return values[0];
        for (int i = 1; i < 9; ++i)
        {
            if (note <= anchors[i])
            {
                const float t = (float) (note - anchors[i - 1])
                              / (float) (anchors[i] - anchors[i - 1]);
                return std::exp(std::log(values[i - 1]) * (1.0f - t)
                              + std::log(values[i]) * t);
            }
        }
        return values[8];
    }

    float nextNoise(Voice& v) noexcept { return v.rng.next(); }

    void startVoice(int midiNote, float velocity)
    {
        if (midiNote < kFirstNote || midiNote > kLastNote || velocity <= 0.0f)
            return;

        Voice* chosen = nullptr;
        for (auto& v : voices)
        {
            if (v.active && v.note == midiNote)
            {
                chosen = &v;
                break;
            }
        }

        if (chosen == nullptr)
        {
            for (auto& v : voices)
            {
                if (!v.active)
                {
                    chosen = &v;
                    break;
                }
            }
        }

        if (chosen == nullptr)
        {
            // Prefer a released, well-damped and old voice. Lower importance is
            // stolen first; a held key always outranks a released one.
            Voice* best = &voices[0];
            float bestScore = std::numeric_limits<float>::max();
            for (auto& v : voices)
            {
                const float score = (v.keyDown ? 1000.0f : 0.0f)
                                  + v.damperGain * 100.0f
                                  - (float) std::min(v.age, 1000000) * 0.001f;
                if (score >= bestScore) continue;
                bestScore = score;
                best = &v;
            }
            chosen = best;
        }

        const bool reuse = chosen->active && chosen->note == midiNote
                        && (chosen->keyDown || chosen->damperGain > 0.02f);
        configureVoice(*chosen, midiNote, velocity, reuse);
    }

    void releaseVoice(int midiNote, float velocity)
    {
        for (auto& v : voices)
        {
            if (v.active && v.note == midiNote && v.keyDown)
            {
                v.keyDown = false;
                const float p = (float) (midiNote - 21) / 87.0f;
                const float base = params.releaseTime * 0.16f * (1.15f - 0.5f * p);
                const float lift = 1.25f - 0.5f * clampf(velocity, 0.0f, 1.0f);
                v.damperTau = clampf(base * lift, 0.004f, 0.35f);
                v.noiseEnv = std::max(v.noiseEnv, params.mechanical * 0.12f);
                v.noiseDecay = std::exp(-1.0f / (sampleRate * 0.008f));
            }
        }
    }

    void handlePedal(float value)
    {
        pedal = clampf(value, 0.0f, 1.0f);
        if (std::abs(pedal - pedalSmoothed) > 0.04f)
        {
            pedalNoiseEnv = params.pedalNoise * 0.8f;
            pedalNoiseFilter = 0.0f;
        }
        if (pedal < 0.25f)
        {
            for (auto& v : voices)
            {
                if (v.active && !v.keyDown)
                {
                    const float p = (float) (v.note - 21) / 87.0f;
                    v.damperTau = clampf(params.releaseTime * 0.16f * (1.15f - 0.5f * p), 0.004f, 0.35f);
                }
            }
        }
    }

    void clear()
    {
        for (auto& v : voices)
        {
            v.active = false;
            v.keyDown = false;
            v.resetStringState();
        }
        sympathetic.configure(sampleRate, params.tuning);
        room.clear();
        limiter.reset();
        sympatheticHp.reset();
        bodyL.reset();
        bodyR.reset();
        viewL = viewR = 0.0f;
        meter = 0.0f;
        pedal = pedalSmoothed = 0.0f;
        activeCount = 0;
    }

    void render(float* left, float* right, int numSamples)
    {
        if (numSamples <= 0)
            return;

        const float dt = 1.0f / sampleRate;
        float peak = 0.0f;

        for (int s = 0; s < numSamples; ++s)
        {
            pedalSmoothed += 0.0015f * (pedal - pedalSmoothed);
            const float pedalNow = pedalSmoothed;
            const float sympatheticGain = params.sympathetic * (0.0006f + 0.016f * pedalNow);

            float mixL = 0.0f;
            float mixR = 0.0f;
            float stringMono = 0.0f;
            activeCount = 0;

            for (auto& v : voices)
            {
                if (!v.active)
                    continue;

                ++activeCount;
                ++v.age;

                // The felt hammer is modelled as a broadband, velocity-
                // dependent contact impulse. The resulting modal excitation is
                // spectrally tilted by the contact time and by felt hardness.
                float forceInput = 0.0f;
                if (!v.struck)
                {
                    forceInput = v.strikeImpulse;
                    v.struck = true;
                }

                float bankLeft[kNumBanks] {};
                float bankRight[kNumBanks] {};
                float forceMono = 0.0f;

                for (int b = 0; b < v.stringCount; ++b)
                {
                    auto& bank = v.banks[b];
                    const float out = bank.process(forceInput);
                    bankLeft[b] = out * bank.panL;
                    bankRight[b] = out * bank.panR;
                    forceMono += out * bank.weight;
                }

                float vl = 0.0f, vr = 0.0f;
                for (int b = 0; b < v.stringCount; ++b)
                {
                    vl += bankLeft[b];
                    vr += bankRight[b];
                }

                // Damper envelope. When the key is up and the pedal is up, the
                // felt damper stops the string with a register-dependent time.
                if (!v.keyDown && pedalNow < 0.35f)
                {
                    const float tau = std::max(0.002f, v.damperTau);
                    v.damperGain *= std::exp(-dt / tau);
                }
                else if (v.keyDown)
                {
                    v.damperGain += (1.0f - v.damperGain) * 0.002f;
                }

                vl *= v.damperGain;
                vr *= v.damperGain;
                vl *= v.outputGain;
                vr *= v.outputGain;

                // Hammer contact noise: a short, band-limited chiff that gives
                // the attack its "felt on steel" edge.
                if (v.noiseEnv > 0.00002f)
                {
                    const float n = nextNoise(v);
                    v.noiseHp += 0.28f * (n - v.noiseHp);
                    v.noiseFilter += 0.16f * ((n - v.noiseHp) - v.noiseFilter);
                    v.noiseHigh += 0.78f * (n - v.noiseHigh);
                    const float noise = (v.noiseFilter * 0.65f + v.noiseHigh * 0.75f)
                                      * v.noiseEnv * 0.75f;
                    vl += noise * v.panL;
                    vr += noise * v.panR;
                    v.noiseEnv *= v.noiseDecay;
                }

                if (v.thumpEnv > 0.00002f)
                {
                    const float th = std::sin(v.thumpPhase) * v.thumpEnv * 0.05f;
                    vl += th * v.panL;
                    vr += th * v.panR;
                    v.thumpPhase += v.thumpInc;
                    if (v.thumpPhase > kTwoPi) v.thumpPhase -= kTwoPi;
                    v.thumpEnv *= std::exp(-dt / 0.045f);
                }

                // Fade and retire voices that no longer contribute.
                if (!v.keyDown && v.damperGain < 0.0004f && v.age > (int) (sampleRate * 0.4f))
                {
                    v.active = false;
                }

                mixL += vl;
                mixR += vr;
                stringMono += forceMono * v.damperGain;
            }

            // Sympathetic strings and duplex-scale resonance.
            const float sym = sympatheticHp.process(sympathetic.process(stringMono * sympatheticGain));
            mixL += sym * 0.32f;
            mixR += sym * 0.32f;

            // Soundboard resonances.
            const float bl = bodyL.process(mixL);
            const float br = bodyR.process(mixR);

            float wetL = 0.0f, wetR = 0.0f;
            const float roomAmount = params.room * 0.9f;
            room.process(bl, br, roomAmount, wetL, wetR);

            float outL = bl + wetL;
            float outR = br + wetR;

            // A short look-ahead limiter handles dense pedalled chords without
            // hard clipping; the plugin's master gain follows this stage.
            outL *= outputScale;
            outR *= outputScale;
            limiter.process(outL, outR);
            outL = softClip(outL);
            outR = softClip(outR);

            left[s] = outL;
            right[s] = outR;
            peak = std::max(peak, std::max(std::abs(outL), std::abs(outR)));

            viewL = outL;
            viewR = outR;

            if (pedalNoiseEnv > 0.00001f)
            {
                const float n = globalRng.next();
                pedalNoiseFilter += 0.08f * (n - pedalNoiseFilter);
                const float pn = pedalNoiseFilter * pedalNoiseEnv * 0.12f;
                left[s] += pn;
                right[s] += pn;
                pedalNoiseEnv *= std::exp(-dt / 0.02f);
            }
        }

        meter = std::max(meter * 0.82f, peak);
    }
};

PianoEngine::PianoEngine() : impl(std::make_unique<Impl>()) {}
PianoEngine::~PianoEngine() = default;

void PianoEngine::prepare(double sampleRate, int maximumBlockSize, int)
{
    impl->sampleRate = (float) sampleRate;
    impl->maxBlock = maximumBlockSize;
    impl->params = params;
    impl->room.prepare(impl->sampleRate);
    impl->limiter.prepare(impl->sampleRate);
    impl->sympatheticHp.setHighPass(impl->sampleRate, 90.0f, 0.707f);
    impl->bodyL.prepare(impl->sampleRate);
    impl->bodyR.prepare(impl->sampleRate);
    impl->configuredTuning = params.tuning;
    impl->sympathetic.configure(impl->sampleRate, params.tuning);
    impl->clear();
}

void PianoEngine::reset()
{
    impl->clear();
}

void PianoEngine::setParams(const Params& p) noexcept
{
    params = p;
    impl->params = p;
    if (std::abs(p.tuning - impl->configuredTuning) > 0.01f)
    {
        impl->configuredTuning = p.tuning;
        impl->sympathetic.configure(impl->sampleRate, p.tuning);
    }
}

void PianoEngine::noteOn(int midiNote, float velocity, int)
{
    impl->startVoice(midiNote, velocity);
}

void PianoEngine::noteOff(int midiNote, float velocity, int)
{
    impl->releaseVoice(midiNote, velocity);
}

void PianoEngine::sustainPedal(float value, int)
{
    impl->handlePedal(value);
}

void PianoEngine::allNotesOff(bool immediately)
{
    if (immediately)
    {
        for (auto& v : impl->voices)
        {
            v.active = false;
            v.keyDown = false;
            v.resetStringState();
        }
        return;
    }
    for (auto& v : impl->voices)
    {
        if (v.active && v.keyDown)
        {
            v.keyDown = false;
            v.damperTau = std::max(0.01f, params.releaseTime);
        }
    }
}

void PianoEngine::render(float* left, float* right, int numSamples)
{
    impl->render(left, right, numSamples);
    meter.store(impl->meter, std::memory_order_relaxed);
    activeVoices.store(impl->activeCount, std::memory_order_relaxed);
}

} // namespace corda
