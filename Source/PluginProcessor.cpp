#include "PluginProcessor.h"

#include "OverlapAdd.h"

// Defined further down, used from the audio callbacks above it.
void logMessage (const juce::String& message);

#if JUCE_MAC || JUCE_IOS
 #include <sys/sysctl.h>
#endif

#include <algorithm>
#include <cmath>
#include <vector>
#include "PluginEditor.h"

SubverseSplitterAudioProcessor::SubverseSplitterAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       modelRunner(*this)
#endif
{
    formatManager.registerBasicFormats();
}

SubverseSplitterAudioProcessor::~SubverseSplitterAudioProcessor()
{
}

const juce::String SubverseSplitterAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SubverseSplitterAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SubverseSplitterAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SubverseSplitterAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SubverseSplitterAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SubverseSplitterAudioProcessor::getNumPrograms()
{
    return 1;
}

int SubverseSplitterAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SubverseSplitterAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String SubverseSplitterAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void SubverseSplitterAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void SubverseSplitterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    logMessage ("prepareToPlay: " + juce::String (sampleRate, 0) + " Hz, block "
                + juce::String (samplesPerBlock) + ", in " + juce::String (getTotalNumInputChannels())
                + " out " + juce::String (getTotalNumOutputChannels()));
    mixerSource.prepareToPlay(static_cast<int>(samplesPerBlock), sampleRate);
    for (int i = 0; i < 4; ++i)
    {
        if (transportSources[i] != nullptr)
            transportSources[i]->prepareToPlay(static_cast<int>(samplesPerBlock), sampleRate);
    }
}

void SubverseSplitterAudioProcessor::releaseResources()
{
    mixerSource.releaseResources();
    for (int i = 0; i < 4; ++i)
    {
        if (transportSources[i] != nullptr)
            transportSources[i]->releaseResources();
    }
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SubverseSplitterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getChannelSet (false, 0) != juce::AudioChannelSet::mono()
     && layouts.getChannelSet (false, 0) != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    // We don't require inputs for this stem splitter plugin!
    // So we don't check if input matches output.
   #endif

    return true;
  #endif
}
#endif

void SubverseSplitterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    if (isPlaying.load())
    {
        buffer.clear();
        juce::AudioSourceChannelInfo info(&buffer, 0, buffer.getNumSamples());
        mixerSource.getNextAudioBlock(info);
    }
    else
    {
        // Pass-through processing since stem splitting is offline
        auto totalNumInputChannels  = getTotalNumInputChannels();
        auto totalNumOutputChannels = getTotalNumOutputChannels();
        for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
            buffer.clear (i, 0, buffer.getNumSamples());
    }
}

// Playback Engine Implementation
void SubverseSplitterAudioProcessor::loadStemForPlayback(int stemIndex, const juce::File& file)
{
    if (stemIndex < 0 || stemIndex >= 4) return;
    
    auto* reader = formatManager.createReaderFor(file);
    if (reader != nullptr)
    {
        auto newSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
        auto newTransport = std::make_unique<juce::AudioTransportSource>();
        newTransport->setSource(newSource.get(), 0, nullptr, reader->sampleRate);
        
        mixerSource.addInputSource(newTransport.get(), false);
        
        readerSources[stemIndex] = std::move(newSource);
        transportSources[stemIndex] = std::move(newTransport);
    }
}

void SubverseSplitterAudioProcessor::togglePlayPause()
{
    bool currentlyPlaying = isPlaying.load();
    if (!currentlyPlaying)
    {
        // Check if we need to rewind
        for (int i = 0; i < 4; ++i)
        {
            if (transportSources[i] != nullptr && transportSources[i]->hasStreamFinished())
                transportSources[i]->setPosition(0.0);
            
            if (transportSources[i] != nullptr)
                transportSources[i]->start();
        }
    }
    else
    {
        for (int i = 0; i < 4; ++i)
        {
            if (transportSources[i] != nullptr)
                transportSources[i]->stop();
        }
    }
    isPlaying.store(!currentlyPlaying);
}

