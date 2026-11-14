#include "PluginEditor.h"

using namespace juce;

// ------------------------------------------------------------------ themes
namespace
{
const Theme kThemes[] = {
    { "Midnight", Colour (0xff0d1117), Colour (0xff161c26), Colour (0xff070a10), Colour (0x2250c8ff),
      Colour (0xff35d6ff), Colour (0xffff4fd8), Colour (0xffe6edf5), Colour (0xff7d8a9c) },
    { "Magma",    Colour (0xff120808), Colour (0xff1e0f0d), Colour (0xff0a0303), Colour (0x26ff6a2a),
      Colour (0xffff6a2a), Colour (0xffffd23f), Colour (0xfff7e6dc), Colour (0xff9c7f75) },
    { "Mint",     Colour (0xff0b1411), Colour (0xff12201b), Colour (0xff050c09), Colour (0x2242ffb0),
      Colour (0xff42ffb0), Colour (0xff9d7bff), Colour (0xffe2f7ee), Colour (0xff7a9a8d) },
    { "Daylight", Colour (0xffe9ecf1), Colour (0xfff7f8fa), Colour (0xffdfe4ec), Colour (0x30304060),
      Colour (0xff2563eb), Colour (0xffe11d74), Colour (0xff1a2230), Colour (0xff6a7487) },
    { "Vapor",    Colour (0xff1a1030), Colour (0xff24183f), Colour (0xff120a24), Colour (0x2aff8ae2),
      Colour (0xffff8ae2), Colour (0xff5ef2ff), Colour (0xfff4e9ff), Colour (0xff9a8ab8) },
};
}

const Theme& themeAt (int i) { return kThemes[jlimit (0, numThemes() - 1, i)]; }
int numThemes() { return (int) (sizeof (kThemes) / sizeof (kThemes[0])); }

Colour categoryColour (ts::Category c)
{
    static const uint32 cols[] = { 0xff3b82f6, 0xff22c55e, 0xfff59e0b, 0xffa855f7, 0xffec4899, 0xffef4444, 0xff14b8a6, 0xffeab308 };
    return Colour (cols[jlimit (0, 7, (int) c)]);
}

