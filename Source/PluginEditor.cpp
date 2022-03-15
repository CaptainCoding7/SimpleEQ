/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

const auto lightBlue = juce::Colour(50, 162, 168);
const auto blue = juce::Colour(0, 0, 149);

/****************************** LookAndFeel Stuff *****************************/

/* 
 * We need to use a class that inherits from LookAndFeel_VX in order to draw inside the slider bounds
 */
void LookAndFeel::drawRotarySlider(juce::Graphics& g, 
                                   int x, 
                                   int y, 
                                   int width, 
                                   int height,
                                   float sliderPosProportional, 
                                   float rotaryStartAngle,
                                   float rotaryEndAngle, 
                                   juce::Slider& slider)
{
    using namespace juce;

    /* Drawing a slider with colours */
    auto bounds = Rectangle<float>(x, y, width, height);
    g.setColour(lightBlue);
    g.fillEllipse(bounds);
    
    g.setColour(blue);
    g.drawEllipse(bounds, 2.f);

    // we just check that it's a RotarySliderWithLabels slider
    if (auto* rswl = dynamic_cast<RotarySliderWithLabels*>(&slider))
    {

        auto center = bounds.getCentre();
        Path path;
        
        /* Creating the hand (aiguille) in the slider */
     
        Rectangle<float> rect;

        rect.setLeft(center.getX() - 2);
        rect.setRight(center.getX() + 2);
        rect.setBottom(center.getY() - rswl->getTextHeight() * 1.5);
        rect.setTop(bounds.getY() );

        path.addRoundedRectangle(rect, 2.f);

        jassert(rotaryStartAngle < rotaryEndAngle);

        auto sliderAngRad = jmap(sliderPosProportional, 0.f, 1.f, rotaryStartAngle, rotaryEndAngle);

        path.applyTransform(AffineTransform().rotated(sliderAngRad, center.getX(), center.getY()));
        g.fillPath(path);

        /* We set the value text appearence */

        g.setFont(rswl->getTextHeight());
        auto text = rswl->getDisplayString();
        auto strWidth = g.getCurrentFont().getStringWidth(text);

        rect.setSize(strWidth + 4, rswl->getTextHeight() + 2); 
        rect.setCentre(bounds.getCentre());

        g.setColour(Colours::black);
        g.fillRect(rect);

        g.setColour(Colours::white);
        g.drawFittedText(text, rect.toNearestInt(), juce::Justification::centred, 1 );
        
    
    }


}

/****************************** RotarySliderWithLabels stuff *****************************/

void RotarySliderWithLabels::paint(juce::Graphics& g) 
{
    using namespace juce;

    auto startAng = degreesToRadians(180.f + 45.f);
    auto endAng = degreesToRadians(180.f - 45.f) + MathConstants<float>::twoPi;
    auto range = getRange();

    auto sliderBounds = getSliderBounds();


    /* We set the colours of our local bounds and slider bounds (visual debug) */
    //g.setColour(Colour(185, 0, 0));
    //g.drawRect(getLocalBounds());
    //g.setColour(Colour(0, 77, 230));
    //g.drawRect(sliderBounds);

    /* We use this fonction to draw our sliders (see above) */
    getLookAndFeel().drawRotarySlider(
        g,
        sliderBounds.getX(),
        sliderBounds.getY(),
        sliderBounds.getWidth(),
        sliderBounds.getHeight(),
        jmap(getValue(), range.getStart(), range.getEnd(), 0.0, 1.0), startAng, endAng, *this);


    auto center = sliderBounds.toFloat().getCentre();
    auto radius = sliderBounds.getWidth() * 0.5f;

    g.setColour(Colour(0, 255, 204));
    g.setFont(getTextHeight());

    auto numchoices = labels.size();
    for (int i = 0; i < numchoices; i++) {
        auto pos = labels[i].pos;
        jassert(pos <= 1.f);
        jassert(0.f <= pos);

        // We place the text of the min/max labels just below the circle defining the slider,
        // near the corners of the slider box bounds
        auto ang = jmap(pos, 0.f, 1.f, startAng, endAng);
        auto c = center.getPointOnCircumference(radius + getTextHeight() * 0.5f + 1, ang);

        Rectangle<float> rect;
        auto str = labels[i].label;
        rect.setSize(g.getCurrentFont().getStringWidth(str), getTextHeight());
        rect.setCentre(c);
        rect.setY(rect.getY() + getTextHeight());

        g.drawFittedText(str, rect.toNearestInt(), juce::Justification::centred, 1);
    }

        
}

