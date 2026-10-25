#include "dsp/PianoEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
int gSampleRate = 48000;

struct Event
{
    int64_t sample;
    int type; // 0 note on, 1 note off, 2 pedal
    int note;
    float value;
};

void writeWav24(const std::string& path, const std::vector<float>& left,
                const std::vector<float>& right, int sampleRate)
{
    std::ofstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open output file: " + path);

    const uint32_t frames = (uint32_t) left.size();
    const uint16_t channels = 2;
    const uint16_t bits = 24;
    const uint16_t blockAlign = (uint16_t) (channels * bits / 8);
    const uint32_t byteRate = (uint32_t) sampleRate * blockAlign;
    const uint32_t dataSize = frames * blockAlign;

    auto u32 = [&f] (uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&f] (uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };

    f.write("RIFF", 4); u32(36 + dataSize); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(channels); u32((uint32_t) sampleRate);
    u32(byteRate); u16(blockAlign); u16(bits);
    f.write("data", 4); u32(dataSize);

    std::vector<uint8_t> bytes(dataSize);
    uint8_t* p = bytes.data();
    for (uint32_t i = 0; i < frames; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float x = ch == 0 ? left[i] : right[i];
            x = std::clamp(x, -1.0f, 1.0f);
            int32_t v = (int32_t) std::lround(x * 8388607.0f);
            p[0] = (uint8_t) (v & 0xff);
            p[1] = (uint8_t) ((v >> 8) & 0xff);
            p[2] = (uint8_t) ((v >> 16) & 0xff);
            p += 3;
        }
    }
    f.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize) bytes.size());
}

void addNote(std::vector<Event>& events, double time, int note, float vel, double duration)
{
    const int64_t s = (int64_t) std::llround(time * (double) gSampleRate);
    const int64_t e = (int64_t) std::llround((time + duration) * (double) gSampleRate);
    events.push_back({ s, 0, note, vel });
    events.push_back({ e, 1, note, 0.55f });
}

std::string noteName(int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int octave = note / 12 - 1;
    return std::string(names[note % 12]) + std::to_string(octave);
}
} // namespace

