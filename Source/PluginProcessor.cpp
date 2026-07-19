#include "PluginProcessor.h"
#include "PluginEditor.h"

SubverseSplitterAudioProcessor::SubverseSplitterAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
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

void SubverseSplitterAudioProcessor::setStemMute(int stemIndex, bool shouldMute)
{
    if (stemIndex >= 0 && stemIndex < 4)
    {
        stemMutes[stemIndex] = shouldMute;
        // Apply mute/solo logic
        for (int i = 0; i < 4; ++i)
        {
            if (transportSources[i] != nullptr)
            {
                bool isMuted = stemMutes[i];
                bool anySolo = stemSolos[0] || stemSolos[1] || stemSolos[2] || stemSolos[3];
                
                if (anySolo)
                {
                    transportSources[i]->setGain(stemSolos[i] ? 1.0f : 0.0f);
                }
                else
                {
                    transportSources[i]->setGain(isMuted ? 0.0f : 1.0f);
                }
            }
        }
    }
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
        // Trigger mute evaluation which handles solo logic
        setStemMute(stemIndex, stemMutes[stemIndex]); 
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
    juce::File logFile ("/Users/senolsahan/.gemini/antigravity-ide/scratch/SubverseSplitterLog.txt");
    logFile.appendText (message + "\n");
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
        sessionOptions.SetIntraOpNumThreads(4);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        
        // Find the model file (Search in multiple locations to support Standalone, VST, and Dev environments)
        juce::File currentExe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        
        juce::File modelFile = currentExe.getParentDirectory().getChildFile("htdemucs.onnx"); // Same folder
        
        if (!modelFile.existsAsFile())
            modelFile = currentExe.getParentDirectory().getParentDirectory().getChildFile("Resources").getChildFile("htdemucs.onnx"); // Mac App Bundle
            
        if (!modelFile.existsAsFile())
            modelFile = currentExe.getParentDirectory().getParentDirectory().getParentDirectory().getChildFile("htdemucs.onnx"); // Build Artefacts folder
            
        if (!modelFile.existsAsFile())
            modelFile = juce::File("/Users/senolsahan/.gemini/antigravity-ide/scratch/SubverseSplitterPlugin/Resources/htdemucs.onnx"); // Dev Hardcoded Fallback
            
        if (!modelFile.existsAsFile())
        {
            errorMessage = "Failed to load ONNX model. File not found.";
            logMessage("Error: Could not find htdemucs.onnx anywhere!");
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
    
    // Check RMS to verify we actually loaded audio
    float maxVal = inputBuffer.getMagnitude(0, totalSamples);
    logMessage(juce::String("Input Audio Max Magnitude: ") + juce::String(maxVal));
    if (maxVal < 0.0001f)
    {
        logMessage("Warning: Input audio appears to be completely silent!");
    }
    
    // 3. Inference Setup
    const int CHUNK_SIZE = 343980; // 7.8 seconds at 44.1kHz
    int numChunks = std::ceil(static_cast<float>(totalSamples) / CHUNK_SIZE);
    
    // Output buffers for the 4 stems
    juce::AudioBuffer<float> outDrums(2, totalSamples); outDrums.clear();
    juce::AudioBuffer<float> outBass(2, totalSamples); outBass.clear();
    juce::AudioBuffer<float> outOther(2, totalSamples); outOther.clear();
    juce::AudioBuffer<float> outVocals(2, totalSamples); outVocals.clear();
    
    std::vector<int64_t> inputDims = {1, 2, CHUNK_SIZE};
    const char* inputNames[] = {"mix"};
    const char* outputNames[] = {"stems"};
    
    // Allocate input tensor memory once
    std::vector<float> inputTensorValues(2 * CHUNK_SIZE, 0.0f);
    
    logMessage(juce::String("Processing ") + juce::String(numChunks) + " chunks...");
    
    for (int i = 0; i < numChunks; ++i)
    {
        if (threadShouldExit()) return;
        
        int startSample = i * CHUNK_SIZE;
        int samplesToProcess = std::min(CHUNK_SIZE, totalSamples - startSample);
        
        // Clear input tensor (for zero padding the last chunk if needed)
        std::fill(inputTensorValues.begin(), inputTensorValues.end(), 0.0f);
        
        // Copy JUCE buffer into contiguous planar vector (Left channel then Right channel)
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* channelData = inputBuffer.getReadPointer(std::min(ch, inputBuffer.getNumChannels() - 1), startSample);
            std::copy(channelData, channelData + samplesToProcess, inputTensorValues.begin() + (ch * CHUNK_SIZE));
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
        
        // Copy data back to JUCE output buffers
        for (int stemIdx = 0; stemIdx < 4; ++stemIdx)
        {
            juce::AudioBuffer<float>* targetBuffer = nullptr;
            if (stemIdx == 0) targetBuffer = &outDrums;
            else if (stemIdx == 1) targetBuffer = &outBass;
            else if (stemIdx == 2) targetBuffer = &outOther;
            else if (stemIdx == 3) targetBuffer = &outVocals;
            
            for (int ch = 0; ch < 2; ++ch)
            {
                // Calculate offset in the flat [1, 4, 2, 343980] array
                size_t offset = (stemIdx * 2 * CHUNK_SIZE) + (ch * CHUNK_SIZE);
                targetBuffer->copyFrom(ch, startSample, outData + offset, samplesToProcess);
            }
        }
        
        progress.set(0.1 + (0.8 * (static_cast<double>(i + 1) / numChunks)));
    }
    
    // Log the magnitude of the separated stems
    logMessage("Max Vocals Magnitude: " + juce::String(outVocals.getMagnitude(0, totalSamples)));
    logMessage("Max Drums Magnitude: " + juce::String(outDrums.getMagnitude(0, totalSamples)));
    logMessage("Max Bass Magnitude: " + juce::String(outBass.getMagnitude(0, totalSamples)));
    logMessage("Max Other Magnitude: " + juce::String(outOther.getMagnitude(0, totalSamples)));
    
    // 4. Write Separated Buffers to File
    logMessage("Writing actual separated WAV files...");
    
    auto writeBufferToFile = [this](const juce::AudioBuffer<float>& buffer, const juce::File& file, double sampleRate) -> bool {
        file.deleteFile();
        juce::WavAudioFormat wavFormat;
        if (auto outStream = file.createOutputStream())
        {
            std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(outStream.get(), sampleRate, 2, 16, {}, 0));
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