void SubverseSplitterAudioProcessor::applyGains()
{
    // Solo, where anything is soloed, decides who is audible; mute decides it
    // otherwise. Either way the level a stem is audible *at* is its own, so a
    // stem turned down to a third stays there through a solo and comes back at
    // a third, rather than snapping to unity.
    const bool anySolo = stemSolos[0] || stemSolos[1] || stemSolos[2] || stemSolos[3];

    for (int i = 0; i < 4; ++i)
    {
        if (transportSources[i] == nullptr)
            continue;

        const bool audible = anySolo ? stemSolos[i].load() : ! stemMutes[i].load();
        transportSources[i]->setGain (audible ? stemVolumes[i].load() : 0.0f);
    }
}

void SubverseSplitterAudioProcessor::setStemMute(int stemIndex, bool shouldMute)
{
    if (stemIndex >= 0 && stemIndex < 4)
    {
        stemMutes[stemIndex] = shouldMute;
        applyGains();
    }
}

void SubverseSplitterAudioProcessor::setStemVolume (int stemIndex, float volume)
{
    if (stemIndex >= 0 && stemIndex < 4)
    {
        stemVolumes[stemIndex] = juce::jlimit (0.0f, 1.0f, volume);
        applyGains();
    }
}

float SubverseSplitterAudioProcessor::getStemVolume (int stemIndex) const
{
    return (stemIndex >= 0 && stemIndex < 4) ? stemVolumes[stemIndex].load() : 1.0f;
}

void SubverseSplitterAudioProcessor::setPlaybackPosition (double seconds)
{
    const double length = getTotalLength();
    const double target = juce::jlimit (0.0, juce::jmax (0.0, length), seconds);

    for (int i = 0; i < 4; ++i)
        if (transportSources[i] != nullptr)
            transportSources[i]->setPosition (target);
}

bool SubverseSplitterAudioProcessor::getStemMute(int stemIndex) const
{
    return (stemIndex >= 0 && stemIndex < 4) ? stemMutes[stemIndex].load() : false;
}

void SubverseSplitterAudioProcessor::setStemSolo(int stemIndex, bool shouldSolo)
{
    if (stemIndex >= 0 && stemIndex < 4)
    {
        stemSolos[stemIndex] = shouldSolo;
        applyGains();
    }
}

bool SubverseSplitterAudioProcessor::getStemSolo(int stemIndex) const
{
    return (stemIndex >= 0 && stemIndex < 4) ? stemSolos[stemIndex].load() : false;
}

double SubverseSplitterAudioProcessor::getPlaybackPosition() const
{
    if (transportSources[0] != nullptr)
        return transportSources[0]->getCurrentPosition();
    return 0.0;
}

double SubverseSplitterAudioProcessor::getTotalLength() const
{
    if (transportSources[0] != nullptr)
        return transportSources[0]->getLengthInSeconds();
    return 0.0;
}

bool SubverseSplitterAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SubverseSplitterAudioProcessor::createEditor()
{
    return new SubverseSplitterAudioProcessorEditor (*this);
}

void SubverseSplitterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ignoreUnused(destData);
}

void SubverseSplitterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ignoreUnused(data, sizeInBytes);
}

// ==============================================================================
// ModelRunner Implementation (Asynchronous AI Execution)
// ==============================================================================
void logMessage(const juce::String& message)
{
    // Written where the operating system keeps logs, not into one developer's
    // scratch directory. The previous path was absolute and personal, so on
    // every other machine `appendText` had nowhere to write and failed
    // silently — the log existed only where it was least needed, and a user
    // reporting "it does nothing" had nothing to send.
    static const juce::File logFile = []
    {
        auto file = juce::FileLogger::getSystemLogFileFolder()
                        .getChildFile ("SubverseLab")
                        .getChildFile ("SubverseLabSplitter.log");
        file.getParentDirectory().createDirectory();

        // A separation logs a line per window, so this grows. Start fresh once
        // it passes a megabyte rather than accumulating every run ever made.
        if (file.existsAsFile() && file.getSize() > 1024 * 1024)
            file.deleteFile();

        return file;
    }();

    logFile.appendText (juce::Time::getCurrentTime().toISO8601 (true) + "  " + message + "\n");
}