// ------------------------------------------------------------------ look and feel
void TouchstormLookAndFeel::setTheme (const Theme& t)
{
    theme = &t;
    setColour (ResizableWindow::backgroundColourId, t.bg);
    setColour (Label::textColourId, t.text);
    setColour (ComboBox::backgroundColourId, t.padBg);
    setColour (ComboBox::textColourId, t.text);
    setColour (ComboBox::outlineColourId, t.grid.withAlpha (0.6f));
    setColour (ComboBox::arrowColourId, t.accent);
    setColour (PopupMenu::backgroundColourId, t.panel);
    setColour (PopupMenu::textColourId, t.text);
    setColour (PopupMenu::highlightedBackgroundColourId, t.accent.withAlpha (0.3f));
    setColour (PopupMenu::highlightedTextColourId, t.text);
    setColour (PopupMenu::headerTextColourId, t.accent);
    setColour (TextButton::textColourOffId, t.text);
    setColour (TextButton::textColourOnId, t.bg);
    setColour (Slider::textBoxTextColourId, t.text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
}

void TouchstormLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider&)
{
    const auto& t = *theme;
    auto r = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float size = jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (size, size);
    const auto c = r.getCentre();
    const float rad = size * 0.5f;
    Path track, val;
    track.addCentredArc (c.x, c.y, rad - 3, rad - 3, 0, start, end, true);
    g.setColour (t.grid.withAlpha (1.0f).withMultipliedAlpha (0.5f));
    g.strokePath (track, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    const float ang = start + pos * (end - start);
    val.addCentredArc (c.x, c.y, rad - 3, rad - 3, 0, start, ang, true);
    g.setColour (t.accent);
    g.strokePath (val, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (t.bg.getPerceivedBrightness() > 0.5f ? t.padBg.darker (0.05f) : t.panel.brighter (0.15f));
    g.fillEllipse (r.reduced (size * 0.2f));
    Point<float> tip (c.x + std::sin (ang) * (rad * 0.55f), c.y - std::cos (ang) * (rad * 0.55f));
    g.setColour (t.text);
    g.drawLine (Line<float> (c, tip), 2.5f);
}

void TouchstormLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& base, bool hi, bool down)
{
    const auto& t = *theme;
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool on = b.getToggleState();
    Colour fill = base == Colour() || base == findColour (TextButton::buttonColourId) ? t.panel.brighter (0.08f) : base;
    if (on) fill = b.findColour (TextButton::buttonOnColourId) == findColour (TextButton::buttonOnColourId) ? t.accent : b.findColour (TextButton::buttonOnColourId);
    if (hi) fill = fill.brighter (0.12f);
    if (down) fill = fill.darker (0.15f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 6.0f);
    if (on)
    {
        g.setColour (fill.withAlpha (0.35f));
        g.drawRoundedRectangle (r.expanded (1.0f), 7.0f, 3.0f);
    }
    g.setColour (t.grid.withAlpha (0.8f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void TouchstormLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool hi, bool down)
{
    drawButtonBackground (g, b, theme->panel, hi, down);
    g.setColour (b.getToggleState() ? theme->bg : theme->text);
    g.setFont (FontOptions (13.0f, Font::bold));
    g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred);
}

// ------------------------------------------------------------------ XY pad
XYPad::XYPad (TouchstormProcessor& p) : proc (p)
{
    setRepaintsOnMouseActivity (false);
    setMouseCursor (MouseCursor::CrosshairCursor);
}

void XYPad::sendPosition (Point<float> pt)
{
    auto b = getLocalBounds().toFloat().reduced (6.0f);
    const float x = jlimit (0.0f, 1.0f, (pt.x - b.getX()) / b.getWidth());
    const float y = jlimit (0.0f, 1.0f, 1.0f - (pt.y - b.getY()) / b.getHeight());
    for (auto [id, v] : { std::pair<String, float> { ids::padX, x }, { ids::padY, y } })
        if (auto* prm = proc.apvts.getParameter (id)) prm->setValueNotifyingHost (v);
    localX = x; localY = y;
}

// While the mouse is down, draw the mouse position (the engine may not be running, e.g. a stopped host)
bool XYPad::isActive() const { return dragging || proc.engine.displayActive(); }
Point<float> XYPad::shownPos() const
{
    return dragging ? Point<float> (localX, localY) : Point<float> (proc.engine.displayX(), proc.engine.displayY());
}

void XYPad::mouseDown (const MouseEvent& e)
{
    dragging = true;
    for (auto id : { ids::padX, ids::padY, ids::touch })
        if (auto* prm = proc.apvts.getParameter (id)) prm->beginChangeGesture();
    sendPosition (e.position);
    if (auto* prm = proc.apvts.getParameter (ids::touch)) prm->setValueNotifyingHost (1.0f);
}

void XYPad::mouseDrag (const MouseEvent& e) { sendPosition (e.position); }

void XYPad::mouseUp (const MouseEvent&)
{
    dragging = false;
    if (auto* prm = proc.apvts.getParameter (ids::touch)) prm->setValueNotifyingHost (0.0f);
    for (auto id : { ids::padX, ids::padY, ids::touch })
        if (auto* prm = proc.apvts.getParameter (id)) prm->endChangeGesture();
}

void XYPad::tick()
{
    if (isActive()) { const auto sp = shownPos(); trail.push_back ({ sp.x, sp.y, 1.0f }); }
    for (auto& t : trail) t.life -= 0.045f;
    trail.erase (std::remove_if (trail.begin(), trail.end(), [] (const TrailPoint& t) { return t.life <= 0; }), trail.end());
    pulse += 0.08f;
    repaint();
}

void XYPad::paint (Graphics& g)
{
    const auto& t = *theme;
    auto outer = getLocalBounds().toFloat();
    g.setColour (t.padBg);
    g.fillRoundedRectangle (outer, 14.0f);
    auto b = outer.reduced (6.0f);

    int n = 0;
    auto* progs = ts::programs (n);
    const int pi = jlimit (0, n - 1, (int) std::lround (proc.getParam (ids::program)));
    const auto& prog = progs[pi];
    const Colour cat = categoryColour (prog.category);

    // grid
    g.setColour (t.grid);
    for (int i = 1; i < 8; ++i)
    {
        const float gx = b.getX() + b.getWidth() * (float) i / 8.0f, gy = b.getY() + b.getHeight() * (float) i / 8.0f;
        g.drawLine (gx, b.getY(), gx, b.getBottom(), i == 4 ? 1.5f : 0.7f);
        g.drawLine (b.getX(), gy, b.getRight(), gy, i == 4 ? 1.5f : 0.7f);
    }

    // axis labels
    g.setColour (t.dimText);
    g.setFont (FontOptions (13.0f, Font::bold));
    g.drawText (String ("X: ") + prog.xLabel + "  >", b.removeFromBottom (0).withHeight (22).translated (10, -26).withWidth (260), Justification::left);
    {
        Graphics::ScopedSaveState s (g);
        const float px = b.getX() + 20, py = b.getBottom() - 40;
        g.addTransform (AffineTransform::rotation (-MathConstants<float>::halfPi, px, py));
        g.drawText (String ("Y: ") + prog.yLabel + "  >", Rectangle<float> (px, py - 14, 260, 20), Justification::left);
    }

    const bool active = isActive();
    auto toPx = [&b] (float x, float y) { return Point<float> (b.getX() + x * b.getWidth(), b.getY() + (1.0f - y) * b.getHeight()); };

    // trail
    for (size_t i = 0; i < trail.size(); ++i)
    {
        const auto& tp = trail[i];
        const auto p = toPx (tp.x, tp.y);
        const float r = 4.0f + 18.0f * tp.life;
        g.setColour (cat.interpolatedWith (t.accent2, 1.0f - tp.life).withAlpha (0.18f * tp.life));
        g.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
    }

    const auto cur = toPx (shownPos().x, shownPos().y);
    if (active)
    {
        // glow + crosshair
        const float glow = 46.0f + 6.0f * std::sin (pulse);
        ColourGradient grad (t.accent.withAlpha (0.55f), cur.x, cur.y, t.accent.withAlpha (0.0f), cur.x + glow, cur.y, true);
        g.setGradientFill (grad);
        g.fillEllipse (cur.x - glow, cur.y - glow, glow * 2, glow * 2);
        g.setColour (t.accent.withAlpha (0.5f));
        g.drawLine (b.getX(), cur.y, b.getRight(), cur.y, 1.0f);
        g.drawLine (cur.x, b.getY(), cur.x, b.getBottom(), 1.0f);
        g.setColour (Colours::white.withAlpha (0.9f));
        g.fillEllipse (cur.x - 7, cur.y - 7, 14, 14);
    }
    else
    {
        g.setColour (t.dimText.withAlpha (0.6f));
        g.drawEllipse (cur.x - 8, cur.y - 8, 16, 16, 1.5f);
    }

    if (proc.engine.motionPlaying())
    {
        g.setColour (t.accent2);
        g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText ("MOTION PLAYBACK", b.reduced (10).removeFromTop (18), Justification::right);
    }
    if (! active && trail.empty())
    {
        g.setColour (t.dimText.withAlpha (0.5f));
        g.setFont (FontOptions (15.0f));
        g.drawText ("touch the pad", b.withTrimmedTop (60), Justification::centred);
    }

    g.setColour (cat.withAlpha (active ? 0.9f : 0.35f));
    g.drawRoundedRectangle (outer.reduced (1.0f), 14.0f, active ? 2.5f : 1.5f);
}

// ------------------------------------------------------------------ editor
TouchstormEditor::TouchstormEditor (TouchstormProcessor& p) : AudioProcessorEditor (&p), proc (p), pad (p)
{
    lnf.setTheme (themeAt (proc.getTheme()));
    setLookAndFeel (&lnf);
    pad.theme = lnf.theme;
    addAndMakeVisible (pad);

    setupKnob (depth, ids::depth, "FX DEPTH");
    setupKnob (input, ids::input, "INPUT");
    setupKnob (output, ids::output, "OUTPUT");
    setupKnob (loopLevel, ids::sampleLevel, "LOOP LEVEL");

    // program list, grouped by category
    int n = 0;
    auto* progs = ts::programs (n);
    for (int c = 0; c < (int) ts::Category::Count; ++c)
    {
        programBox.addSectionHeading (ts::categoryName ((ts::Category) c));
        for (int i = 0; i < n; ++i)
            if ((int) progs[i].category == c) programBox.addItem (String (i + 1).paddedLeft ('0', 2) + "  " + progs[i].name, i + 1);
    }
    programBox.onChange = [this] { if (programBox.getSelectedId() > 0) proc.setParam (ids::program, (float) (programBox.getSelectedId() - 1)); };
    addAndMakeVisible (programBox);

    prevButton.onClick = [this] { stepProgram (-1); };
    nextButton.onClick = [this] { stepProgram (1); };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);

    for (int c = 0; c < (int) ts::Category::Count; ++c)
    {
        auto& b = categoryButtons[c];
        static const char* shortNames[] = { "FILTER", "MOD", "DELAY", "REVERB", "PITCH", "GRAIN", "LO-FI", "SYNTH" };
        b.setButtonText (shortNames[c]);
        b.setTooltip (ts::categoryName ((ts::Category) c));
        b.setColour (TextButton::buttonOnColourId, categoryColour ((ts::Category) c));
        b.setClickingTogglesState (false);
        b.onClick = [this, c] { proc.setParam (ids::program, (float) ts::firstProgramInCategory ((ts::Category) c)); };
        addAndMakeVisible (b);
    }

    for (int i = 0; i < numThemes(); ++i) themeBox.addItem (themeAt (i).name, i + 1);
    themeBox.setSelectedId (proc.getTheme() + 1, dontSendNotification);
    themeBox.onChange = [this] { proc.setTheme (themeBox.getSelectedId() - 1); applyTheme(); };
    addAndMakeVisible (themeBox);

    auto setupToggle = [this] (TextButton& b, const String& id, std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment>& a)
    {
        b.setClickingTogglesState (true);
        addAndMakeVisible (b);
        a = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, b);
    };
    setupToggle (holdButton, ids::hold, holdAttach);
    setupToggle (motionButton, ids::motion, motionAttach);
    setupToggle (syncButton, ids::syncHost, syncAttach);
    setupToggle (resampleButton, ids::resample, resampleAttach);
    holdButton.setTooltip ("Keep the effect on after you let go of the pad");
    motionButton.setTooltip ("Record your pad movement while touching, then loop it when you let go");
    resampleButton.setTooltip ("Loop banks record the output (with the effect) instead of the input");

    tapButton.onClick = [this] { tapTempo(); };
    addAndMakeVisible (tapButton);

    auto setupBox = [this] (ComboBox& box, const String& id, std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment>& a)
    {
        if (auto* c = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter (id)))
            box.addItemList (c->choices, 1);
        addAndMakeVisible (box);
        a = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, box);
    };
    setupBox (keyBox, ids::key, keyAttach);
    setupBox (scaleBox, ids::scale, scaleAttach);
    setupBox (lengthBox, ids::bankLength, lengthAttach);

    for (int i = 0; i < 4; ++i)
    {
        auto& b = bankButtons[i];
        b.setButtonText (String::charToString ((juce_wchar) ('A' + i)));
        b.setTooltip ("Click: record / play / stop. Right-click: clear or reverse");
        b.onClick = [this, i]
        {
            if (ModifierKeys::getCurrentModifiers().isPopupMenu()) bankMenu (i);
            else proc.engine.bankPress (i);
        };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < TouchstormProcessor::numMemories; ++i)
    {
        auto& b = memoryButtons[i];
        b.setButtonText (String (i + 1));
        b.onClick = [this, i]
        {
            if (writeButton.getToggleState()) { proc.storeMemory (i); writeButton.setToggleState (false, dontSendNotification); }
            else proc.recallMemory (i);
        };
        addAndMakeVisible (b);
    }
    writeButton.setClickingTogglesState (true);
    writeButton.setColour (TextButton::buttonOnColourId, Colour (0xffef4444));
    writeButton.setTooltip ("Arm, then click a memory slot to store the current program, depth, pad position and hold");
    addAndMakeVisible (writeButton);

    setSize (980, 640);
    applyTheme();
    startTimerHz (40);
}

