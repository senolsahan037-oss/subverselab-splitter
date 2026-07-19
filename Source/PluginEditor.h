#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SubverseSplitterAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                              public juce::FileDragAndDropTarget,
                                              public juce::Timer,
                                              public juce::DragAndDropContainer
{
public:
    SubverseSplitterAudioProcessorEditor (SubverseSplitterAudioProcessor&);
    ~SubverseSplitterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // FileDragAndDropTarget overrides
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // Timer override for polling progress
    void timerCallback() override;

    // Custom Stem Panel Component
    class StemPanel : public juce::Component, public juce::Timer
    {
    public:
        StemPanel(const juce::String& name, const juce::File& file, juce::Colour themeColor, int index, SubverseSplitterAudioProcessor& processor, juce::DragAndDropContainer& container);
        ~StemPanel() override;
        
        void paint(juce::Graphics& g) override;
        void resized() override;
        
        void mouseEnter (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        
        void timerCallback() override;
        void generateWaveformPreview();
        
    private:
        juce::File targetFile;
        juce::Colour themeColor;
        int stemIndex;
        SubverseSplitterAudioProcessor& audioProcessor;
        juce::DragAndDropContainer& dragContainer;
        
        juce::TextButton muteButton { "M" };
        juce::TextButton soloButton { "S" };
        juce::TextButton exportButton { "Drag/Export" };
        
        std::vector<float> waveformData;
        float hoverAlpha { 0.0f };
        bool isHovering { false };
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemPanel)
    };

private:
    SubverseSplitterAudioProcessor& audioProcessor;
    juce::Image backgroundImage;
    juce::Image logoImage;
    juce::Image loadingWorkersImage;

    // Header Elements
    juce::Label loadedFileLabel;
    juce::TextButton openFolderButton { "Open Folder" };

    // Processing UI Elements
    juce::Label instructionLabel;
    juce::Label statusLabel;
    juce::TextButton selectAudioButton;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool isDraggingActive{ false };

    // Stems Panels
    std::unique_ptr<StemPanel> vocalsPanel;
    std::unique_ptr<StemPanel> drumsPanel;
    std::unique_ptr<StemPanel> bassPanel;
    std::unique_ptr<StemPanel> otherPanel;
    
    // Transport Bar Elements
    juce::TextButton playPauseButton { "Play" };
    
    // Animation state
    float pulsePhase { 0.0f };
    float loadingPhase { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubverseSplitterAudioProcessorEditor)
};