juce::Rectangle<int> RotarySliderWithLabels::getSliderBounds() const 
{
    auto bounds = getLocalBounds();
    auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());

    size -= getTextHeight() * 2;
    juce::Rectangle<int> rect;
    rect.setSize(size, size);
    rect.setCentre(bounds.getCentreX(), 0);
    rect.setY(2);

    return rect;

}

juce::String RotarySliderWithLabels::getDisplayString() const
{

    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param))
        return choiceParam->getCurrentChoiceName();
    
    juce::String str;
    bool addK = false;

    if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
    {
        float val = getValue();
        if (val > 999)
        {
            val /= 1000.f;
            addK = true;
        }

        str = juce::String(val, (addK ? 2 : 0));

    }
    else {
        jassertfalse;
    }

    if (suffix.isNotEmpty())
    {
        str << " ";
            if(addK)
                str << "k";
            str << suffix;
    }
    return str;
}


/****************************** ResponseCurveComponent stuff *****************************/

ResponseCurveComponent::ResponseCurveComponent(SimpleEQAudioProcessor& p) : audioProcessor(p) {
    const auto& params = audioProcessor.getParameters();
    for (auto param : params)
        param->addListener(this);

    updateChain(); 

    startTimerHz(60);

}

ResponseCurveComponent::~ResponseCurveComponent() {
    const auto& params = audioProcessor.getParameters();
    for (auto param : params)
        param->removeListener(this);
}


void ResponseCurveComponent::parameterValueChanged(int parameterIndex, float newValue) {

    parametersChanged.set(true);

}

void ResponseCurveComponent::timerCallback() {

    if (parametersChanged.compareAndSetBool(false, true)) {

        DBG("params changed :-)");

        //update the monochain
        updateChain();

        //repaint the curve
        repaint();
    }

}

void ResponseCurveComponent::updateChain() {

    auto cs = getChainSettings(audioProcessor.apvts);
    auto peakCoeff = makePeakFilter(cs, audioProcessor.getSampleRate());
    updateCoefficients(monoChain.get<ChainPositions::Peak>().coefficients, peakCoeff);

    auto lowCutCoeff = makeLowCutFilter(cs, audioProcessor.getSampleRate());
    updateCutFilter(monoChain.get<ChainPositions::LowCut>(), lowCutCoeff, cs.lowCutSlope);

    auto highCutCoeff = makeHighCutFilter(cs, audioProcessor.getSampleRate());
    updateCutFilter(monoChain.get<ChainPositions::HighCut>(), highCutCoeff, cs.highCutSlope);

}

void ResponseCurveComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.drawImage(background, getLocalBounds().toFloat());

    auto responseArea = getAnalysisArea();
    auto width = responseArea.getWidth();

    auto& lowCut = monoChain.get<ChainPositions::LowCut>();
    auto& peak = monoChain.get<ChainPositions::Peak>();
    auto& highCut = monoChain.get<ChainPositions::HighCut>();

    auto sampleRate = audioProcessor.getSampleRate();

    // where we'll store the magnitudes:
    std::vector<double> mags;
    // one magnitude per pixel
    mags.resize(width);

    // magnitudes are expressed with gain units (multiplicative)
    // frequencies are expressd with decibels (additive) 
    for (size_t i = 0; i < width; i++)
    {
        double mag = 1.f;
        auto freq = juce::mapToLog10(double(i) / double(width), 20.0, 20000.0);

        if (!monoChain.isBypassed <ChainPositions::Peak>())
            mag *= peak.coefficients->getMagnitudeForFrequency(freq, sampleRate);

        if (!lowCut.isBypassed<0>())
            mag *= lowCut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowCut.isBypassed<1>())
            mag *= lowCut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowCut.isBypassed<2>())
            mag *= lowCut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowCut.isBypassed<3>())
            mag *= lowCut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);

        if (!highCut.isBypassed<0>())
            mag *= highCut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highCut.isBypassed<1>())
            mag *= highCut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highCut.isBypassed<2>())
            mag *= highCut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highCut.isBypassed<3>())
            mag *= highCut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);

        mags[i] = juce::Decibels::gainToDecibels(mag);
    }

    juce::Path responseCurve;
    const double outputMin = responseArea.getBottom();
    const double outputMax = responseArea.getY();
    auto map = [outputMin, outputMax](double input)
    {
        return juce::jmap(input, -24.0, 24.0, outputMin, outputMax);
    };

    responseCurve.startNewSubPath(responseArea.getX(), map(mags.front()));

    for (size_t i = 0; i < mags.size(); i++)
    {
        responseCurve.lineTo(responseArea.getX() + i, map(mags[i]));
    }

    g.setColour(juce::Colours::orange);
    g.drawRoundedRectangle(getRenderArea().toFloat(), 4.f, 1.4);

    g.setColour(juce::Colours::white);
    g.strokePath(responseCurve, juce::PathStrokeType(2.f));

}

