/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

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
static void updateCoefficients(Coefficients& oldCoeffs, const Coefficients& newCoeffs);

Coefficients makePeakFilter(const ChainSettings& cs, double sampleRate);

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

    template<typename ChainType, typename CoefficientType>
    void updateCutFilter(ChainType& monoLowCut, const CoefficientType& cutCoeff, const Slope& lowCutSlope);

    template<int Index, typename ChainType, typename CoefficientType>
    void update(ChainType& chain, const CoefficientType& cutCoeff);


    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleEQAudioProcessor)
};
