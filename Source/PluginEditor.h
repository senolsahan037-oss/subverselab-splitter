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

    /** How tall the tempo/key strip is right now — zero until there is a
        reading to show. paint() and resized() both need the answer. */
    int analysisStripHeight() const;

    /** Where the scrub bar is. Drawing it and hit-testing it read the same
        rectangle, because the two drifting apart is exactly how the timeline
        ended up painting in one place and answering clicks in another. */
    juce::Rectangle<int> timelineBounds() const;

    // FileDragAndDropTarget overrides
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // Timer override for polling progress
    void timerCallback() override;

    // Scrubbing on the timeline. The bar drew the playhead from the first
    // release and did nothing when clicked, which reads as a broken control
    // rather than as a read-out.
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void seekFromMouse (juce::Point<int> position);

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

        /** The loudest point in this stem, before any display scaling. */
        float getPeak() const { return peak; }

        /** Multiplier the waveform is drawn at. Set from outside so all four
            stems share one scale: a vocal really is quieter than the drums, and
            normalising each row to its own peak would hide that while claiming
            to show it. */
        void setDisplayScale (float newScale) { displayScale = newScale; repaint(); }
        
    private:
        juce::File targetFile;
        juce::Colour themeColor;
        int stemIndex;
        SubverseSplitterAudioProcessor& audioProcessor;
        juce::DragAndDropContainer& dragContainer;
        
        juce::Slider volumeSlider { juce::Slider::LinearHorizontal,
                                    juce::Slider::NoTextBox };
        juce::TextButton muteButton { "M" };
        juce::TextButton soloButton { "S" };
        juce::TextButton exportButton { "Drag/Export" };
        
        std::vector<float> waveformData;
        float peak { 0.0f };
        float displayScale { 1.0f };
        float hoverAlpha { 0.0f };
        bool isHovering { false };
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StemPanel)
    };

private:
    SubverseSplitterAudioProcessor& audioProcessor;
    juce::Image logoImage;

    // Header Elements
    juce::Label loadedFileLabel;
    juce::TextButton openFolderButton { "Open Folder" };

    // Processing UI Elements
    juce::Label instructionLabel;
    juce::Label statusLabel;
    juce::TextButton selectAudioButton;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool isDraggingActive{ false };
    bool autoPlayWhenReady { false };   // set only by the SPLITTER_AUTOTEST hook
    int  lastStripHeight { -1 };        // so the strip's arrival triggers a re-layout

    // Stems Panels
    std::unique_ptr<StemPanel> vocalsPanel;
    std::unique_ptr<StemPanel> drumsPanel;
    std::unique_ptr<StemPanel> bassPanel;
    std::unique_ptr<StemPanel> otherPanel;
    
    // Transport Bar Elements
    juce::TextButton playPauseButton { "Play" };

    /*  The backing track — the mix with the vocal taken out.

        Offered here rather than as a fifth mixer lane, because it is not one.
        It already contains the drums, bass and other stems, so playing it
        alongside them would double everything but the vocal. It is something
        you take away, not something you balance against the rest, and it was
        being written to disk with nothing in the interface admitting it
        existed. */
    juce::TextButton instrumentalButton { "Instrumental" };
    
    // Animation state
    float pulsePhase { 0.0f };
    float loadingPhase { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubverseSplitterAudioProcessorEditor)
};