TouchstormEditor::~TouchstormEditor() { setLookAndFeel (nullptr); }

void TouchstormEditor::setupKnob (Knob& k, const String& id, const String& name)
{
    k.slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 70, 16);
    k.label.setText (name, dontSendNotification);
    k.label.setJustificationType (Justification::centred);
    k.label.setFont (FontOptions (11.0f, Font::bold));
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.attach = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
}

void TouchstormEditor::applyTheme()
{
    lnf.setTheme (themeAt (proc.getTheme()));
    pad.theme = lnf.theme;
    for (auto* k : { &depth, &input, &output, &loopLevel }) k->label.setColour (Label::textColourId, lnf.theme->dimText);
    lastTheme = proc.getTheme();
    sendLookAndFeelChange();
    repaint();
}

void TouchstormEditor::stepProgram (int delta)
{
    const int n = proc.getNumPrograms();
    const int cur = proc.getCurrentProgram();
    proc.setParam (ids::program, (float) ((cur + delta + n) % n));
}

void TouchstormEditor::tapTempo()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    if (! taps.empty() && now - taps.back() > 2.0) taps.clear();
    taps.push_back (now);
    if (taps.size() > 5) taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        proc.setParam (ids::bpm, (float) jlimit (20.0, 300.0, 60.0 / avg));
        proc.setParam (ids::syncHost, 0.0f);   // a tapped tempo overrides the host
    }
}

