#pragma once

#include <JuceHeader.h>
#include <onnxruntime_cxx_api.h>

class SubverseSplitterAudioProcessor  : public juce::AudioProcessor
{
public:
    SubverseSplitterAudioProcessor();
    ~SubverseSplitterAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Asynchronous AI Model Runner Thread
    class ModelRunner : public juce::Thread
    {
    public:
        enum class State {
            Idle,
            SeparatingAudio,
            Finished
        };

        ModelRunner(SubverseSplitterAudioProcessor& processor);
        ~ModelRunner() override;

        void startSeparation(const juce::File& audioFile);
        void run() override;

        double getProgress() const { return progress.get(); }
        State getState() const { return static_cast<State>(currentState.get()); }
        bool isRunning() const { return isThreadRunning(); }
        bool isFinishedSuccessfully() const { return finishedSuccess.get(); }
        juce::String getErrorMessage() const { return errorMessage; }

        juce::File getVocalsFile() const { return vocalsFile; }
        juce::File getDrumsFile() const { return drumsFile; }
        juce::File getBassFile() const { return bassFile; }
        juce::File getOtherFile() const { return otherFile; }

    private:
        void loadModel();
        
        SubverseSplitterAudioProcessor& owner;
        juce::File inputFile;
        
        // ONNX Runtime variables
        std::unique_ptr<Ort::Env> env;
        std::unique_ptr<Ort::Session> session;
        std::unique_ptr<Ort::MemoryInfo> memoryInfo;

        // Progress and State atomic variables
        juce::Atomic<int> currentState{ static_cast<int>(State::Idle) };
        juce::Atomic<double> progress{ 0.0 };
        juce::Atomic<bool> finishedSuccess{ false };
        juce::String errorMessage;

        // Output file handles
        juce::File vocalsFile;
        juce::File drumsFile;
        juce::File bassFile;
        juce::File otherFile;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModelRunner)
    };

    ModelRunner& getModelRunner() { return modelRunner; }
    
    // Playback Engine Controls
    void loadStemForPlayback(int stemIndex, const juce::File& file);
    void togglePlayPause();
    bool getIsPlaying() const { return isPlaying.load(); }
    void setStemMute(int stemIndex, bool shouldMute);
    bool getStemMute(int stemIndex) const;
    void setStemSolo(int stemIndex, bool shouldSolo);
    bool getStemSolo(int stemIndex) const;
    
    double getPlaybackPosition() const;
    double getTotalLength() const;

private:
    ModelRunner modelRunner;
    
    // Playback Engine
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSources[4];
    std::unique_ptr<juce::AudioTransportSource> transportSources[4];
    juce::MixerAudioSource mixerSource;
    
    std::atomic<bool> isPlaying { false };
    std::atomic<bool> stemMutes[4] { false, false, false, false };
    std::atomic<bool> stemSolos[4] { false, false, false, false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubverseSplitterAudioProcessor)
};