int main(int argc, char* argv[])
{
    std::string output = "corda_demo.wav";
    int sampleRate = 48000;
    bool single = false;
    int note = 60;
    float velocity = 0.8f;
    double duration = 8.0;
    bool allTests = false;
    bool pedalDown = false;
    bool clean = false;
    int voicing = 0;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--note" && i + 1 < argc) { note = std::atoi(argv[++i]); single = true; }
        else if (a == "--vel" && i + 1 < argc) velocity = std::atof(argv[++i]);
        else if (a == "--dur" && i + 1 < argc) duration = std::atof(argv[++i]);
        else if (a == "--sr" && i + 1 < argc) sampleRate = std::atoi(argv[++i]);
        else if (a == "--all") allTests = true;
        else if (a == "--pedal") pedalDown = true;
        else if (a == "--clean") clean = true;
        else if (a == "--voicing" && i + 1 < argc) voicing = std::atoi(argv[++i]);
        else if (a == "--help") { std::puts("usage: corda_render [out.wav] [--note N --vel V --dur S] [--all] [--sr SR]"); return 0; }
        else output = a;
    }

    gSampleRate = sampleRate;
    std::vector<Event> events;

    if (allTests)
    {
        // A register sweep at two velocities, with a short gap between notes.
        double t = 0.4;
        for (int n : { 21, 28, 36, 45, 52, 60, 69, 76, 84, 93, 100, 108 })
        {
            addNote(events, t, n, 0.85f, 1.6);
            t += 2.0;
        }
        for (int n : { 21, 36, 48, 60, 72, 84, 96, 108 })
        {
            addNote(events, t, n, 0.28f, 1.6);
            t += 2.0;
        }
        duration = t + 9.0;
    }
    else if (single)
    {
        events.push_back({ (int64_t) (0.15 * sampleRate), 0, note, velocity });
        events.push_back({ (int64_t) ((0.15 + std::max(0.15, duration * 0.45)) * sampleRate), 1, note, 0.5f });
        duration += 0.4;
    }
    else
    {
        // A short demonstration: single notes, a sustained chord, a pedalled
        // arpeggio, a fast repeated-note figure and a final low/high chord.
        addNote(events, 0.35, 60, 0.82, 2.8);
        addNote(events, 3.35, 67, 0.72, 2.7);
        addNote(events, 6.20, 48, 0.9, 3.4);
        addNote(events, 6.20, 55, 0.72, 3.4);
        addNote(events, 6.20, 64, 0.65, 3.4);
        addNote(events, 9.9, 21, 0.95, 4.0);
        addNote(events, 9.9, 33, 0.75, 4.0);
        addNote(events, 9.9, 45, 0.62, 4.0);
        addNote(events, 9.9, 57, 0.55, 4.0);

        events.push_back({ (int64_t) (16.0 * sampleRate), 2, 0, 1.0f });
        const int arp[] = { 48, 55, 60, 64, 67, 72, 76, 79 };
        double t = 16.1;
        for (int i = 0; i < 20; ++i)
        {
            addNote(events, t, arp[i % 8] + (i >= 8 ? 12 : 0), 0.55f + 0.02f * (i % 4), 1.0);
            t += 0.115;
        }
        events.push_back({ (int64_t) (20.2 * sampleRate), 2, 0, 0.0f });
        addNote(events, 20.3, 36, 0.85, 3.5);
        addNote(events, 20.3, 48, 0.7, 3.5);
        addNote(events, 20.3, 55, 0.62, 3.5);
        events.push_back({ (int64_t) (24.0 * sampleRate), 2, 0, 1.0f });
        const int run[] = { 72, 74, 76, 77, 79, 81, 83, 84 };
        t = 24.2;
        for (int i = 0; i < 8; ++i) { addNote(events, t, run[i], 0.32f + 0.06f * i, 0.7); t += 0.085; }
        events.push_back({ (int64_t) (25.6 * sampleRate), 2, 0, 0.0f });
        addNote(events, 25.6, 24, 1.0, 5.0);
        addNote(events, 25.6, 36, 0.78, 5.0);
        addNote(events, 25.6, 48, 0.7, 5.0);
        addNote(events, 25.6, 60, 0.72, 5.0);
        addNote(events, 25.6, 64, 0.65, 5.0);
        addNote(events, 25.6, 67, 0.62, 5.0);
        addNote(events, 26.1, 84, 0.55, 3.0);
        addNote(events, 26.1, 91, 0.45, 3.0);
        duration = 34.0;
    }

    std::sort(events.begin(), events.end(), [] (const Event& a, const Event& b) {
        if (a.sample != b.sample) return a.sample < b.sample;
        return a.type < b.type;
    });

    const int64_t total = (int64_t) std::llround(duration * sampleRate);
    std::vector<float> left((size_t) total, 0.0f), right((size_t) total, 0.0f);

    corda::PianoEngine engine;
    corda::PianoEngine::Params p;
    p.masterGainDb = 0.0f;
    p.room = 0.25f;
    p.tone = 0.55f;
    p.dynamics = 0.65f;
    p.sympathetic = 0.45f;
    p.mechanical = 0.30f;
    p.stereoWidth = 0.65f;
    p.voicing = voicing;
    if (clean) { p.sympathetic = 0.0f; p.body = 0.0f; p.room = 0.0f; p.mechanical = 0.0f; }
    engine.setParams(p);
    engine.prepare(sampleRate, 512, 2);
    if (pedalDown)
        engine.sustainPedal(1.0f);

    size_t nextEvent = 0;
    const int blockSize = 256;
    for (int64_t pos = 0; pos < total; )
    {
        const int n = (int) std::min<int64_t>(blockSize, total - pos);
        int offset = 0;
        while (nextEvent < events.size() && events[nextEvent].sample < pos + n)
        {
            const int64_t evSample = std::max<int64_t>(events[nextEvent].sample, pos);
            const int local = (int) (evSample - pos);
            if (local > offset)
            {
                engine.render(left.data() + pos + offset, right.data() + pos + offset, local - offset);
                offset = local;
            }
            auto& e = events[nextEvent];
            if (e.type == 0) engine.noteOn(e.note, e.value);
            else if (e.type == 1) engine.noteOff(e.note, e.value);
            else engine.sustainPedal(e.value);
            ++nextEvent;
        }
        if (offset < n)
            engine.render(left.data() + pos + offset, right.data() + pos + offset, n - offset);
        pos += n;
    }

    double sumL = 0.0, sumR = 0.0;
    float peak = 0.0f;
    for (int64_t i = 0; i < total; ++i)
    {
        sumL += (double) left[(size_t) i] * left[(size_t) i];
        sumR += (double) right[(size_t) i] * right[(size_t) i];
        peak = std::max(peak, std::max(std::abs(left[(size_t) i]), std::abs(right[(size_t) i])));
    }
    const double rms = std::sqrt(0.5 * (sumL + sumR) / (double) std::max<int64_t>(1, total));

    std::printf("wrote %s  %.2f s  %.0f Hz  peak %.2f dBFS  rms %.2f dBFS\n",
                output.c_str(), duration, (double) sampleRate,
                20.0 * std::log10(std::max(1e-9f, peak)),
                20.0 * std::log10(std::max(1e-12, rms)));

    writeWav24(output, left, right, sampleRate);
    return 0;
}
