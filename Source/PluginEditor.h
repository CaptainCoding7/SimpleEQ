/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"


enum FFTOrder
{
    order2048 = 11, 
    order4096=12,
    order8192=13
};

template<typename BlockType>
class FFTDataGenerator
{
public:
    /*
     * produces the FFT data from an audio buffer
     */
    void produceFFTDataForRendering(const juce::AudioBuffer<float>& audioData, const float negInfinity) 
    {
        const auto fftSize = getFFTSize();
        fftData.assign(fftData.size(), 0);
        auto* readIndex = audioData.getReadPointer(0);
        std::copy(readIndex, readIndex + fftSize, fftData.begin());

        //first apply a windowing function to our data
        window->multiplyWithWindowingTable(fftData.data(), fftSize);

        //then render our fft data
        forwardFFT->performFrequencyOnlyForwardTransform(fftData.data());

        int nbBins = (int)fftSize / 2;
        for (size_t i = 0; i < nbBins; i++)
        {
            //normalize the fft values
            fftData[i] /= (float)nbBins;
            //convert them to decibels
            fftData[i] = juce::Decibels::gainToDecibels(fftData[i], negInfinity);
        }
        fftDataFifo.push(fftData);

    }

    void changeOrder(FFTOrder newOrder)
    {
        //when you change order, recreate the window, forwardFFT, fifo, fftData
        // + reset the fifoIndex
        // things that need recreating should be created on the heap with std::make_unique<>

        order = newOrder;
        auto fftSize = getFFTSize();

        forwardFFT = std::make_unique<juce::dsp::FFT>(order);
        window = std::make_unique<juce::dsp::WindowingFunction<float>>(fftSize, juce::dsp::WindowingFunction<float>::blackmanHarris);

        fftData.clear();
        fftData.resize(fftSize * 2, 0);
        fftDataFifo.prepare(fftData.size());
    }
    int getFFTSize() const { return 1 << order; }
    int getNumAvailableFFTDataBlocks() const { return fftDataFifo.getNumAvailableForReading(); }
    bool getFFTData(BlockType& fftData) { return fftDataFifo.pull(fftData); }

private:
    FFTOrder order;
    BlockType fftData;
    std::unique_ptr<juce::dsp::FFT> forwardFFT;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    Fifo<BlockType> fftDataFifo;
};

template<typename PathType>
class AnalyzerPathGenerator
{
public: 

    /* Converts renderData[] into a juce::Path */
    void generatePath(const std::vector<float>& renderData,
        juce::Rectangle<float> fftBounds,
        int fftSize,
        float binWidth,
        float negInf)
    {
        auto top = fftBounds.getY();
        auto bottom = fftBounds.getHeight();
        auto width = fftBounds.getWidth();

        int nbBins = (int)fftSize / 2;

        PathType p;
        p.preallocateSpace(3 * (int)fftBounds.getWidth());

        auto map = [bottom, top, negInf](float v)
        {
            return juce::jmap(v, negInf, 0.f, float(bottom), top);
        };
        auto y = map(renderData[0]);

        jassert(!std::isnan(y) && !std::isinf(y));

        p.startNewSubPath(0, y);

        const int pathResolution = 2;
        for (int binNum = 1; binNum < nbBins; binNum += pathResolution)
        {
            y = map(renderData[binNum]);
            jassert(!std::isnan(y) && !std::isinf(y));
            if (!std::isnan(y) && !std::isinf(y))
            {
                auto binFreq = binNum * binWidth;
                auto normalzedBinX = juce::mapFromLog10(binFreq, 20.f, 20000.f);
                int binX = std::floor(normalzedBinX * width);
                p.lineTo(binX, y);
            }
        }
        pathFifo.push(p);
    }


    int getNumPathsAvailable() const { return pathFifo.getNumAvailableForReading(); }
    bool getPath(PathType& path) { return pathFifo.pull(path); }

private:
    Fifo<PathType> pathFifo;

};

struct LookAndFeel : juce::LookAndFeel_V4 
{
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
        float sliderPosProportional, float rotaryStartAngle,
        float rotaryEndAngle, juce::Slider&) override;
};

struct RotarySliderWithLabels : juce::Slider 
{
    RotarySliderWithLabels(juce::RangedAudioParameter &rap, const juce::String& unitSuffix) :
        juce::Slider(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag,
                     juce::Slider::TextEntryBoxPosition::NoTextBox),
        param(&rap),
        suffix(unitSuffix)
    {
        setLookAndFeel(&lnf);
    }
    ~RotarySliderWithLabels() {
        setLookAndFeel(nullptr);
    }

