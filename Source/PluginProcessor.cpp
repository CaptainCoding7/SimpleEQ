/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SimpleEQAudioProcessor::SimpleEQAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

SimpleEQAudioProcessor::~SimpleEQAudioProcessor()
{
}

//==============================================================================
const juce::String SimpleEQAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SimpleEQAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SimpleEQAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SimpleEQAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SimpleEQAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SimpleEQAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SimpleEQAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SimpleEQAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String SimpleEQAudioProcessor::getProgramName (int index)
{
    return {};
}

void SimpleEQAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
/*****************************************   prepareToPlay function   ******************************************/

void SimpleEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..

    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 1;
    spec.sampleRate = sampleRate;

    leftChain.prepare(spec);
    rightChain.prepare(spec);

    updateFilters();

    leftChannelFifo.prepare(samplesPerBlock);
    rightChannelFifo.prepare(samplesPerBlock);

    // debuging with an oscillator
    osc.initialise([](float x) {return std::sin(x);});
    spec.numChannels = getTotalNumOutputChannels();
    osc.prepare(spec);
    osc.setFrequency(200);

}

void SimpleEQAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SimpleEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

//==============================================================================
/*****************************************   processBlock function   ******************************************/

void SimpleEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    updateFilters();

    juce::dsp::AudioBlock<float> block(buffer);

    ////oscillator stuff
    //buffer.clear();
    //juce::dsp::ProcessContextReplacing<float> stereoContext(block);
    //osc.process(stereoContext);

    auto leftBlock = block.getSingleChannelBlock(0);
    auto rightBlock = block.getSingleChannelBlock(1);

    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);

    leftChain.process(leftContext);
    rightChain.process(rightContext);

    leftChannelFifo.update(buffer);
    rightChannelFifo.update(buffer);

}

//==============================================================================
bool SimpleEQAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SimpleEQAudioProcessor::createEditor()
{
    return new SimpleEQAudioProcessorEditor (*this);
    //return new juce::GenericAudioProcessorEditor(*this);

}

//==============================================================================
void SimpleEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.

    juce::MemoryOutputStream mos(destData, true);
    apvts.state.writeToStream(mos);

}

void SimpleEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.

    auto tree = juce::ValueTree::readFromData(data, sizeInBytes);
    if (tree.isValid()) {
        apvts.replaceState(tree);
        updateFilters();
    }
}

/*
 * Adding parameters for the GUI 
 */
juce::AudioProcessorValueTreeState::ParameterLayout
SimpleEQAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    /* Sliders */

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "LowCut Freq",
        "LowCut Freq", 
        juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f),
        20.f
     ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "HighCut Freq",
        "HighCut Freq",
        juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f),
        20000.f
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "Peak Freq",
        "Peak Freq",
        juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f),
        750.f
        ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "Peak Gain",
        "Peak Gain",
        juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f),
        0.0f
        ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "Peak Quality",
        "Peak Quality",
        juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f),
        1.f ));

    /* Slope choices */

    juce::StringArray stringArray;

    for (int i = 0; i < 4; i++) {
        juce::String str;
        str << (12 + i * 12);
        str << " db/Oct";
        stringArray.add(str);
    }

    layout.add(std::make_unique <juce::AudioParameterChoice>(
        "LowCut Slope",
        "LowCut Slope",
        stringArray,
        0));

    layout.add(std::make_unique <juce::AudioParameterChoice>(
        "HighCut Slope",
        "HighCut Slope",
        stringArray,
        0));

    /****************  4 bypass ***************/

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "LowCut Bypassed",
        "LowCut Bypassed",
        false
        ));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "HighCut Bypassed",
        "HighCut Bypassed",
        false
        ));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "Peak Bypassed",
        "Peak Bypassed",
        false
        ));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "Analyzer Enabled",
        "Analyzer Enabled",
        true
        ));

    return layout;
}



/********************* My functions  *****************************************/

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts) {

    ChainSettings cs;

    cs.lowCutFreq = apvts.getRawParameterValue("LowCut Freq")->load();
    cs.highCutFreq = apvts.getRawParameterValue("HighCut Freq")->load();
    cs.lowCutSlope = static_cast<Slope>(apvts.getRawParameterValue("LowCut Slope")->load());
    cs.highCutSlope = static_cast<Slope>(apvts.getRawParameterValue("HighCut Slope")->load());
    cs.peakFreq = apvts.getRawParameterValue("Peak Freq")->load();
    cs.peakGainInDecibels = apvts.getRawParameterValue("Peak Gain")->load();
    cs.peakQuality = apvts.getRawParameterValue("Peak Quality")->load();

    cs.lowCutBypassed = apvts.getRawParameterValue("LowCut Bypassed")->load() > 0.5f;
    cs.highCutBypassed = apvts.getRawParameterValue("HighCut Bypassed")->load() > 0.5f;
    cs.peakBypassed = apvts.getRawParameterValue("Peak Bypassed")->load() > 0.5f;


    return cs;

}