void ResponseCurveComponent::resized() 
{
    using namespace juce;
    background = Image(Image::PixelFormat::RGB, getWidth(), getHeight(), true);
    Graphics g(background);

    //frequencie lines

    Array<float> freqs
    {
        20, /*30, 40,*/ 50, 100,
        200, /*300, 400,*/ 500, 1000,
        2000, /*3000, 4000,*/ 5000, 10000,
        20000
    };

    auto renderArea = getAnalysisArea();
    auto left = renderArea.getX();
    auto right = renderArea.getRight();
    auto top = renderArea.getY();
    auto bottom = renderArea.getBottom();
    auto width = renderArea.getWidth();

    Array<float> xs;
    for (auto f : freqs)
    {
        auto normX = mapFromLog10(f, 20.f, 20000.f);
        xs.add(left + width * normX);
    }

    g.setColour(Colours::dimgrey);
    for (auto xs : xs)
    {
        g.drawVerticalLine(xs, top, bottom);
    
    }

    // freq labels

    g.setColour(Colours::lightgrey);
    const int fontHeight = 10;
    g.setFont(fontHeight);

    for (int i = 0; i < freqs.size(); i++)
    {
        auto f = freqs[i];
        auto x = xs[i];

        bool addK = false;
        String str;
        if (f > 999.f)
        {
            addK = true;
            f /= 1000.f;
        }
        str << f;
        if (addK)
            str << "k";
        str << "Hz";

        auto textWidth = g.getCurrentFont().getStringWidth(str);
        Rectangle<int> rect;
        rect.setSize(textWidth, fontHeight);
        rect.setCentre(x, 0);
        rect.setY(1);

        g.drawFittedText(str, rect, Justification::centred, 1);
    }

    // gain lines and labels

    Array<float> gains
    {
        -24, -12, 0, 12, 24
    };

    g.setColour(Colours::white);
    for (auto g_dB : gains)
    {
        // gain lines
        auto y = jmap(g_dB, -24.f, 24.f, float(bottom), float(top));
        g.setColour(g_dB == 0.f ? lightBlue : Colours::darkgrey);
        g.drawHorizontalLine(y, left, right);

        // gain right labels
        String str;
        if (g_dB > 0)
            str << "+";
        str << g_dB;

        g.setColour(g_dB == 0.f ? lightBlue : Colours::lightgrey);
        auto textWidth = g.getCurrentFont().getStringWidth(str);
        Rectangle<int> rect;
        rect.setSize(textWidth, fontHeight);
        rect.setX(getWidth() - textWidth);
        rect.setCentre(rect.getCentreX(), y);
        g.drawFittedText(str, rect, Justification::centred, 1);

        // gain left labels
        str.clear();
        str << (g_dB - 24.f);

        rect.setX(1);
        textWidth = g.getCurrentFont().getStringWidth(str);
        rect.setSize(textWidth, fontHeight);
        g.drawFittedText(str, rect, Justification::centred, 1);


    }
   

}

juce::Rectangle<int> ResponseCurveComponent::getRenderArea()
{
    auto bounds = getLocalBounds();
    
    //bounds.reduce(10,//JUCE_LIVE_CONSTANT(5), 
    //              8);//JUCE_LIVE_CONSTANT(5));

    bounds.removeFromTop(12);
    bounds.removeFromBottom(2);
    bounds.removeFromLeft(20);
    bounds.removeFromRight(20);
    return bounds;
}

juce::Rectangle<int> ResponseCurveComponent::getAnalysisArea()
{
    auto bounds = getRenderArea();
    bounds.removeFromTop(4);
    bounds.removeFromBottom(4);
    return bounds;
}