SubverseSplitterAudioProcessor::ModelRunner::ModelRunner(SubverseSplitterAudioProcessor& p)
    : Thread("SubverseSplitterAIWorker"), owner(p)
{
}

SubverseSplitterAudioProcessor::ModelRunner::~ModelRunner()
{
    stopThread(3000);
}

void SubverseSplitterAudioProcessor::ModelRunner::loadModel()
{
    if (session != nullptr) return; // already loaded
    
    logMessage("Loading ONNX Model dynamically from disk...");
    
    try {
        Ort::Env envLocal(ORT_LOGGING_LEVEL_WARNING, "Demucs");
        Ort::SessionOptions sessionOptions;
        // Performance cores, not every core.
        //
        // Handing this the full core count is the obvious move and it is
        // slower: measured on an M2, three windows took 17.5 s across all eight
        // threads and 14 s across four. An Apple Silicon machine reports its
        // efficiency cores in the same total, and a parallel matrix op split
        // evenly across cores of two different speeds runs at the pace of the
        // slow ones while the fast ones wait at the barrier. Asking for the
        // performance cores specifically is what the number was meant to be.
        int threads = juce::SystemStats::getNumCpus();
       #if JUCE_MAC || JUCE_IOS
        {
            int performanceCores = 0;
            size_t size = sizeof (performanceCores);
            if (sysctlbyname ("hw.perflevel0.logicalcpu", &performanceCores, &size, nullptr, 0) == 0
                && performanceCores > 0)
                threads = performanceCores;
        }
       #endif
        sessionOptions.SetIntraOpNumThreads (juce::jmax (1, threads));
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        
        // Find the model file (Search in multiple locations to support Standalone, VST, and Dev environments)
        juce::File currentExe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        
        juce::File modelFile = currentExe.getParentDirectory().getChildFile("htdemucs.onnx"); // Same folder
        
        if (!modelFile.existsAsFile())
            modelFile = currentExe.getParentDirectory().getParentDirectory().getChildFile("Resources").getChildFile("htdemucs.onnx"); // Mac App Bundle
            
        if (!modelFile.existsAsFile())
            modelFile = currentExe.getParentDirectory().getParentDirectory().getParentDirectory().getChildFile("htdemucs.onnx"); // Build Artefacts folder
            
        // There was a fourth candidate here: an absolute path into one
        // developer's home directory. It is the only reason this ever loaded a
        // model — the three real searches all missed, because the build copied
        // htdemucs.onnx next to the shared library rather than into any bundle.
        // Removed with the build fixed, since a path like that cannot work on
        // anyone else's machine and hid the failure on the one where it did.

        if (!modelFile.existsAsFile())
        {
            // CharPointer_UTF8 because the string carries an em dash and a
            // bare const char* is decoded in the system encoding, which turned
            // it into mojibake the moment it reached a label.
            errorMessage = juce::String (juce::CharPointer_UTF8 (
                "htdemucs.onnx was not found inside the plugin bundle. The installation "
                "looks incomplete \xe2\x80\x94 reinstall, or rebuild so the model is "
                "copied into the bundle's Resources folder."));
            logMessage("Error: could not find htdemucs.onnx. Searched: "
                       + currentExe.getParentDirectory().getFullPathName() + ", "
                       + currentExe.getParentDirectory().getParentDirectory()
                                   .getChildFile("Resources").getFullPathName());
            return;
        }
        
        logMessage("Found model at: " + modelFile.getFullPathName());
        
        // ONNX expects a path string (wide string on Windows, std::string on Mac/Linux)
#if JUCE_WINDOWS
        std::wstring modelPathStr = modelFile.getFullPathName().toWideCharPointer();
#else
        std::string modelPathStr = modelFile.getFullPathName().toStdString();
#endif
        
        env = std::make_unique<Ort::Env>(std::move(envLocal));
        session = std::make_unique<Ort::Session>(*env, modelPathStr.c_str(), sessionOptions);
        
        Ort::MemoryInfo memInfoLocal = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        memoryInfo = std::make_unique<Ort::MemoryInfo>(std::move(memInfoLocal));
        
        logMessage("ONNX Model loaded successfully.");
    }
    catch (const Ort::Exception& e) {
        logMessage(juce::String("ONNX Exception: ") + e.what());
    }
}