Coefficients makePeakFilter(const ChainSettings& cs, double sampleRate) {

    return juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate,
        cs.peakFreq,
        cs.peakQuality,
        juce::Decibels::decibelsToGain(cs.peakGainInDecibels));
}

void SimpleEQAudioProcessor::updatePeakFilter(const ChainSettings& cs) {

    //auto peakCoeff = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
    //    getSampleRate(),
    //    cs.peakFreq,
    //    cs.peakQuality,
    //    juce::Decibels::decibelsToGain(cs.peakGainInDecibels)
    //);

    auto peakCoeff = makePeakFilter(cs, getSampleRate());

    //*leftChain.get<ChainPositions::Peak>().coefficients = *peakCoeff;
    //*rightChain.get<ChainPositions::Peak>().coefficients = *peakCoeff;

    leftChain.setBypassed<ChainPositions::Peak>(cs.peakBypassed);
    rightChain.setBypassed<ChainPositions::Peak>(cs.peakBypassed);

    updateCoefficients(leftChain.get<ChainPositions::Peak>().coefficients, peakCoeff);
    updateCoefficients(rightChain.get<ChainPositions::Peak>().coefficients, peakCoeff);
}

void updateCoefficients(Coefficients& oldCoeffs, const Coefficients& newCoeffs) {

    *oldCoeffs = *newCoeffs;
}

template<int Index, typename ChainType, typename CoefficientType>
void update(ChainType& chain, const CoefficientType& cutCoeff) {

    updateCoefficients(chain.get <Index>().coefficients, cutCoeff[Index]);
    chain.setBypassed<Index>(false);

}


/*
 * Function used to update low & high cut filters (for left & right chain)
 */
template<typename ChainType, typename CoefficientType>
void updateCutFilter(ChainType& chain, const CoefficientType& cutCoeff, const Slope& slope) {

    chain.setBypassed<0>(true);
    chain.setBypassed<1>(true);
    chain.setBypassed<2>(true);
    chain.setBypassed<3>(true);

    switch (slope)
    {
        case Slope_48:
        {
            update<3>(chain, cutCoeff);
        }
        case Slope_36:
        {
            update<2>(chain, cutCoeff);
        }
        case Slope_24:
        {
            update<1>(chain, cutCoeff);
        }
        case Slope_12:
        {
            update<0>(chain, cutCoeff);
        }
    }
}

void SimpleEQAudioProcessor::updateLowCutFilter(const ChainSettings& cs) {

    auto lowCutCoeff = makeLowCutFilter(cs, getSampleRate());
    auto& leftLowCut = leftChain.get<ChainPositions::LowCut>();
    auto& rightLowCut = rightChain.get<ChainPositions::LowCut>();

    leftChain.setBypassed<ChainPositions::LowCut>(cs.lowCutBypassed);
    rightChain.setBypassed<ChainPositions::LowCut>(cs.lowCutBypassed);

    updateCutFilter(leftLowCut, lowCutCoeff, cs.lowCutSlope);
    updateCutFilter(rightLowCut, lowCutCoeff, cs.lowCutSlope);
}

void SimpleEQAudioProcessor::updateHighCutFilter(const ChainSettings& cs) {

    auto highCutCoeff = makeHighCutFilter(cs, getSampleRate());
    auto& leftHighCut = leftChain.get<ChainPositions::HighCut>();
    auto& rightHighCut = rightChain.get<ChainPositions::HighCut>();

    leftChain.setBypassed<ChainPositions::HighCut>(cs.highCutBypassed);
    rightChain.setBypassed<ChainPositions::HighCut>(cs.highCutBypassed);

    updateCutFilter(leftHighCut, highCutCoeff, cs.highCutSlope);
    updateCutFilter(rightHighCut, highCutCoeff, cs.highCutSlope);

}


void SimpleEQAudioProcessor::updateFilters() {

    auto cs = getChainSettings(apvts);
    updatePeakFilter(cs);
    // update Low Cut (left&right) 
    updateLowCutFilter(cs);
    // update High Cut (left&right) 
    updateHighCutFilter(cs);
}



//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleEQAudioProcessor();
}