//==============================================================================
SimpleEQAudioProcessorEditor::SimpleEQAudioProcessorEditor(SimpleEQAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
    peakFreqSlider(*audioProcessor.apvts.getParameter("Peak Freq"), "Hz"), 
    peakGainSlider(*audioProcessor.apvts.getParameter("Peak Gain"), "dB"),
    peakQualitySlider(*audioProcessor.apvts.getParameter("Peak Quality"), ""),
    lowCutFreqSlider(*audioProcessor.apvts.getParameter("LowCut Freq"), "Hz"),
    highCutFreqSlider(*audioProcessor.apvts.getParameter("HighCut Freq"), "Hz"),
    lowCutSlopeSlider(*audioProcessor.apvts.getParameter("LowCut Slope"), "dB/Octave"),
    highCutSlopeSlider(*audioProcessor.apvts.getParameter("HighCut Slope"), "dB/Octave"),

    rcc(audioProcessor),
      peakFreqSliderAttachment(audioProcessor.apvts, "Peak Freq", peakFreqSlider),
      peakGainSliderAttachment(audioProcessor.apvts, "Peak Gain", peakGainSlider),
      peakQualitySliderAttachment(audioProcessor.apvts, "Peak Quality", peakQualitySlider),
      lowCutFreqSliderAttachment(audioProcessor.apvts, "LowCut Freq", lowCutFreqSlider),
      lowCutSlopeSliderAttachment(audioProcessor.apvts, "LowCut Slope", lowCutSlopeSlider),
      highCutFreqSliderAttachment(audioProcessor.apvts, "HighCut Freq", highCutFreqSlider),
      highCutSlopeSliderAttachment(audioProcessor.apvts, "HighCut Slope", highCutSlopeSlider)
{

    peakFreqSlider.labels.add({ 0.f, "20Hz" });
    peakFreqSlider.labels.add({ 1.f, "20kHz" });

    peakGainSlider.labels.add({ 0.f, "-24dB" });
    peakGainSlider.labels.add({ 1.f, "+24dB" });

    peakQualitySlider.labels.add({ 0.f, "0.1" });
    peakQualitySlider.labels.add({ 1.f, "10.0" });

    lowCutFreqSlider.labels.add({ 0.f, "20Hz" });
    lowCutFreqSlider.labels.add({ 1.f, "20kHz" });

    highCutFreqSlider.labels.add({ 0.f, "20Hz" });
    highCutFreqSlider.labels.add({ 1.f, "20kHz" });

    lowCutSlopeSlider.labels.add({ 0.f, "12dB/Oct" });
    lowCutSlopeSlider.labels.add({ 1.f, "48dB/Oct" });

    highCutSlopeSlider.labels.add({ 0.f, "12dB/Oct" });
    highCutSlopeSlider.labels.add({ 1.f, "48dB/Oct" });

    for (auto* comp : getComps())
    {
        addAndMakeVisible(comp);
    }

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (600, 480);
}

SimpleEQAudioProcessorEditor::~SimpleEQAudioProcessorEditor()
{
}

//==============================================================================
void SimpleEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

}

void SimpleEQAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    auto bounds = getLocalBounds();
    
    float hRatio = 25.f / 100.f;
    // hRatio = JUCE_LIVE_CONSTANT(33) / 100.f; //DEBUG 
    auto responseArea = bounds.removeFromTop(bounds.getHeight() * hRatio);

    rcc.setBounds(responseArea);

    bounds.removeFromTop(5);

    auto lowCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
    auto highCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);

    lowCutFreqSlider.setBounds(lowCutArea.removeFromTop(lowCutArea.getHeight()*0.5));
    lowCutSlopeSlider.setBounds(lowCutArea);

    highCutFreqSlider.setBounds(highCutArea.removeFromTop(highCutArea.getHeight() * 0.5));
    highCutSlopeSlider.setBounds(highCutArea);


    peakFreqSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.33));
    peakGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.5));
    peakQualitySlider.setBounds(bounds);

}


std::vector<juce::Component*>SimpleEQAudioProcessorEditor::getComps() 
{
    return
    {
        &peakFreqSlider,
        &peakGainSlider,
        &peakQualitySlider,
        &lowCutFreqSlider,
        &highCutFreqSlider,
        &lowCutSlopeSlider,
        &highCutSlopeSlider,
        &rcc
    };
}