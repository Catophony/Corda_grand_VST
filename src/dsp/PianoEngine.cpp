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
constexpr int kMaxExcitation = 1024;
constexpr int kNumBanks = 3;
constexpr int kMaxVoices = PianoEngine::kMaxVoices;
constexpr int kSympatheticModes = 36;
constexpr int kSympatheticNotes = 12;
constexpr float kNoteTrimDb[88] = {
      9.85f,   9.41f,   9.29f,   9.61f,   9.63f,   9.78f,   9.83f,   8.66f,
      6.74f,   5.63f,   5.50f,   5.49f,   5.33f,   5.00f,   4.92f,   4.56f,
      3.81f,   3.51f,   3.53f,   3.44f,   2.84f,   2.35f,   2.36f,   2.32f,
      2.17f,   2.28f,   2.71f,   3.19f,   3.52f,   3.59f,   3.59f,   3.24f,
      2.55f,   2.44f,   2.83f,   2.85f,   2.51f,   2.26f,   2.68f,   3.23f,
      3.15f,   2.38f,   1.97f,   2.11f,   1.92f,   2.03f,   2.75f,   3.34f,
      3.17f,   2.81f,   2.37f,   2.04f,   2.24f,   2.60f,   3.10f,   3.60f,
      3.09f,   0.38f,  -3.44f,  -5.32f,  -5.12f,  -4.65f,  -4.22f,  -4.01f,
     -4.33f,  -4.46f,  -4.33f,  -4.41f,  -4.07f,  -3.24f,  -2.55f,  -1.93f,
     -1.35f,  -1.37f,  -1.84f,  -1.89f,  -1.51f,  -0.98f,  -0.50f,  -0.37f,
     -0.20f,   0.14f,   0.65f,   1.51f,   2.36f,   2.77f,   2.75f,   2.19f
};


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

// Magnitude of the discrete Fourier transform of a short excitation pulse at
// one angular frequency. Used to compensate the hammer-contact low-pass so
// that every partial receives the intended physical amplitude.
inline float pulseMagnitude(const float* x, int n, float omega) noexcept
{
    const double c = std::cos((double) omega);
    const double s = std::sin((double) omega);
    double cr = 1.0, ci = 0.0, re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double v = (double) x[i];
        re += v * cr;
        im -= v * ci;
        const double nr = cr * c - ci * s;
        ci = cr * s + ci * c;
        cr = nr;
    }
    return (float) std::sqrt(re * re + im * im);
}