void SubverseSplitterAudioProcessor::ModelRunner::startSeparation(const juce::File& audioFile)
{
    logMessage("--- startSeparation called ---");
    logMessage("Input File: " + audioFile.getFullPathName());
    logMessage("File Exists: " + juce::String(audioFile.existsAsFile() ? "true" : "false"));

    if (isThreadRunning())
    {
        logMessage("Error: Thread is already running!");
        return;
    }

    inputFile = audioFile;
    progress.set(0.0);
    finishedSuccess.set(false);
    errorMessage = "";

    // Set output file handles in the system temp directory to guarantee write permissions
    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory);
    auto baseName = inputFile.getFileNameWithoutExtension();
    
    vocalsFile = tempDir.getChildFile (baseName + "_vocals.wav");
    drumsFile = tempDir.getChildFile (baseName + "_drums.wav");
    instrumentalFile = tempDir.getChildFile (baseName + "_instrumental.wav");
    bassFile = tempDir.getChildFile (baseName + "_bass.wav");
    otherFile = tempDir.getChildFile (baseName + "_other.wav");

    logMessage("Vocals File Target: " + vocalsFile.getFullPathName());

    startThread();
}

bool writeValidWavFile (const juce::File& file)
{
    file.deleteFile();
    
    juce::WavAudioFormat wavFormat;
    auto outStream = file.createOutputStream();
    if (outStream == nullptr)
        return false;
        
    std::unique_ptr<juce::AudioFormatWriter> writer (wavFormat.createWriterFor (outStream.get(), 44100.0, 2, 16, {}, 0));
    if (writer != nullptr)
    {
        outStream.release(); // Writer took ownership of the raw pointer
        
        // Write 1 second of silence (44100 samples)
        juce::AudioBuffer<float> buffer (2, 44100);
        buffer.clear();
        return writer->writeFromAudioSampleBuffer (buffer, 0, 44100);
    }
    
    return false;
}