void TouchstormEditor::bankMenu (int bank)
{
    PopupMenu m;
    m.addItem (1, "Clear");
    m.addItem (2, "Reverse", true, proc.engine.bankReversed (bank));
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&bankButtons[bank]), [this, bank] (int r)
    {
        if (r == 1) proc.engine.bankClear (bank);
        if (r == 2) proc.engine.bankToggleReverse (bank);
    });
}

void TouchstormEditor::timerCallback()
{
    if (proc.getTheme() != lastTheme) { themeBox.setSelectedId (proc.getTheme() + 1, dontSendNotification); applyTheme(); }
    const int prog = proc.getCurrentProgram();
    if (prog != lastProgram)
    {
        lastProgram = prog;
        programBox.setSelectedId (prog + 1, dontSendNotification);
        int n = 0;
        auto* progs = ts::programs (n);
        for (int c = 0; c < (int) ts::Category::Count; ++c)
            categoryButtons[c].setToggleState ((int) progs[jlimit (0, n - 1, prog)].category == c, dontSendNotification);
    }

    for (int i = 0; i < 4; ++i)
    {
        const auto st = proc.engine.bankState (i);
        Colour c = lnf.theme->panel.brighter (0.08f);
        if (st == ts::BankState::Armed) c = Colour (0xfff59e0b).withAlpha ((Time::getMillisecondCounter() / 250) % 2 ? 1.0f : 0.4f);
        if (st == ts::BankState::Recording) c = Colour (0xffef4444);
        if (st == ts::BankState::Playing) c = Colour (0xff22c55e);
        if (st == ts::BankState::Stopped) c = Colour (0xff22c55e).withAlpha (0.35f);
        bankButtons[i].setColour (TextButton::buttonColourId, c);
        String txt = String::charToString ((juce_wchar) ('A' + i));
        if (proc.engine.bankReversed (i)) txt << " <";
        bankButtons[i].setButtonText (txt);
    }
    for (int i = 0; i < TouchstormProcessor::numMemories; ++i)
    {
        memoryButtons[i].setColour (TextButton::buttonColourId, proc.memoryUsed (i) ? lnf.theme->accent.withAlpha (0.35f) : lnf.theme->panel.brighter (0.08f));
        memoryButtons[i].setTooltip (proc.memoryUsed (i) ? proc.memoryLabel (i) : "empty");
    }

    inMeter = jmax (proc.engine.inputPeak(), inMeter * 0.85f);
    outMeter = jmax (proc.engine.outputPeak(), outMeter * 0.85f);
    pad.tick();
    repaint (lcdArea);
    repaint (meterArea);
    repaint (bpmArea);
}

