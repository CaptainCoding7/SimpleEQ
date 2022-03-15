/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>

enum Channel
{
    Right, //effectively 0
    Left //eff 1
};

/*
 * A class to retrieve the block samples in order to use the SingleChannelSampleFifo class 
 */
template<typename T>
class Fifo
{
public:
    void prepare(int numChannels, int numSamples)
    {
        static_assert(std::is_same_v<T, juce::AudioBuffer<float>>,
            "Function: prepare(int numChannels, int numSamples) should only be used when the Fifo is holding juce::AudioBuffer<float>");
        for (auto& buffer : buffers)
        {

            buffer.setSize(numChannels, //channel
                numSamples, // nb samples
                false, // keepExistingContent
                true, // clear extra space
                true); // avoid reallocating
            buffer.clear();
        }
    }

    void prepare(size_t nbElements)
    {
        static_assert(std::is_same_v<T, std::vector<float>>,
            "Function: prepare(size_t nbElements) should only be used when the Fifo is holding std::vector<float>");
        for (auto& buffer : buffers)
        {
            buffer.clear(); 
            buffer.resize(nbElements, 0);
        }
    }

    bool push(const T& t)
    {
        auto write = fifo.write(1);
        if (write.blockSize1 > 0)
        {
            buffers[write.startIndex1] = t;
            return true;
        }
        return false;
    }

    bool pull(T& t)
    {
        auto read = fifo.read(1);
        if (read.blockSize1 > 0)
        {
            t = buffers[read.startIndex1];
            return true;
        }
        return false;
    }

    int getNumAvailableForReading() const
    {
        return fifo.getNumReady();
    }

private:
    static constexpr int Capacity = 30;
    std::array<T, Capacity> buffers;
    juce::AbstractFifo fifo{ Capacity };
};

/*
 * A class to have a fixed number of samples
 */
template<typename BlockType>
class SingleChannelSampleFifo
{
public:
    SingleChannelSampleFifo(Channel ch) : channelToUse(ch)
    {
        prepared.set(false);
    }

    void update(const BlockType& buffer)
    {
        jassert(prepared.get());
        jassert(buffer.getNumChannels() > channelToUse);
        auto* channelPtr = buffer.getReadPointer(channelToUse);
        for (size_t i = 0; i < buffer.getNumSamples(); i++)
        {
            pushNextSampleIntoFifo(channelPtr[i]);
        }
    }

    void prepare(int bufferSize)
    {
        prepared.set(false);
        size.set(bufferSize);

        bufferToFill.setSize(1, //channel
            bufferSize, // nb samples
            false, // keepExistingContent
            true, // clear extra space
            true); // avoid reallocating
        audioBufferFifo.prepare(1, bufferSize);
        fifoIndex = 0; 
        prepared.set(true);
    }

    int getNumCompleteBufferAvailable() const 
    {
        return audioBufferFifo.getNumAvailableForReading();
    }
    bool isPrepared() const 
    {
        return prepared.get();
    }
    int getSize() const
    {
        return size.get();
    }
    bool getAudioBuffer(BlockType& buf)
    {
        return audioBufferFifo.pull(buf);
    }
private:
    Channel channelToUse;  
    int fifoIndex = 0;
    Fifo<BlockType> audioBufferFifo;
    BlockType bufferToFill;
    juce::Atomic<bool> prepared = false;
    juce::Atomic<int> size = 0;

    void pushNextSampleIntoFifo(float sample)
    {
        if (fifoIndex == bufferToFill.getNumSamples())
        {
            auto ok = audioBufferFifo.push(bufferToFill);
            juce::ignoreUnused(ok);
            fifoIndex = 0;
        }
        bufferToFill.setSample(0, fifoIndex, sample);
        ++fifoIndex;
    }

};

enum Slope {
    Slope_12,
    Slope_24,
    Slope_36,
    Slope_48
};

struct  ChainSettings
{
    float peakFreq{ 0 };
    float peakGainInDecibels{ 0 };
    float peakQuality{ 1.f };
    float lowCutFreq{ 0 };
    float highCutFreq{ 0 };
    Slope lowCutSlope{ Slope::Slope_12 };
    Slope highCutSlope{ Slope::Slope_12 };

    bool lowCutBypassed{ false };
    bool highCutBypassed{ false };
    bool peakBypassed{ false };
};

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts);

//aliases
using Filter = juce::dsp::IIR::Filter<float>;
using CutFilter = juce::dsp::ProcessorChain<Filter, Filter, Filter, Filter>;
using MonoChain = juce::dsp::ProcessorChain<CutFilter, Filter, CutFilter >;

enum ChainPositions
{
    LowCut,
    Peak,
    HighCut
};

using Coefficients = Filter::CoefficientsPtr;
void updateCoefficients(Coefficients& oldCoeffs, const Coefficients& newCoeffs);

Coefficients makePeakFilter(const ChainSettings& cs, double sampleRate);

template<typename ChainType, typename CoefficientType>
void updateCutFilter(ChainType& monoLowCut, const CoefficientType& cutCoeff, const Slope& lowCutSlope);

template<int Index, typename ChainType, typename CoefficientType>
void update(ChainType& chain, const CoefficientType& cutCoeff);


// inline functions in order to use them in files that include this header
// nice explanation :o  : https://www.commentcamarche.net/faq/11250-les-inlines-en-c
inline auto makeLowCutFilter(const ChainSettings& cs, double sampleRate) {
    return juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(cs.lowCutFreq, sampleRate, 2 * (cs.lowCutSlope + 1));
}

inline auto makeHighCutFilter(const ChainSettings& cs, double sampleRate) {
    return juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(cs.highCutFreq, sampleRate, 2 * (cs.highCutSlope + 1));
}

//==============================================================================
/**
*/
class SimpleEQAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    SimpleEQAudioProcessor();
    ~SimpleEQAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout
        createParameterLayout();

    juce::AudioProcessorValueTreeState apvts{ *this, nullptr,"Parameters", createParameterLayout() };

    using BlockType = juce::AudioBuffer<float>;
    SingleChannelSampleFifo<BlockType> leftChannelFifo{ Channel::Left };
    SingleChannelSampleFifo<BlockType> rightChannelFifo{ Channel::Right };


private: 

    MonoChain leftChain, rightChain;

    /*
     * Here is the call order:
     * updateFilters -> updateHigh/LowCutFilter -> updateCutFilter -> update -> updateCoefficients
     *               -> updatePeakFilter -> updateCoefficients
     */

    void updateFilters();

    void updatePeakFilter(const ChainSettings& cs);

    void updateHighCutFilter(const ChainSettings& cs);
    void updateLowCutFilter(const ChainSettings& cs);

    juce::dsp::Oscillator<float> osc;

   
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleEQAudioProcessor)
};