// Railsback stretch: the bass is tuned flat and the treble sharp. This is one
// of the reasons a real piano sounds in tune with itself but not with a
// perfectly equal-tempered oscillator bank.
inline float stretchCents(int note) noexcept
{
    const float d = (float) note - 60.0f;
    if (d < 0.0f)
        return -0.085f * std::pow(-d, 1.55f);
    // The treble stretch of a real grand is much gentler than a naive
    // Railsback power curve; too much stretch makes chords sound out of tune.
    return 0.035f * std::pow(d, 1.50f);
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

// A first-order allpass section used for string stiffness/dispersion.
struct WaveAllpass
{
    float a = 0.0f, z = 0.0f;
    float process(float x) noexcept
    {
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }
    void reset() noexcept { z = 0.0f; }
};

struct WaveLowpass
{
    float a = 0.0f, z = 0.0f;
    float process(float x) noexcept
    {
        z = (1.0f - a) * x + a * z;
        return z;
    }
    void reset() noexcept { z = 0.0f; }
};

struct WaveDelay
{
    std::vector<float> data;
    int writeIndex = 0;

    void prepare(int n)
    {
        data.assign((size_t) std::max(8, n), 0.0f);
        writeIndex = 0;
    }
    void reset()
    {
        std::fill(data.begin(), data.end(), 0.0f);
        writeIndex = 0;
    }
    float read(float delay) const noexcept
    {
        float r = (float) writeIndex - delay;
        while (r < 0.0f) r += (float) data.size();
        const int i = (int) r;
        const float f = r - (float) i;
        const int j = (i + 1) % (int) data.size();
        return data[(size_t) i] * (1.0f - f) + data[(size_t) j] * f;
    }
    void write(float x) noexcept
    {
        data[(size_t) writeIndex] = x;
        if (++writeIndex >= (int) data.size())
            writeIndex = 0;
    }
};

inline float allpassPhaseDelay(float a, float w) noexcept
{
    const float nr = a + std::cos(w), ni = -std::sin(w);
    const float dr = 1.0f + a * std::cos(w), di = -a * std::sin(w);
    const float ph = std::atan2(ni, nr) - std::atan2(di, dr);
    return -ph / w;
}

inline float lowpassPhaseDelay(float a, float w) noexcept
{
    const float dr = 1.0f - a * std::cos(w), di = a * std::sin(w);
    const float ph = -std::atan2(di, dr);
    return -ph / w;
}

struct StringBank
{
    WaveDelay delay;
    WaveDelay forceDelay;
    WaveAllpass dispersion[3];
    WaveLowpass loss;
    float lineDelay = 100.0f;
    float loopGain = 0.999f;
    float strikePosition = 0.125f;
    float panL = 0.7071f, panR = 0.7071f;
    float weight = 1.0f;
    int numModes = 1;
    bool useModal = false;
    Mode modes[kMaxModes];
    float inharmonicity = 0.0002f;
    float forceScale = 0.001f;

    void reset()
    {
        delay.reset();
        forceDelay.reset();
        loss.reset();
        for (int i = 0; i < numModes; ++i) modes[i].reset();
        for (auto& a : dispersion)
            a.reset();
    }

    void configure(float sr, float frequency, float detuneCents, float baseT60,
                   float strike, int modeCount, float bankWeight, float, float, uint32_t,
                   bool preserveState, float, const float*, int, float, float timbre)
    {
        weight = bankWeight;
        strikePosition = clampf(strike, 0.06f, 0.20f);
        const float f0 = frequency * std::pow(2.0f, detuneCents / 1200.0f);
        const float b = std::max(0.0f, inharmonicity);
        const float w0 = kTwoPi * f0 / sr;
        useModal = f0 >= 750.0f;

        if (useModal)
        {
            numModes = 0;
            const float q = std::clamp((f0 - 27.5f) / 4158.0f, 0.0f, 1.0f);
            const float ampExp = 1.00f + 0.30f * std::clamp((q - 0.15f) / 0.55f, 0.0f, 1.0f)
                                          - 0.10f * std::clamp((q - 0.70f) / 0.30f, 0.0f, 1.0f)
                                          - 0.22f * timbre;
            for (int n = 1; n <= modeCount; ++n)
            {
                const float nn = (float) n;
                const float freq = nn * f0 * std::sqrt(1.0f + b * nn * nn);
                if (freq >= sr * 0.475f)
                    break;
                const float ratioToF0 = freq / f0;
                const float t60 = std::max(0.04f, baseT60 / std::pow(nn, 0.63f));
                const float sigma = kLn1000 / t60;
                const float rr = std::exp(-sigma / sr);
                const float w = kTwoPi * freq / sr;
                const float phi = std::sin(kPi * nn * strike);
                const float roll = 1.0f / (1.0f + std::pow(freq / (9000.0f * (1.0f + 0.2f * timbre)), 1.6f));
                const float amp = (0.22f + 0.78f * std::abs(phi))
                                / std::pow(ratioToF0, ampExp)
                                * roll * (phi < 0.0f ? -1.0f : 1.0f);
                auto& m = modes[numModes++];
                m.x1 = m.x2 = 0.0f;
                m.a1 = 2.0f * rr * std::cos(w);
                m.a2 = -rr * rr;
                m.b0 = amp * std::sin(w) * 0.0035f;
                m.phi = phi;
            }
            if (!preserveState) reset();
            return;
        }

        // Three first-order allpasses create the stiffness dispersion. The
        // coefficient is negative so upper partials are sharp (piano
        // inharmonicity), not flat.
        const float coeff = -clampf(0.06f + b * 20.0f, 0.06f, 0.35f);
        for (auto& a : dispersion)
            a.a = coeff;
        loss.a = clampf(0.10f + 0.08f * (0.5f - 0.5f * timbre), 0.06f, 0.18f);

        const float pd = 3.0f * allpassPhaseDelay(coeff, w0) + lowpassPhaseDelay(loss.a, w0);
        lineDelay = std::max(3.5f, sr / f0 - pd);
        const int need = (int) (lineDelay * 3.0f) + 16;
        if ((int) delay.data.size() < need)
            delay.prepare(need);
        const int needForce = (int) (2.0f * strikePosition * lineDelay * 2.0f) + 16;
        if ((int) forceDelay.data.size() < needForce)
            forceDelay.prepare(needForce);

        // Loop gain sets the fundamental T60. The lowpass makes high partials
        // decay faster, as on a struck string.
        const float t60 = std::max(0.3f, baseT60);
        loopGain = std::pow(10.0f, -3.0f / (t60 * f0));
        loopGain = clampf(loopGain, 0.80f, 0.99995f);

        if (!preserveState)
            reset();
    }

    void setInharmonicity(float b) noexcept { inharmonicity = b; }

    float processAndDisplacement(float force, float& displacement) noexcept
    {
        if (useModal)
        {
            float sum = 0.0f, d = 0.0f;
            for (int i = 0; i < numModes; ++i)
            {
                auto& m = modes[i];
                const float x = m.a1 * m.x1 + m.a2 * m.x2 + m.b0 * force;
                m.x2 = m.x1; m.x1 = x;
                sum += x;
                if (i < 18) d += x * m.phi;
            }
            displacement = d * 0.020f;
            return sum * weight * 20.0f;
        }
        // Output at the bridge.
        const float y = delay.read(lineDelay);

        // Force is injected at the strike point: 1 - z^(-2 p L) creates the
        // characteristic comb of a hammer striking at 1/7...1/8 of the string.
        forceDelay.write(force);
        const float delayedForce = forceDelay.read(2.0f * strikePosition * lineDelay);
        const float excitation = (force - delayedForce) * forceScale;

        float loop = loss.process(y);
        for (auto& a : dispersion)
            loop = a.process(loop);
        loop *= loopGain;
        delay.write(loop + excitation);

        displacement = y * 0.020f;
        return y * weight * 800.0f;
    }

    float process(float force) noexcept
    {
        float d = 0.0f;
        return processAndDisplacement(force, d);
    }
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
    int startDelay = 0;

    float contactTimeTarget = 0.003f;

    float noiseEnv = 0.0f;
    float noiseDecay = 0.0f;
    float noiseFilter = 0.0f;
    float noiseHp = 0.0f;
    float noiseHigh = 0.0f;
    float noiseLow = 0.0f;
    float thumpEnv = 0.0f;
    float thumpPhase = 0.0f;
    float thumpInc = 0.0f;
    float knockEnv = 0.0f;
    float knockPhase = 0.0f;
    float knockInc = 0.0f;
    float toneLpL = 0.0f, toneLpR = 0.0f;
    float toneLpCoef = 0.0f;
    float damperThumpEnv = 0.0f;
    float damperThumpPhase = 0.0f;
    float damperThumpInc = 0.0f;

    float hammerPos = 0.0f;
    float hammerVel = 0.0f;
    float hammerMass = 0.008f;
    float hammerStiffness = 20000.0f;
    float hammerNonlinearity = 2.6f;
    float hammerDamping = 4.0f;
    float stringDisplacement = 0.0f;
    float bridgeState = 0.0f;
    bool hammerActive = false;
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
                const float t60 = 0.45f + 0.30f / (float) h;
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

struct SoundboardBank
{
    static constexpr int kModes = 24;
    Mode modes[kModes];
    int count = 0;

    void configure(float sr)
    {
        static constexpr float freqs[kModes] = {
            62.0f, 90.0f, 105.0f, 127.0f, 187.0f, 222.0f, 245.0f, 305.0f,
            380.0f, 470.0f, 580.0f, 710.0f, 870.0f, 1060.0f, 1290.0f, 1570.0f,
            1910.0f, 2320.0f, 2820.0f, 3430.0f, 4180.0f, 5100.0f, 6250.0f, 7600.0f
        };
        count = kModes;
        for (int i = 0; i < kModes; ++i)
        {
            const float f = freqs[i];
            const float t60 = clampf(2.2f / (0.033f * f), 0.060f, 0.45f);
            const float sigma = kLn1000 / t60;
            const float r = std::exp(-sigma / sr);
            const float w = kTwoPi * f / sr;
            auto& m = modes[i];
            m.x1 = m.x2 = 0.0f;
            m.a1 = 2.0f * r * std::cos(w);
            m.a2 = -r * r;
            // Radiation falls gently above the low body modes.
            const float gain = 0.55f / (1.0f + std::pow(f / 900.0f, 0.85f));
            m.b0 = gain * std::sin(w);
            m.phi = 0.0f;
        }
    }

    void reset()
    {
        for (int i = 0; i < count; ++i)
            modes[i].reset();
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
    float threshold = 0.90f;

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
    std::vector<float> earlyBuffer;
    int preIndex = 0;
    int earlyIndex = 0;
    Biquad earlyLp;

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
            damp[i] = 0.16f + 0.06f * (float) i;
            lowState[i] = 0.0f;
        }
        preBuffer.assign((size_t) (sr * 0.018f) + 4, 0.0f);
        preIndex = 0;
        earlyBuffer.assign((size_t) (sr * 0.060f) + 8, 0.0f);
        earlyIndex = 0;
        preHp.setHighPass(sr, 100.0f, 0.707f);
        preLp.setLowPass(sr, 8500.0f, 0.707f);
        earlyLp.setLowPass(sr, 6200.0f, 0.707f);
    }

    void clear()
    {
        for (auto& l : lines)
            l.clear();
        std::fill(preBuffer.begin(), preBuffer.end(), 0.0f);
        std::fill(earlyBuffer.begin(), earlyBuffer.end(), 0.0f);
        preIndex = earlyIndex = 0;
        for (float& v : lowState)
            v = 0.0f;
        preHp.reset();
        preLp.reset();
        earlyLp.reset();
    }

    void process(float inL, float inR, float mix, float& wetL, float& wetR) noexcept
    {
        const float input = 0.5f * (inL + inR);
        earlyBuffer[(size_t) earlyIndex] = input;
        preBuffer[(size_t) preIndex] = input;
        if (++preIndex >= (int) preBuffer.size())
            preIndex = 0;
        if (++earlyIndex >= (int) earlyBuffer.size())
            earlyIndex = 0;
        float delayed = preBuffer[(size_t) preIndex];

        auto earlyTap = [this] (float seconds) noexcept
        {
            int d = (int) (sampleRate * seconds);
            int i = earlyIndex - d;
            if (i < 0) i += (int) earlyBuffer.size();
            return earlyBuffer[(size_t) i];
        };
        const float e0 = earlyTap(0.0031f);
        const float e1 = earlyTap(0.0047f);
        const float e2 = earlyTap(0.0063f);
        const float e3 = earlyTap(0.0089f);
        const float e4 = earlyTap(0.0121f);
        const float e5 = earlyTap(0.0163f);
        const float earlyL = 0.30f * e0 + 0.16f * e2 - 0.08f * e4;
        const float earlyR = -0.28f * e1 + 0.14f * e3 + 0.08f * e5;
        const float earlyLpOut = earlyLp.process(earlyL);

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

        constexpr float feedback = 0.76f;
        for (int i = 0; i < kLines; ++i)
        {
            lowState[i] += damp[i] * (fb[i] - lowState[i]);
            const float v = pre + feedback * lowState[i];
            lines[i].write(v);
        }

        const float wl = 0.5f * (d[0] + d[2]);
        const float wr = 0.5f * (d[1] + d[3]);
        wetL = (wl + earlyLpOut) * mix;
        wetR = (wr + earlyR) * mix;
    }
};

struct Body
{
    Biquad peak1, peak2, peak3, peak4, peak5, peak6, hp, lp;
    float amount = 0.5f;

    void prepare(float sr)
    {
        hp.setHighPass(sr, 42.0f, 0.707f);
        peak1.setPeak(sr, 90.0f, 1.8f, 2.4f);
        peak2.setPeak(sr, 127.0f, 2.2f, 1.8f);
        peak3.setPeak(sr, 187.0f, 2.6f, 1.4f);
        peak4.setPeak(sr, 245.0f, 2.4f, 2.0f);
        peak5.setPeak(sr, 390.0f, 0.95f, 4.0f);
        peak6.setPeak(sr, 640.0f, 1.15f, 2.0f);
        lp.setLowPass(sr, 9000.0f, 0.65f);
    }

    void reset()
    {
        peak1.reset(); peak2.reset(); peak3.reset(); peak4.reset();
        peak5.reset(); peak6.reset(); hp.reset(); lp.reset();
    }

    float process(float x) noexcept
    {
        float y = hp.process(x);
        const float resonances = 0.62f * peak1.process(y)
                               + 0.44f * peak2.process(y)
                               + 0.30f * peak3.process(y)
                               + 0.20f * peak4.process(y)
                               + 0.26f * peak5.process(y)
                               + 0.18f * peak6.process(y);
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
    SoundboardBank soundboard;
    Biquad soundboardHp;
    LookaheadLimiter limiter;
    Room room;
    Biquad sympatheticHp;
    Body bodyL;
    Body bodyR;
    Rng globalRng;

    float pedal = 0.0f;
    float pedalSmoothed = 0.0f;
    float sympatheticDamper = 1.0f;
    float configuredTuning = 440.0f;
    float pedalNoiseEnv = 0.0f;
    float pedalNoiseFilter = 0.0f;

    float viewL = 0.0f, viewR = 0.0f;
    float meter = 0.0f;
    int activeCount = 0;

    uint32_t randomCounter = 1;

    float outputScale = 0.0010f;

    static void applyVoicing(Params& p) noexcept
    {
        switch (p.voicing)
        {
            case 1: // Intimate: dark, woody, close, more body
                p.tone *= 0.62f;
                p.timbre = -0.65f;
                p.body = std::min(1.0f, p.body * 1.55f);
                p.stereoWidth *= 0.65f;
                p.room = std::min(1.0f, p.room * 1.35f);
                p.mechanical *= 1.15f;
                p.dynamics = std::min(1.0f, p.dynamics * 1.08f);
                p.detuneCents = std::min(4.0f, p.detuneCents * 1.2f);
                p.releaseTime = std::min(1.2f, p.releaseTime * 1.1f);
                break;
            case 2: // Bright studio: hard hammer, lean body, more attack
                p.tone = std::min(1.0f, p.tone * 1.45f + 0.12f);
                p.timbre = 0.95f;
                p.body *= 0.55f;
                p.room *= 0.65f;
                p.mechanical *= 0.80f;
                p.dynamics = std::min(1.0f, p.dynamics + 0.12f);
                p.detuneCents *= 0.80f;
                p.stereoWidth = std::min(1.0f, p.stereoWidth * 1.1f);
                break;
            case 3: // Vintage: detuned, darker, more mechanical noise
                p.tone *= 0.72f;
                p.timbre = -0.35f;
                p.body = std::min(1.0f, p.body * 1.4f);
                p.detuneCents = std::min(4.0f, p.detuneCents * 1.9f + 0.4f);
                p.mechanical *= 1.35f;
                p.room = std::min(1.0f, p.room * 1.1f);
                p.releaseTime = std::min(1.2f, p.releaseTime * 1.15f);
                p.velocityCurve *= 0.9f;
                break;
            default:
                break;
        }
        p.tone = clampf(p.tone, 0.0f, 1.0f);
        p.body = clampf(p.body, 0.0f, 1.0f);
        p.room = clampf(p.room, 0.0f, 1.0f);
        p.stereoWidth = clampf(p.stereoWidth, 0.0f, 1.0f);
        p.mechanical = clampf(p.mechanical, 0.0f, 1.0f);
        p.dynamics = clampf(p.dynamics, 0.0f, 1.0f);
        p.velocityCurve = clampf(p.velocityCurve, 0.0f, 1.0f);
        p.detuneCents = clampf(p.detuneCents, 0.0f, 4.0f);
        p.releaseTime = clampf(p.releaseTime, 0.05f, 1.2f);
        p.timbre = clampf(p.timbre, -1.0f, 1.0f);
    }

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
        // The lowest single strings still have two polarisations. Presenting
        // them as two weakly detuned banks gives the characteristic double
        // decay and slow bass beating without pretending there are two strings.
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
        const float f0 = midiToFreq((float) note, params.tuning);
        const float track = std::max(1.55f * f0, 6000.0f * std::pow(600.0f / std::max(600.0f, f0), 0.5f));
        return track * (0.72f + 0.56f * params.tone) * (1.0f + 0.20f * params.timbre);
    }

    void configureVoice(Voice& v, int midiNote, float vel, bool reuseStringState)
    {
        const uint32_t seed = ((uint32_t) midiNote * 1315423911u) ^ (randomCounter++ * 2654435761u);
        v.rng.seed(seed);
        v.startDelay = (int) (sampleRate * (0.00015f + 0.00165f * (0.5f + 0.5f * v.rng.next())));
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

        v.stringCount = midiNote >= 80 ? 1 : stringCountForNote(midiNote);
        const int modes = modeCountForNote(midiNote);
        const float t60 = baseT60(midiNote) * (0.94f + 0.12f * (0.5f + 0.5f * v.rng.next()));
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
        const float brightnessTilt = 0.32f - 0.52f * v.velocity + contactTilt + 0.10f * params.timbre;
        // One dominant string bank with low-level detuned side banks. This
        // keeps a natural, long fundamental decay while adding the beating of
        // a real unison without the deep phase cancellation of three equal
        // independent oscillators.
        float detunes[kNumBanks] = { 0.0f, detuneBase * 0.5f, -detuneBase * 0.5f };
        const float t60scale1[kNumBanks] = { 1.0f, 1.0f, 1.0f };
        const float t60scale2[kNumBanks] = { 1.0f, 0.50f, 2.50f };
        const float t60scale3[kNumBanks] = { 1.0f, 0.55f, 2.50f };
        const float weights1[kNumBanks] = { 1.0f, 1.0f, 1.0f };
        const float weights2[kNumBanks] = { 0.46f, 0.34f, 0.20f };
        const float weights3[kNumBanks] = { 0.46f, 0.34f, 0.20f };
        const float t60scaleBass[kNumBanks] = { 1.0f, 0.50f, 2.50f };
        const float weightsBass[kNumBanks] = { 0.46f, 0.34f, 0.20f };
        const float* t60scale = midiNote <= 28 ? t60scaleBass
                              : (v.stringCount == 1 ? t60scale1 : (v.stringCount == 2 ? t60scale2 : t60scale3));
        const float* weights = midiNote <= 28 ? weightsBass
                            : (v.stringCount == 1 ? weights1 : (v.stringCount == 2 ? weights2 : weights3));
        const float bankPans[kNumBanks] = { 0.0f, -panSpread, panSpread };
        if (midiNote <= 28)
            detunes[1] = 0.10f;
        else if (v.stringCount == 2)
            detunes[1] = detuneBase * 0.5f;

        // Hammer state: the felt is compressed against the string with a
        // nonlinear force and the hammer rebounds from the actual string
        // displacement. This is what makes the attack struck rather than
        // plucked.
        v.hammerMass = 0.0032f + 0.0078f * std::exp(-(float) (midiNote - 21) / 40.0f);
        const float contact = std::max(0.00045f, v.contactTimeTarget);
        v.hammerStiffness = v.hammerMass * std::pow(kPi / contact, 2.0f);
        v.hammerNonlinearity = 2.45f + 0.6f * params.tone;
        v.hammerDamping = 2.0f * 0.18f * std::sqrt(v.hammerStiffness * v.hammerMass);
        v.hammerPos = 0.0f;
        v.hammerVel = 0.25f * std::pow(22.0f, v.velocity);
        v.stringDisplacement = 0.0f;
        v.hammerActive = true;

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
                           reuseStringState, brightnessTilt,
                           nullptr, 0, 0.0f, params.timbre);
            float bl, br;
            setPan(notePanValue + bankPans[b], bl, br);
            bank.panL = bl;
            bank.panR = br;
            if (!reuseStringState)
                bank.reset();
        }

        if (!reuseStringState)
            v.resetStringState();

        const float treble = clampf((float) (midiNote - 72) / 36.0f, 0.0f, 1.0f);
        v.noiseEnv = params.mechanical * (0.16f + 1.8f * std::pow(v.velocity, 1.55f))
                   * (1.0f - 0.55f * treble) * (1.0f + 0.25f * params.timbre);
        v.noiseDecay = std::exp(-1.0f / (sampleRate * (0.010f + 0.018f * (1.0f - p))));
        v.noiseFilter = 0.0f;
        v.noiseHp = 0.0f;
        v.noiseHigh = 0.0f;
        v.noiseLow = 0.0f;
        v.thumpEnv = params.mechanical * (0.35f + 0.55f * std::pow(v.velocity, 1.25f));
        v.thumpPhase = 0.0f;
        v.thumpInc = kTwoPi * (52.0f + 55.0f * (1.0f - p)) / sampleRate;
        v.knockEnv = params.mechanical * (0.20f + 0.65f * std::pow(v.velocity, 1.4f));
        v.knockPhase = 0.0f;
        v.knockInc = kTwoPi * (165.0f + 135.0f * (1.0f - p)) / sampleRate;

        // The treble is not a brighter version of the midrange: above C6 the
        // radiation is gently rolled off to avoid a glassy, synthetic top.
        float toneCutoff = 20000.0f;
        if (midiNote >= 76)
            toneCutoff = std::max(midiNote >= 72 ? f0 * 1.9f : 2200.0f, 2200.0f);
        v.toneLpCoef = std::exp(-kTwoPi * toneCutoff / sampleRate);
        v.toneLpL = v.toneLpR = 0.0f;
        v.damperThumpEnv = 0.0f;
        v.damperThumpPhase = 0.0f;
        v.damperThumpInc = kTwoPi * (120.0f + 70.0f * (1.0f - p)) / sampleRate;

        const int trimIndex = std::clamp(midiNote - 21, 0, 87);
        v.outputGain = std::pow(10.0f, kNoteTrimDb[trimIndex] / 20.0f);
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
                const float base = params.releaseTime * 0.11f * (1.15f - 0.5f * p);
                const float lift = 1.25f - 0.5f * clampf(velocity, 0.0f, 1.0f);
                v.damperTau = clampf(base * lift, 0.003f, 0.25f);
                v.noiseEnv = std::max(v.noiseEnv, params.mechanical * 0.12f);
                v.noiseDecay = std::exp(-1.0f / (sampleRate * 0.008f));
                v.damperThumpEnv = params.mechanical * (0.10f + 0.25f * clampf(velocity, 0.0f, 1.0f));
                v.damperThumpPhase = 0.0f;
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
                    v.damperTau = clampf(params.releaseTime * 0.11f * (1.15f - 0.5f * p), 0.003f, 0.25f);
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
        soundboard.reset();
        soundboardHp.reset();
        limiter.reset();
        sympatheticHp.reset();
        bodyL.reset();
        bodyR.reset();
        viewL = viewR = 0.0f;
        meter = 0.0f;
        pedal = pedalSmoothed = 0.0f;
        sympatheticDamper = 1.0f;
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
            if (pedalNow > 0.35f)
                sympatheticDamper += (1.0f - sympatheticDamper) * 0.003f;
            else
                sympatheticDamper *= std::exp(-dt / 0.12f);
            const float sympatheticGain = params.sympathetic * (0.0001f + 0.0025f * pedalNow);

            float mixL = 0.0f;
            float mixR = 0.0f;
            float stringMono = 0.0f;
            activeCount = 0;

            for (auto& v : voices)
            {
                if (!v.active)
                    continue;

                if (v.startDelay > 0)
                {
                    --v.startDelay;
                    continue;
                }

                ++activeCount;
                ++v.age;

                // Nonlinear felt hammer in contact with the string. The
                // hammer is driven by the actual string displacement at the
                // strike point, not by a pre-recorded impulse.
                float forceInput = 0.0f;
                if (v.hammerActive)
                {
                    const float compression = v.hammerPos - v.stringDisplacement;
                    if (compression > 0.0f)
                    {
                        const float cn = compression / 0.00060f;
                        const float spring = v.hammerStiffness * compression
                                           * std::pow(std::max(0.0f, cn),
                                                      v.hammerNonlinearity - 1.0f);
                        const float damping = v.hammerDamping * std::max(0.0f, v.hammerVel);
                        forceInput = std::max(0.0f, spring + damping);
                        forceInput = std::min(forceInput, 2500.0f);
                    }
                    v.hammerVel -= forceInput / v.hammerMass * dt;
                    v.hammerPos += v.hammerVel * dt;
                    if (v.hammerPos < -0.002f && v.hammerVel <= 0.0f)
                        v.hammerActive = false;
                }
                forceInput += v.bridgeState * 0.012f;

                float bankLeft[kNumBanks] {};
                float bankRight[kNumBanks] {};
                float forceMono = 0.0f;
                float displacement = 0.0f;

                for (int b = 0; b < v.stringCount; ++b)
                {
                    auto& bank = v.banks[b];
                    float d = 0.0f;
                    const float out = bank.processAndDisplacement(forceInput, d);
                    bankLeft[b] = out * bank.panL;
                    bankRight[b] = out * bank.panR;
                    forceMono += out * bank.weight;
                    displacement += d;
                }
                v.stringDisplacement = displacement / std::max(1, v.stringCount);
                v.bridgeState = 0.85f * v.bridgeState
                              + 0.15f * (bankLeft[0] + bankRight[0]) * 0.5f;

                float vl = 0.0f, vr = 0.0f;
                for (int b = 0; b < v.stringCount; ++b)
                {
                    vl += bankLeft[b];
                    vr += bankRight[b];
                }

                if (v.toneLpCoef < 0.999f)
                {
                    const float a = 1.0f - v.toneLpCoef;
                    v.toneLpL += a * (vl - v.toneLpL);
                    v.toneLpR += a * (vr - v.toneLpR);
                    vl = v.toneLpL;
                    vr = v.toneLpR;
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
                    v.noiseLow += 0.10f * (n - v.noiseLow);
                    const float noise = (v.noiseLow * 1.00f + v.noiseFilter * 0.55f
                                       + v.noiseHigh * 0.10f) * v.noiseEnv * 0.75f;
                    vl += noise * v.panL;
                    vr += noise * v.panR;
                    v.noiseEnv *= v.noiseDecay;
                }

                if (v.thumpEnv > 0.00002f)
                {
                    const float th = std::sin(v.thumpPhase) * v.thumpEnv * 0.095f;
                    vl += th * v.panL;
                    vr += th * v.panR;
                    v.thumpPhase += v.thumpInc;
                    if (v.thumpPhase > kTwoPi) v.thumpPhase -= kTwoPi;
                    v.thumpEnv *= std::exp(-dt / 0.055f);
                }

                if (v.damperThumpEnv > 0.00002f)
                {
                    const float dth = std::sin(v.damperThumpPhase) * v.damperThumpEnv
                                    * std::exp(-v.age * dt / 0.020f) * 0.050f;
                    vl += dth * v.panL;
                    vr += dth * v.panR;
                    v.damperThumpPhase += v.damperThumpInc;
                    if (v.damperThumpPhase > kTwoPi) v.damperThumpPhase -= kTwoPi;
                    v.damperThumpEnv *= std::exp(-dt / 0.030f);
                }

                if (v.knockEnv > 0.00002f)
                {
                    const float kn = std::sin(v.knockPhase) * v.knockEnv
                                   * std::exp(-v.age * dt / 0.012f) * 0.060f;
                    vl += kn * v.panL;
                    vr += kn * v.panR;
                    v.knockPhase += v.knockInc;
                    if (v.knockPhase > kTwoPi) v.knockPhase -= kTwoPi;
                    v.knockEnv *= std::exp(-dt / 0.018f);
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
            const float sym = sympatheticHp.process(
                sympathetic.process(stringMono * sympatheticGain)) * sympatheticDamper;
            mixL += sym * 0.10f;
            mixR += sym * 0.10f;

            // Shared soundboard modal bank: a dense, short wooden resonance
            // that is excited by all strings through the bridge.
            const float boardIn = 0.5f * (mixL + mixR);
            const float board = soundboardHp.process(soundboard.process(boardIn * 0.007f));
            mixL += board * 0.42f;
            mixR += board * 0.42f;

            // Soundboard resonances.
            const float bl = bodyL.process(mixL);
            const float br = bodyR.process(mixR);

            float wetL = 0.0f, wetR = 0.0f;
            const float roomAmount = params.room * 1.0f;
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
    impl->soundboard.configure(impl->sampleRate);
    impl->soundboardHp.setHighPass(impl->sampleRate, 105.0f, 0.707f);
    impl->limiter.prepare(impl->sampleRate);
    impl->sympatheticHp.setHighPass(impl->sampleRate, 180.0f, 0.707f);
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
    Params q = p;
    Impl::applyVoicing(q);
    params = q;
    impl->params = q;
    if (std::abs(q.tuning - impl->configuredTuning) > 0.01f)
    {
        impl->configuredTuning = q.tuning;
        impl->sympathetic.configure(impl->sampleRate, q.tuning);
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
