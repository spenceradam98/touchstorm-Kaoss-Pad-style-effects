#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"

namespace ids
{
    inline const juce::String program = "program", padX = "padX", padY = "padY", touch = "touch", hold = "hold",
                              depth = "depth", bpm = "bpm", syncHost = "syncHost", input = "input", output = "output",
                              key = "key", scale = "scale", motion = "motion", sampleLevel = "sampleLevel",
                              bankLength = "bankLength", resample = "resample";
}

class TouchstormProcessor : public juce::AudioProcessor
{
public:
    TouchstormProcessor();
    ~TouchstormProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 15.0; }

    // Host program list = the effect programs
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- UI hooks (message thread)
    void setParam (const juce::String& id, float plainValue);
    float getParam (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    static constexpr int numMemories = 8;
    void storeMemory (int slot);
    void recallMemory (int slot);
    bool memoryUsed (int slot) const;
    juce::String memoryLabel (int slot) const;
    int getTheme() const { return (int) apvts.state.getProperty ("theme", 0); }
    void setTheme (int t) { apvts.state.setProperty ("theme", t, nullptr); }
    float hostBpm() const { return lastHostBpm.load(); }
    bool hostIsPlaying() const { return lastHostPlaying.load(); }

    ts::Engine engine;
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    ts::Params readParams();
    void handleMidi (const juce::MidiBuffer&);
    void setParamFromAudio (const juce::String& id, float plainValue);

    juce::AudioBuffer<float> sideChain;
    std::atomic<float> lastHostBpm { 120.0f };
    std::atomic<bool> lastHostPlaying { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchstormProcessor)
};