void TouchstormEditor::paint (Graphics& g)
{
    const auto& t = *lnf.theme;
    g.fillAll (t.bg);

    // logo
    g.setColour (t.text);
    g.setFont (FontOptions (26.0f, Font::bold));
    g.drawText ("TOUCHSTORM", 20, 14, 200, 32, Justification::left);
    g.setColour (t.accent);
    g.setFont (FontOptions (11.0f));
    g.drawText ("XY PAD EFFECTS & LOOPS", 21, 42, 200, 14, Justification::left);

    // side panels
    g.setColour (t.panel);
    g.fillRoundedRectangle (leftArea.toFloat(), 12.0f);
    g.fillRoundedRectangle (rightArea.toFloat(), 12.0f);

    // LCD
    int n = 0;
    auto* progs = ts::programs (n);
    const int pi = jlimit (0, n - 1, proc.getCurrentProgram());
    const auto& prog = progs[pi];
    auto lcd = lcdArea.toFloat();
    g.setColour (t.padBg);
    g.fillRoundedRectangle (lcd, 8.0f);
    g.setColour (categoryColour (prog.category));
    g.fillRoundedRectangle (lcd.withWidth (6.0f), 3.0f);
    g.setColour (t.accent);
    g.setFont (FontOptions (Font::getDefaultMonospacedFontName(), 30.0f, Font::bold));
    g.drawText (String (pi + 1).paddedLeft ('0', 2), lcd.withWidth (70).translated (14, 0), Justification::centredLeft);
    g.setColour (t.text);
    g.setFont (FontOptions (18.0f, Font::bold));
    g.drawText (prog.name, lcd.withTrimmedLeft (80).withTrimmedTop (8).withHeight (24), Justification::centredLeft);
    g.setColour (t.dimText);
    g.setFont (FontOptions (11.0f, Font::bold));
    g.drawText (String (ts::categoryName (prog.category)).toUpperCase() + "   X " + prog.xLabel + "  /  Y " + prog.yLabel,
                lcd.withTrimmedLeft (80).withTrimmedTop (34).withHeight (14), Justification::centredLeft);

    // section labels
    g.setColour (t.dimText);
    g.setFont (FontOptions (11.0f, Font::bold));
    auto label = [&g] (const String& s, int x, int y, int w) { g.drawText (s, x, y, w, 14, Justification::left); };
    label ("TEMPO", leftArea.getX() + 12, bpmArea.getY() - 16, 100);
    label ("SYNTH KEY / SCALE", leftArea.getX() + 12, keyBox.getY() - 16, 160);
    label ("PAD", rightArea.getX() + 12, holdButton.getY() - 16, 100);
    label ("LOOP BANKS", rightArea.getX() + 12, bankButtons[0].getY() - 16, 120);
    label ("MEMORY", rightArea.getX() + 12, memoryButtons[0].getY() - 16, 120);

    // BPM display
    g.setColour (t.padBg);
    g.fillRoundedRectangle (bpmArea.toFloat(), 6.0f);
    g.setColour (t.text);
    g.setFont (FontOptions (Font::getDefaultMonospacedFontName(), 20.0f, Font::bold));
    g.drawText (String (proc.hostBpm(), 1), bpmArea.withTrimmedRight (40), Justification::centredRight);
    g.setColour (proc.hostIsPlaying() ? t.accent : t.dimText);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText ("BPM", bpmArea.withTrimmedLeft (bpmArea.getWidth() - 36), Justification::centred);

    // meters
    auto m = meterArea.toFloat();
    auto drawMeter = [&] (Rectangle<float> r, float v, const String& name)
    {
        g.setColour (t.padBg);
        g.fillRoundedRectangle (r, 3.0f);
        const float db = Decibels::gainToDecibels (v, -60.0f);
        const float frac = jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        g.setColour (v > 0.98f ? Colour (0xffef4444) : t.accent);
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * frac), 3.0f);
        g.setColour (t.dimText);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText (name, r.translated (0, -13).withHeight (12), Justification::left);
    };
    drawMeter (m.removeFromTop (8).translated (0, 13), inMeter, "IN");
    m.removeFromTop (22);
    drawMeter (m.removeFromTop (8).translated (0, 13), outMeter, "OUT");
}

void TouchstormEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto header = r.removeFromTop (58);
    header.removeFromLeft (210);
    themeBox.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 28));
    header.removeFromRight (12);
    prevButton.setBounds (header.removeFromLeft (36).withSizeKeepingCentre (36, 36));
    header.removeFromLeft (6);
    nextButton.setBounds (header.removeFromRight (36).withSizeKeepingCentre (36, 36));
    header.removeFromRight (6);
    auto boxArea = header.removeFromRight (180);
    programBox.setBounds (boxArea.withSizeKeepingCentre (180, 28));
    header.removeFromRight (8);
    lcdArea = header.reduced (0, 2);

    r.removeFromTop (8);
    auto cats = r.removeFromTop (30);
    r.removeFromTop (10);

    leftArea = r.removeFromLeft (180);
    r.removeFromLeft (14);
    rightArea = r.removeFromRight (190);
    r.removeFromRight (14);
    padArea = r;
    pad.setBounds (padArea);

    {
        auto c = cats.withTrimmedLeft (leftArea.getWidth() + 14).withTrimmedRight (rightArea.getWidth() + 14);
        const int w = c.getWidth() / (int) ts::Category::Count;
        for (int i = 0; i < (int) ts::Category::Count; ++i) categoryButtons[i].setBounds (c.removeFromLeft (w).reduced (2, 0));
    }

    // left column
    auto l = leftArea.reduced (12);
    auto big = l.removeFromTop (130);
    depth.label.setBounds (big.removeFromTop (16));
    depth.slider.setBounds (big);
    l.removeFromTop (8);
    auto row = l.removeFromTop (96);
    auto half = row.removeFromLeft (row.getWidth() / 2);
    input.label.setBounds (half.removeFromTop (16)); input.slider.setBounds (half);
    output.label.setBounds (row.removeFromTop (16)); output.slider.setBounds (row);
    l.removeFromTop (26);
    bpmArea = l.removeFromTop (34);
    l.removeFromTop (6);
    auto tr = l.removeFromTop (30);
    tapButton.setBounds (tr.removeFromLeft (tr.getWidth() / 2).reduced (0, 0).withTrimmedRight (3));
    syncButton.setBounds (tr.withTrimmedLeft (3));
    l.removeFromTop (26);
    keyBox.setBounds (l.removeFromTop (26));
    l.removeFromTop (6);
    scaleBox.setBounds (l.removeFromTop (26));
    meterArea = l.removeFromBottom (44);

    // right column
    auto rr = rightArea.reduced (12);
    rr.removeFromTop (18);
    auto pr = rr.removeFromTop (44);
    holdButton.setBounds (pr.removeFromLeft (pr.getWidth() / 2).withTrimmedRight (3));
    motionButton.setBounds (pr.withTrimmedLeft (3));
    rr.removeFromTop (26);
    for (int row2 = 0; row2 < 2; ++row2)
    {
        auto br = rr.removeFromTop (44);
        bankButtons[row2 * 2].setBounds (br.removeFromLeft (br.getWidth() / 2).withTrimmedRight (3));
        bankButtons[row2 * 2 + 1].setBounds (br.withTrimmedLeft (3));
        rr.removeFromTop (6);
    }
    auto lr = rr.removeFromTop (26);
    lengthBox.setBounds (lr.removeFromLeft (lr.getWidth() / 2).withTrimmedRight (3));
    resampleButton.setBounds (lr.withTrimmedLeft (3));
    rr.removeFromTop (4);
    auto ll = rr.removeFromTop (84);
    loopLevel.label.setBounds (ll.removeFromTop (14));
    loopLevel.slider.setBounds (ll);
    rr.removeFromTop (20);
    for (int row3 = 0; row3 < 2; ++row3)
    {
        auto mr = rr.removeFromTop (34);
        const int w = mr.getWidth() / 4;
        for (int i = 0; i < 4; ++i) memoryButtons[row3 * 4 + i].setBounds (mr.removeFromLeft (w).reduced (2));
    }
    rr.removeFromTop (4);
    writeButton.setBounds (rr.removeFromTop (28));
}