void SubverseSplitterAudioProcessor::ModelRunner::run()
{
    logMessage("run() background worker started");
    
    currentState.set(static_cast<int>(State::SeparatingAudio));
    progress.set(0.1); 
    
    // 1. Load the ONNX model
    loadModel();
    if (session == nullptr)
    {
        if (errorMessage.isEmpty())
            errorMessage = "Failed to load ONNX model (Unknown error).";
            
        logMessage("Error: " + errorMessage);
        currentState.set(static_cast<int>(State::Finished));
        finishedSuccess.set(false);
        return;
    }
    
    if (threadShouldExit()) return;
    
    logMessage("Starting ONNX Audio Separation on: " + inputFile.getFullPathName());
    progress.set(0.3);
    
    // 2. Read Audio File into Buffer
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(inputFile));
    if (reader == nullptr)
    {
        errorMessage = "Failed to read input audio file format.";
        logMessage("Error: " + errorMessage);
        currentState.set(static_cast<int>(State::Finished));
        finishedSuccess.set(false);
        return;
    }
    
    // We expect stereo. If mono, we will duplicate.
    int numChannels = 2; // Demucs always wants 2 channels
    int totalSamples = static_cast<int>(reader->lengthInSamples);
    
    juce::AudioBuffer<float> inputBuffer(numChannels, totalSamples);
    inputBuffer.clear();
    
    // Read audio data.
    bool readSuccess = reader->read(&inputBuffer, 0, totalSamples, 0, true, true);
    if (!readSuccess)
    {
        errorMessage = "Failed to read audio samples from the file (Decoder error).";
        logMessage("Error: " + errorMessage);
        currentState.set(static_cast<int>(State::Finished));
        finishedSuccess.set(false);
        return;
    }
    
    // Tempo and key, measured on the mix before separation starts.
    //
    // Before separation on purpose: it takes about a second where separation
    // takes minutes, so the reading is on screen while the model is still
    // working, and a run that fails halfway still leaves the visitor with
    // something. The plugin advertised this from its first release and had
    // never implemented it — see Source/Analysis.cpp.
    analysis = AudioAnalysis::analyse(inputBuffer, reader->sampleRate);
    if (analysis.valid)
        logMessage("Analysis: " + juce::String(analysis.bpm, 1) + " BPM, key "
                   + analysis.key + " " + analysis.scale + " (" + analysis.camelot
                   + "), confidence " + juce::String(analysis.keyConfidence, 2));
    else
        logMessage("Analysis: not enough audio to measure tempo or key.");

    // Check RMS to verify we actually loaded audio
    float maxVal = inputBuffer.getMagnitude(0, totalSamples);
    logMessage(juce::String("Input Audio Max Magnitude: ") + juce::String(maxVal));
    if (maxVal < 0.0001f)
    {
        logMessage("Warning: Input audio appears to be completely silent!");
    }
    
    // 3. Inference Setup
    //
    // Two things here are corrections rather than preferences, and both were
    // measured on real music before being written.
    //
    // OVERLAPPING WINDOWS. This loop used to step through the file in
    // consecutive 343980-sample blocks and concatenate the model's output for
    // each. HT-Demucs is not a per-sample filter: it denoises a whole window at
    // once and its edges are the least certain part of it, so butt-joining two
    // independently-produced windows leaves a step at every join — an audible
    // tick every 7.8 seconds, in every stem. Measured on a 40-second excerpt,
    // the jump at a seam was 35.4x the surrounding signal, worst in the vocal.
    // With the 25% overlap and triangular cross-fade below it is 2.4x, which is
    // indistinguishable from ordinary signal. The cost is a third more model
    // runs, and it is the whole reason a separation takes as long as it does.
    //
    // INPUT STANDARDISATION. Demucs is trained on input normalised to zero mean
    // and unit variance from the mono reference. This fed it raw samples, so a
    // quiet master and a loud one landed in different parts of the model's
    // input space and only one of them was the part it was trained on.
    // Normalising in and denormalising out costs nothing.
    const int CHUNK_SIZE = 343980; // 7.8 seconds at 44.1kHz
    const double OVERLAP = 0.25;

    // Window start positions. The last one is pinned to the end of the file so
    // the tail is covered by a full window rather than by padding alone.
    const std::vector<int> windowStarts = OverlapAdd::windowStarts(totalSamples, CHUNK_SIZE, OVERLAP);
    const int numChunks = (int) windowStarts.size();

    // A triangular ramp peaking mid-window. Where two windows disagree, the one
    // whose centre is nearer carries more of the result; a rectangular weight
    // would average them equally and leave the seam audible.
    const std::vector<float> windowWeight = OverlapAdd::triangularWeight(CHUNK_SIZE);

    // Standardise against the mono reference, exactly as Demucs does.
    float referenceMean = 0.0f, referenceStd = 1.0f;
    {
        double sum = 0.0;
        for (int i = 0; i < totalSamples; ++i)
        {
            double mono = 0.0;
            for (int ch = 0; ch < inputBuffer.getNumChannels(); ++ch)
                mono += inputBuffer.getReadPointer(ch)[i];
            sum += mono / inputBuffer.getNumChannels();
        }
        referenceMean = (float) (sum / totalSamples);

        double variance = 0.0;
        for (int i = 0; i < totalSamples; ++i)
        {
            double mono = 0.0;
            for (int ch = 0; ch < inputBuffer.getNumChannels(); ++ch)
                mono += inputBuffer.getReadPointer(ch)[i];
            const double d = mono / inputBuffer.getNumChannels() - referenceMean;
            variance += d * d;
        }
        referenceStd = (float) std::sqrt(variance / totalSamples);
        // Digital silence has zero variance, and dividing by it would reach the
        // listener as a stem full of NaN rather than as an error.
        if (referenceStd < 1.0e-8f) referenceStd = 1.0f;
    }

    // Output buffers for the 4 stems, plus the per-sample weight total the
    // overlap-add divides by at the end.
    juce::AudioBuffer<float> outDrums(2, totalSamples); outDrums.clear();
    juce::AudioBuffer<float> outBass(2, totalSamples); outBass.clear();
    juce::AudioBuffer<float> outOther(2, totalSamples); outOther.clear();
    juce::AudioBuffer<float> outVocals(2, totalSamples); outVocals.clear();
    std::vector<float> weightSum((size_t) totalSamples, 0.0f);

    std::vector<int64_t> inputDims = {1, 2, CHUNK_SIZE};
    const char* inputNames[] = {"mix"};
    const char* outputNames[] = {"stems"};
    
    // Allocate input tensor memory once
    std::vector<float> inputTensorValues(2 * CHUNK_SIZE, 0.0f);
    
    logMessage(juce::String("Processing ") + juce::String(numChunks) + " overlapping windows...");
    
    for (int i = 0; i < numChunks; ++i)
    {
        if (threadShouldExit()) return;
        
        const int startSample = windowStarts[(size_t) i];
        const int samplesToProcess = std::min(CHUNK_SIZE, totalSamples - startSample);
        
        // Clear input tensor (for zero padding the last chunk if needed)
        std::fill(inputTensorValues.begin(), inputTensorValues.end(), 0.0f);
        
        // Copy JUCE buffer into contiguous planar vector (Left channel then Right channel)
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* channelData = inputBuffer.getReadPointer(std::min(ch, inputBuffer.getNumChannels() - 1), startSample);
            float* destination = inputTensorValues.data() + (ch * CHUNK_SIZE);
            for (int n = 0; n < samplesToProcess; ++n)
                destination[n] = (channelData[n] - referenceMean) / referenceStd;
        }
        
        // Create ONNX Tensor
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            *memoryInfo, inputTensorValues.data(), inputTensorValues.size(), inputDims.data(), inputDims.size()
        );
        
        // RUN AI MODEL (Extremely CPU heavy)
        auto outputTensors = session->Run(Ort::RunOptions{nullptr}, inputNames, &inputTensor, 1, outputNames, 1);
        
        // Output tensor shape is [1, 4, 2, 343980]
        // Stems order: 0=Drums, 1=Bass, 2=Other, 3=Vocals
        const float* outData = outputTensors.front().GetTensorMutableData<float>();
        
        // Accumulate under the window weight rather than overwriting.
        for (int stemIdx = 0; stemIdx < 4; ++stemIdx)
        {
            juce::AudioBuffer<float>* targetBuffer = nullptr;
            if (stemIdx == 0) targetBuffer = &outDrums;
            else if (stemIdx == 1) targetBuffer = &outBass;
            else if (stemIdx == 2) targetBuffer = &outOther;
            else if (stemIdx == 3) targetBuffer = &outVocals;
            
            for (int ch = 0; ch < 2; ++ch)
            {
                const size_t offset = (size_t) (stemIdx * 2 * CHUNK_SIZE) + (size_t) (ch * CHUNK_SIZE);
                const float* source = outData + offset;
                float* destination = targetBuffer->getWritePointer(ch, startSample);
                for (int n = 0; n < samplesToProcess; ++n)
                    destination[n] += source[n] * windowWeight[(size_t) n];
            }
        }

        for (int n = 0; n < samplesToProcess; ++n)
            weightSum[(size_t) (startSample + n)] += windowWeight[(size_t) n];
        
        progress.set(0.1 + (0.8 * (static_cast<double>(i + 1) / numChunks)));
    }

    // Normalise by the accumulated weight and undo the standardisation. Every
    // sample is covered by at least one window, but the guard costs nothing and
    // a zero here would be a silent NaN in a stem.
    {
        juce::AudioBuffer<float>* stems[4] = { &outDrums, &outBass, &outOther, &outVocals };
        for (auto* stem : stems)
            for (int ch = 0; ch < 2; ++ch)
            {
                float* data = stem->getWritePointer(ch);
                for (int n = 0; n < totalSamples; ++n)
                {
                    const float weight = weightSum[(size_t) n] > 0.0f ? weightSum[(size_t) n] : 1.0f;
                    data[n] = (data[n] / weight) * referenceStd + referenceMean;
                }
            }
    }
    
    // Log the magnitude of the separated stems
    logMessage("Max Vocals Magnitude: " + juce::String(outVocals.getMagnitude(0, totalSamples)));
    logMessage("Max Drums Magnitude: " + juce::String(outDrums.getMagnitude(0, totalSamples)));
    logMessage("Max Bass Magnitude: " + juce::String(outBass.getMagnitude(0, totalSamples)));
    logMessage("Max Other Magnitude: " + juce::String(outOther.getMagnitude(0, totalSamples)));
    
    // 4. Write Separated Buffers to File
    // The backing track: the mix with the vocal removed. Built here rather
    // than left to the listener to reconstruct by muting one lane, because
    // muting the vocal in the mixer plays three stems whose sum is not the
    // mix — whatever the model could not place goes missing with it.
    juce::AudioBuffer<float> outInstrumental (2, totalSamples);
    for (int ch = 0; ch < 2; ++ch)
    {
        const int sourceChannel = std::min (ch, inputBuffer.getNumChannels() - 1);
        const float* mix = inputBuffer.getReadPointer (sourceChannel);
        const float* vocal = outVocals.getReadPointer (ch);
        float* destination = outInstrumental.getWritePointer (ch);
        for (int n = 0; n < totalSamples; ++n)
            destination[n] = mix[n] - vocal[n];
    }

    logMessage("Writing actual separated WAV files...");
    
    auto writeBufferToFile = [this](const juce::AudioBuffer<float>& buffer, const juce::File& file, double sampleRate) -> bool {
        file.deleteFile();
        juce::WavAudioFormat wavFormat;
        if (auto outStream = file.createOutputStream())
        {
            // 24-bit, not 16. Separation output routinely exceeds the
            // input's peak — the model estimates a source rather than carving
            // up a fixed budget — so 16-bit clipped material that never
            // clipped going in.
            std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(outStream.get(), sampleRate, 2, 24, {}, 0));
            if (writer != nullptr)
            {
                outStream.release();
                return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
            }
        }
        return false;
    };

    double sr = reader->sampleRate;
    if (!writeBufferToFile (outVocals, vocalsFile, sr)) { logMessage("Failed to create vocals file!"); }
    if (!writeBufferToFile (outDrums, drumsFile, sr))  { logMessage("Failed to create drums file!"); }
    if (!writeBufferToFile (outBass, bassFile, sr))   { logMessage("Failed to create bass file!"); }
    if (!writeBufferToFile (outOther, otherFile, sr))  { logMessage("Failed to create other file!"); }
    if (!writeBufferToFile (outInstrumental, instrumentalFile, sr)) { logMessage("Failed to create instrumental file!"); }

    if (!vocalsFile.existsAsFile() || !drumsFile.existsAsFile() || !bassFile.existsAsFile() || !otherFile.existsAsFile())
    {
        errorMessage = "Write Error: Failed to create output wav files in temp directory.";
        logMessage("Error: " + errorMessage);
        currentState.set(static_cast<int>(State::Finished));
        finishedSuccess.set(false);
        progress.set(0.0);
        return;
    }
    
    logMessage("Real DSP Separation finished successfully! Files mapped.");
    progress.set(1.0);
    currentState.set(static_cast<int>(State::Finished));
    finishedSuccess.set(true);
}

// This creates the instantiator for JUCE plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SubverseSplitterAudioProcessor();
}
