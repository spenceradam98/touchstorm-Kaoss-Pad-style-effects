#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const int kBankBeats[] = { 1, 2, 4, 8, 16 };
}

juce::AudioProcessorValueTreeState::ParameterLayout TouchstormProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    int count = 0;
    auto* progs = ts::programs (count);
    StringArray progNames;
    for (int i = 0; i < count; ++i) progNames.add (String (i + 1).paddedLeft ('0', 2) + " " + progs[i].name);

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::program, 1 }, "Program", progNames, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::padX, 1 }, "Pad X", NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::padY, 1 }, "Pad Y", NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::touch, 1 }, "Pad Touch", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::hold, 1 }, "Hold", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::depth, 1 }, "FX Depth", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::bpm, 1 }, "BPM", NormalisableRange<float> (20.0f, 300.0f, 0.1f), 120.0f));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::syncHost, 1 }, "Sync to Host", true));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::input, 1 }, "Input", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::output, 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::key, 1 }, "Key",
                                                         StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::scale, 1 }, "Scale",
                                                         StringArray { "Minor Pentatonic", "Major", "Minor", "Chromatic" }, 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::motion, 1 }, "Pad Motion", false));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids::sampleLevel, 1 }, "Loop Level", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 80.0f));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::bankLength, 1 }, "Loop Length",
                                                         StringArray { "1 beat", "2 beats", "4 beats", "8 beats", "16 beats" }, 2));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::resample, 1 }, "Resample", false));
    return { p.begin(), p.end() };
}

TouchstormProcessor::TouchstormProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Touchstorm", createLayout())
{
}

bool TouchstormProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    const bool okOut = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    const bool okIn = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo() || in.isDisabled();
    return okOut && okIn;
}

void TouchstormProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate, samplesPerBlock);
}

ts::Params TouchstormProcessor::readParams()
{
    auto v = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    ts::Params p;
    p.program = (int) std::lround (v (ids::program));
    p.x = v (ids::padX);
    p.y = v (ids::padY);
    p.touch = v (ids::touch) > 0.5f;
    p.hold = v (ids::hold) > 0.5f;
    p.depth = v (ids::depth) / 100.0f;
    p.inputDb = v (ids::input);
    p.outputDb = v (ids::output);
    p.key = (int) std::lround (v (ids::key));
    p.scale = (ts::Scale) juce::jlimit (0, (int) ts::Scale::Count - 1, (int) std::lround (v (ids::scale)));
    p.motion = v (ids::motion) > 0.5f;
    p.sampleLevel = v (ids::sampleLevel) / 100.0f;
    p.bankBeats = kBankBeats[juce::jlimit (0, 4, (int) std::lround (v (ids::bankLength)))];
    p.resample = v (ids::resample) > 0.5f;
    p.bpm = v (ids::bpm);

    const bool sync = v (ids::syncHost) > 0.5f;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm(); b.hasValue() && sync) { p.bpm = (float) *b; }
            if (sync && pos->getIsPlaying())
                if (auto q = pos->getPpqPosition(); q.hasValue()) { playing = true; p.ppq = *q; }
        }
    p.hostPlaying = playing;
    lastHostBpm.store (p.bpm);
    lastHostPlaying.store (playing);
    return p;
}

void TouchstormProcessor::setParamFromAudio (const juce::String& id, float plainValue)
{
    if (auto* prm = apvts.getParameter (id))
    {
        const float norm = prm->convertTo0to1 (plainValue);
        if (std::abs (prm->getValue() - norm) > 1e-6f) prm->setValueNotifyingHost (norm);
    }
}

// MIDI map (fixed for v0.1): CC12 = pad X, CC13 = pad Y, CC92 = touch, CC93 = FX depth,
// CC94 = hold, CC95 = pad motion, program change = effect program.
void TouchstormProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isController())
        {
            const float v = (float) m.getControllerValue() / 127.0f;
            switch (m.getControllerNumber())
            {
                case 12: setParamFromAudio (ids::padX, v); break;
                case 13: setParamFromAudio (ids::padY, v); break;
                case 92: setParamFromAudio (ids::touch, v >= 0.5f ? 1.0f : 0.0f); break;
                case 93: setParamFromAudio (ids::depth, v * 100.0f); break;
                case 94: setParamFromAudio (ids::hold, v >= 0.5f ? 1.0f : 0.0f); break;
                case 95: setParamFromAudio (ids::motion, v >= 0.5f ? 1.0f : 0.0f); break;
                default: break;
            }
        }
        else if (m.isProgramChange())
        {
            setParamFromAudio (ids::program, (float) juce::jmin (m.getProgramChangeNumber(), getNumPrograms() - 1));
        }
    }
}

void TouchstormProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    handleMidi (midi);
    midi.clear();

    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    if (outCh == 0) return;
    if (inCh == 0) buffer.clear();
    else if (inCh == 1 && outCh > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    for (int c = juce::jmax (inCh, 2); c < outCh; ++c) buffer.clear (c, 0, n);

    engine.setParams (readParams());
    float* l = buffer.getWritePointer (0);
    float* r = outCh > 1 ? buffer.getWritePointer (1) : nullptr;
    if (r != nullptr) engine.process (l, r, n);
    else
    {
        // mono out: run in stereo on a scratch copy
        juce::HeapBlock<float> tmp ((size_t) n);
        std::copy (l, l + n, tmp.get());
        engine.process (l, tmp.get(), n);
        for (int i = 0; i < n; ++i) l[i] = 0.5f * (l[i] + tmp[i]);
    }
}

int TouchstormProcessor::getNumPrograms() { int n = 0; ts::programs (n); return n; }
int TouchstormProcessor::getCurrentProgram() { return (int) std::lround (getParam (ids::program)); }
const juce::String TouchstormProcessor::getProgramName (int index)
{
    int n = 0; auto* ps = ts::programs (n);
    return juce::isPositiveAndBelow (index, n) ? juce::String (ps[index].name) : juce::String();
}
void TouchstormProcessor::setCurrentProgram (int index)
{
    if (juce::isPositiveAndBelow (index, getNumPrograms())) setParam (ids::program, (float) index);
}

void TouchstormProcessor::setParam (const juce::String& id, float plainValue)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
        prm->endChangeGesture();
    }
}

// ---- Program memories (like the hardware's memory buttons): program, depth, pad position, hold
static juce::ValueTree memoryTree (juce::ValueTree& state)
{
    auto t = state.getChildWithName ("Memories");
    if (! t.isValid()) { t = juce::ValueTree ("Memories"); state.appendChild (t, nullptr); }
    return t;
}

void TouchstormProcessor::storeMemory (int slot)
{
    auto mem = memoryTree (apvts.state);
    auto old = mem.getChildWithProperty ("slot", slot);
    if (old.isValid()) mem.removeChild (old, nullptr);
    juce::ValueTree m ("Memory");
    m.setProperty ("slot", slot, nullptr);
    for (auto id : { ids::program, ids::depth, ids::padX, ids::padY, ids::hold, ids::key, ids::scale })
        m.setProperty (juce::Identifier (id), getParam (id), nullptr);
    mem.appendChild (m, nullptr);
}

void TouchstormProcessor::recallMemory (int slot)
{
    auto mem = memoryTree (apvts.state);
    auto m = mem.getChildWithProperty ("slot", slot);
    if (! m.isValid()) return;
    for (auto id : { ids::program, ids::depth, ids::padX, ids::padY, ids::hold, ids::key, ids::scale })
        if (m.hasProperty (juce::Identifier (id))) setParam (id, (float) m.getProperty (juce::Identifier (id)));
}

bool TouchstormProcessor::memoryUsed (int slot) const
{
    auto t = apvts.state.getChildWithName ("Memories");
    return t.isValid() && t.getChildWithProperty ("slot", slot).isValid();
}

juce::String TouchstormProcessor::memoryLabel (int slot) const
{
    auto t = apvts.state.getChildWithName ("Memories");
    if (! t.isValid()) return {};
    auto m = t.getChildWithProperty ("slot", slot);
    if (! m.isValid()) return {};
    int n = 0; auto* ps = ts::programs (n);
    const int p = juce::jlimit (0, n - 1, (int) m.getProperty (juce::Identifier (ids::program)));
    return ps[p].name;
}

void TouchstormProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // never restore with a finger on the pad
    state.setProperty ("version", 1, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void TouchstormProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            setParam (ids::touch, 0.0f);
        }
}

juce::AudioProcessorEditor* TouchstormProcessor::createEditor() { return new TouchstormEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TouchstormProcessor(); }