    struct LabelPos
    {
        float pos;
        juce::String label;
    };

    juce::Array<LabelPos> labels;

    void paint(juce::Graphics& g) override;
    juce::Rectangle<int> getSliderBounds() const;
    int getTextHeight() const { return 14; }
    juce::String getDisplayString() const;

private:
    LookAndFeel lnf;
    juce::RangedAudioParameter* param;
    juce::String suffix;
};

class PathProducer
{
public: 
    PathProducer(SingleChannelSampleFifo<SimpleEQAudioProcessor::BlockType>& scsf) :
        monoChannelFifo(&scsf) 
    {
        monoChannelFFTDataGenerator.changeOrder(FFTOrder::order2048);
        monoBuffer.setSize(1, monoChannelFFTDataGenerator.getFFTSize());
    }
    void process(juce::Rectangle<float> fftBounds, double sampleRate);
    juce::Path getPath() { return monoChannelFFTPath; }

private:
    // converting audio samples into FFT data with :
    SingleChannelSampleFifo<SimpleEQAudioProcessor::BlockType>* monoChannelFifo;
    juce::AudioBuffer<float> monoBuffer;
    FFTDataGenerator<std::vector<float>> monoChannelFFTDataGenerator;
    AnalyzerPathGenerator<juce::Path> pathProducer;
    juce::Path monoChannelFFTPath;
};

/*
 * We used the SimpleEQAudioProcessorEditor class as a model to 
 * write our ResponseCurveComponent class below 
 */
struct ResponseCurveComponent : juce::Component,
    juce::AudioProcessorParameter::Listener,
    juce::Timer
{
    ResponseCurveComponent(SimpleEQAudioProcessor&);
    ~ResponseCurveComponent();

    //==============================================================================

    void parameterValueChanged(int parameterIndex, float newValue) override;

    /** Indicates that a parameter change gesture has started.

        E.g. if the user is dragging a slider, this would be called with gestureIsStarting
        being true when they first press the mouse button, and it will be called again with
        gestureIsStarting being false when they release it.

        IMPORTANT NOTE: This will be called synchronously, and many audio processors will
        call it during their audio callback. This means that not only has your handler code
        got to be completely thread-safe, but it's also got to be VERY fast, and avoid
        blocking. If you need to handle this event on your message thread, use this callback
        to trigger an AsyncUpdater or ChangeBroadcaster which you can respond to later on the
        message thread.
    */
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override {}

    void timerCallback() override;

    void paint(juce::Graphics&) override;

    void updateChain();


private:
    SimpleEQAudioProcessor& audioProcessor;
    // atomic flag
    juce::Atomic<bool> parametersChanged{ false };

    MonoChain monoChain;

    // fonction used to manage visual render
    juce::Image background;
    void resized() override;
    juce::Rectangle<int> getRenderArea();
    juce::Rectangle<int> getAnalysisArea();

    PathProducer leftPathProducer, rightPathProducer;
};


//==============================================================================
/**
*/
class SimpleEQAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    SimpleEQAudioProcessorEditor (SimpleEQAudioProcessor&);
    ~SimpleEQAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
      
private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    SimpleEQAudioProcessor& audioProcessor;

    RotarySliderWithLabels peakFreqSlider,
                       peakGainSlider,
                       peakQualitySlider,
                       lowCutFreqSlider,
                       highCutFreqSlider,
                       lowCutSlopeSlider,
                       highCutSlopeSlider;

    // An instance of ResponsCurveComponent as an attribute of the SimpleEQAudioProcessorEditor class
    ResponseCurveComponent rcc;

    using apvts = juce::AudioProcessorValueTreeState;
    using Attachment = apvts::SliderAttachment;

    Attachment peakFreqSliderAttachment,
        peakGainSliderAttachment,
        peakQualitySliderAttachment,
        lowCutFreqSliderAttachment,
        highCutFreqSliderAttachment,
        lowCutSlopeSliderAttachment,
        highCutSlopeSliderAttachment;

    juce::ToggleButton lowCutBypassButton, peakBypassButton, highCutBypassButton, analyzerEnabledButton;
    using ButtonAttachment = apvts::ButtonAttachment;
    ButtonAttachment lowCutBypassButtonAttachment,
                     peakBypassButtonAttachment, 
                     highCutBypassButtonAttachment, 
                     analyzerEnabledButtonAttachment;

    std::vector<juce::Component*> getComps();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleEQAudioProcessorEditor)
};

