#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

struct Theme
{
    const char* name;
    juce::Colour bg, panel, padBg, grid, accent, accent2, text, dimText;
};
const Theme& themeAt (int index);
int numThemes();
juce::Colour categoryColour (ts::Category c);

class TouchstormLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void setTheme (const Theme& t);
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return juce::Font (juce::FontOptions (juce::jmin (14.0f, h * 0.45f), juce::Font::bold)); }
    const Theme* theme = nullptr;
};

// The big touch pad
class XYPad : public juce::Component
{
public:
    explicit XYPad (TouchstormProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void tick();   // called by the editor's timer
    const Theme* theme = nullptr;

private:
    void sendPosition (juce::Point<float>);
    bool isActive() const;
    juce::Point<float> shownPos() const;
    float localX = 0.5f, localY = 0.5f;
    TouchstormProcessor& proc;
    struct TrailPoint { float x, y, life; };
    std::vector<TrailPoint> trail;
    bool dragging = false;
    float pulse = 0;
};

class TouchstormEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TouchstormEditor (TouchstormProcessor&);
    ~TouchstormEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyTheme();
    void stepProgram (int delta);
    void tapTempo();
    void bankMenu (int bank);

    TouchstormProcessor& proc;
    TouchstormLookAndFeel lnf;
    XYPad pad;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    Knob depth, input, output, loopLevel;
    void setupKnob (Knob&, const juce::String& id, const juce::String& name);

    juce::TextButton prevButton { "<" }, nextButton { ">" }, tapButton { "TAP" };
    juce::ComboBox programBox, themeBox, keyBox, scaleBox, lengthBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttach, scaleAttach, lengthAttach;
    juce::TextButton categoryButtons[(int) ts::Category::Count];

    juce::TextButton holdButton { "HOLD" }, motionButton { "MOTION" }, syncButton { "SYNC" }, resampleButton { "RESAMPLE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> holdAttach, motionAttach, syncAttach, resampleAttach;

    juce::TextButton bankButtons[4];
    juce::TextButton memoryButtons[TouchstormProcessor::numMemories];
    juce::TextButton writeButton { "WRITE" };

    juce::Rectangle<int> lcdArea, leftArea, rightArea, padArea, meterArea, bpmArea;
    std::vector<double> taps;
    float inMeter = 0, outMeter = 0;
    int lastProgram = -1, lastTheme = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TouchstormEditor)
};
