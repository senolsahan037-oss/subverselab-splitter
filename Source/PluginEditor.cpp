#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "Theme.h"

// ==============================================================================
// MAIN EDITOR CLASS
// ==============================================================================

SubverseSplitterAudioProcessorEditor::SubverseSplitterAudioProcessorEditor (SubverseSplitterAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Header setup
    loadedFileLabel.setText ("No File Loaded", juce::dontSendNotification);
    loadedFileLabel.setFont (juce::Font (juce::FontOptions (14.0f)));
    loadedFileLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (loadedFileLabel);
    
    openFolderButton.setColour (juce::TextButton::buttonColourId, Theme::surfaceRaised);
    openFolderButton.onClick = []() {
        juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        if (tempDir.exists()) tempDir.revealToUser();
    };
    addAndMakeVisible(openFolderButton);

    // Initial Drag & Drop state
    instructionLabel.setText ("Drag & Drop Audio to Separate", juce::dontSendNotification);
    instructionLabel.setFont (juce::Font (juce::FontOptions (20.0f).withStyle("Bold")));
    instructionLabel.setJustificationType (juce::Justification::centred);
    instructionLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (instructionLabel);

    statusLabel.setText ("", juce::dontSendNotification);
    statusLabel.setFont (juce::Font (juce::FontOptions (14.0f).withStyle("Bold")));
    statusLabel.setJustificationType (juce::Justification::centred);
    // An error message is a sentence, not a word. Without this the label
    // squeezes it onto one line until it is unreadable rather than wrapping.
    statusLabel.setMinimumHorizontalScale (1.0f);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (statusLabel);

    selectAudioButton.setButtonText("Browse Files...");
    selectAudioButton.setColour(juce::TextButton::buttonColourId, Theme::surfaceHigh);
    selectAudioButton.onClick = [this]() {
        fileChooser = std::make_unique<juce::FileChooser>("Select Audio File", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.wav;*.mp3;*.aiff;*.aif;*.flac");
        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
            auto result = fc.getResult();
            if (result.existsAsFile()) {
                juce::StringArray files;
                files.add(result.getFullPathName());
                filesDropped(files, 0, 0); 
            }
        });
    };
    addAndMakeVisible(selectAudioButton);

    // Transport Bar
    playPauseButton.setColour(juce::TextButton::buttonColourId, Theme::gold); // Teal
    playPauseButton.onClick = [this]() {
        audioProcessor.togglePlayPause();
    };
    addAndMakeVisible(playPauseButton);

    instrumentalButton.setColour (juce::TextButton::buttonColourId, Theme::surfaceHigh);
    instrumentalButton.setColour (juce::TextButton::textColourOffId, Theme::text);
    instrumentalButton.onClick = [this]() {
        const auto file = audioProcessor.getModelRunner().getInstrumentalFile();
        if (file.existsAsFile())
            file.revealToUser();          // shows it in Finder, selected
    };
    addAndMakeVisible(instrumentalButton);

    // The header mark. Two other images were loaded here and never drawn.
    int logoSize = 0;
    if (const char* logoData = BinaryData::getNamedResource("logo_png", logoSize))
        logoImage = juce::ImageCache::getFromMemory(logoData, logoSize);

    setSize (1000, 780);
    startTimer(50); // 20 FPS refresh for UI timeline

    // Debug hook: SPLITTER_AUTOTEST=<file> separates that file on launch and
    // starts playback when it finishes. It exists because a bug that only
    // appears in the Standalone cannot be chased from a console harness — the
    // harness reproduces the code, not the wrapper around it — and driving the
    // real window by clicking it is far less reliable than this.
    if (auto* autoTest = std::getenv ("SPLITTER_AUTOTEST"))
    {
        const juce::File file { juce::String (autoTest) };
        if (file.existsAsFile())
        {
            juce::StringArray files;
            files.add (file.getFullPathName());
            juce::MessageManager::callAsync ([this, files] { filesDropped (files, 0, 0); });
            autoPlayWhenReady = true;
        }
    }
}

SubverseSplitterAudioProcessorEditor::~SubverseSplitterAudioProcessorEditor()
{
}

juce::Rectangle<int> SubverseSplitterAudioProcessorEditor::timelineBounds() const
{
    const int left = 150, right = getWidth() - 210;
    return { left, getHeight() - 32, juce::jmax (0, right - left), 6 };
}

int SubverseSplitterAudioProcessorEditor::analysisStripHeight() const
{
    // Only occupies space once there is something to put in it. Before a file
    // is loaded the drop target should have the whole window.
    return audioProcessor.getModelRunner().getAnalysis().valid ? 58 : 0;
}

void SubverseSplitterAudioProcessorEditor::paint (juce::Graphics& g)
{
    // The brand's dark green, lifted slightly at the top. It was a navy
    // gradient — the one colour the SubverseLab palette does not contain.
    juce::ColourGradient bgGradient(Theme::surface, 0.0f, 0.0f,
                                    Theme::background, 0.0f, (float)getHeight(), false);
    g.setGradientFill(bgGradient);
    g.fillRect(getLocalBounds());
    
    // Draw Header separator line
    g.setColour(Theme::surface.withAlpha(0.5f));
    g.drawLine(0.0f, 50.0f, (float)getWidth(), 50.0f, 1.0f);
    
    // Draw Logo in header (centered)
    if (logoImage.isValid())
    {
        // Calculate center X of the header (which is getWidth() / 2)
        // Header height is 50. So center Y is 25.
        g.drawImageWithin(logoImage, getWidth() / 2 - 20, 5, 40, 40, juce::RectanglePlacement::centred);
    }
    
    // Tempo and key, in a strip of their own under the header.
    //
    // These were drawn into the header's left corner, underneath
    // loadedFileLabel — a child component that paints after its parent, over
    // the same 300 pixels. The reading was there the whole time and nobody
    // could see it. Four labelled cells below the header instead, matching the
    // row the web Splitter puts above its mixer.
    if (analysisStripHeight() > 0)
    {
        const auto analysis = audioProcessor.getModelRunner().getAnalysis();
        auto strip = getLocalBounds().withTrimmedTop (50).withHeight (analysisStripHeight());

        g.setColour (Theme::surface);
        g.fillRect (strip);
        g.setColour (Theme::line);
        g.drawLine ((float) strip.getX(), (float) strip.getBottom(),
                    (float) strip.getRight(), (float) strip.getBottom(), 1.0f);

        const juce::String captions[4] = { "TEMPO", "KEY", "CAMELOT", "KEY CONFIDENCE" };
        const juce::String values[4] = {
            juce::String (analysis.bpm, 1) + " BPM",
            analysis.key + " " + (analysis.scale == "min" ? "minor" : "major"),
            analysis.camelot,
            juce::String (juce::roundToInt (analysis.keyConfidence * 100.0f)) + "%"
        };

        auto cells = strip.reduced (20, 10);
        const int cellWidth = cells.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto cell = cells.removeFromLeft (i == 3 ? cells.getWidth() : cellWidth);

            g.setColour (Theme::textMuted);
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText (captions[i], cell.removeFromTop (14), juce::Justification::centredLeft);

            // The tempo is the one a DJ types into a deck, so it is the one in
            // the accent colour; the rest read as supporting detail.
            g.setColour (i == 0 ? Theme::gold : Theme::text);
            g.setFont (juce::Font (juce::FontOptions (19.0f).withStyle ("Bold")));
            g.drawText (values[i], cell, juce::Justification::centredLeft);
        }
    }

    // Draw Transport Bar separator line
    g.setColour(Theme::surface.withAlpha(0.5f));
    g.drawLine(0.0f, (float)(getHeight() - 60), (float)getWidth(), (float)(getHeight() - 60), 1.0f);
    
    // Draw timeline if loaded
    if (vocalsPanel != nullptr)
    {
        double pos = audioProcessor.getPlaybackPosition();
        double len = audioProcessor.getTotalLength();
        if (len > 0.0)
        {
            const float progress = juce::jlimit (0.0f, 1.0f, (float) (pos / len));

            // A scrub bar you can see and aim at. It was a four-pixel dark
            // strip whose filled portion is zero-width at the start of a track,
            // so at the moment you most want to find it there was nothing on
            // screen at all.
            auto formatTime = [](double seconds) {
                int m = static_cast<int>(seconds) / 60;
                int s = static_cast<int>(seconds) % 60;
                return juce::String::formatted("%02d:%02d", m, s);
            };

            const auto track = timelineBounds();
            const int trackLeft = track.getX();
            const int trackRight = track.getRight();
            const float trackY = (float) track.getY();
            const float trackH = (float) track.getHeight();
            const float trackW = (float) track.getWidth();

            g.setColour (Theme::surfaceHigh);
            g.fillRoundedRectangle ((float) trackLeft, trackY, trackW, trackH, trackH / 2.0f);

            g.setColour (Theme::gold);
            g.fillRoundedRectangle ((float) trackLeft, trackY, trackW * progress, trackH, trackH / 2.0f);

            // The handle is what says "this is draggable"; a filled bar alone
            // reads as a progress indicator.
            const float handleX = trackLeft + trackW * progress;
            g.setColour (Theme::text);
            g.fillEllipse (handleX - 6.0f, trackY + trackH / 2.0f - 6.0f, 12.0f, 12.0f);

            g.setColour (Theme::textMuted);
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawText (formatTime (pos), trackLeft - 62, (int) trackY - 7, 54, 20,
                        juce::Justification::centredRight);
            g.drawText (formatTime (len), trackRight + 8, (int) trackY - 7, 54, 20,
                        juce::Justification::centredLeft);
        }
    }

    // Drag overlay
    if (vocalsPanel == nullptr)
    {
        // The strip's height, not a constant 50: while a separation runs the
        // tempo/key reading is already on screen, and a card measured from the
        // header alone slid up underneath it.
        auto bounds = getLocalBounds()
                        .withTrimmedTop (50 + analysisStripHeight())
                        .withTrimmedBottom (60)
                        .reduced (20);
        
        g.setColour(Theme::surface.withAlpha(0.7f));
        g.fillRoundedRectangle(bounds.toFloat(), 12.0f);
        
        if (isDraggingActive)
        {
            g.setColour(Theme::teal);
            g.drawRoundedRectangle(bounds.toFloat(), 12.0f, 2.0f);
        }
        else
        {
            g.setColour(Theme::surfaceHigh);
            g.drawRoundedRectangle(bounds.toFloat(), 12.0f, 1.0f);
        }
        
        // Processing progress bar
        if (audioProcessor.getModelRunner().isRunning())
        {
            auto imgRect = bounds.withSizeKeepingCentre(800, 400).translated(0, -60).toFloat();
            
            // Real-time sleek mathematical waveform animation (Richer)
            float timeSecs = (float)juce::Time::getMillisecondCounterHiRes() / 1000.0f;
            float waveCenterY = imgRect.getCentreY();
            float waveWidth = imgRect.getWidth();
            float waveStartX = imgRect.getX();
            
            struct WaveConfig { juce::Colour color; float speed1; float speed2; float freq1; float freq2; float ampMult; float thickness; };
            std::vector<WaveConfig> waves = {
                { Theme::stemVocals,  2.0f, -1.5f, 10.0f,  18.0f, 80.0f, 2.0f }, // Orange
                { Theme::stemDrums, -2.5f,  3.0f, 12.0f,  22.0f, 110.0f, 3.0f }, // Teal
                { Theme::stemBass,  3.5f, -2.0f, 15.0f,  25.0f, 95.0f, 2.5f }, // Light Blue
                { Theme::stemOther, -1.8f,  2.8f,  8.0f,  20.0f, 65.0f, 1.5f }  // Indigo
            };
            
            for (const auto& config : waves)
            {
                juce::Path wavePath;
                wavePath.startNewSubPath(waveStartX, waveCenterY);
                
                for (float x = 0; x <= waveWidth; x += 3.0f)
                {
                    float normalizedX = x / waveWidth;
                    // Smooth envelope so it tapers smoothly at the edges (sine from 0 to pi)
                    float envelope = std::sin(normalizedX * juce::MathConstants<float>::pi); 
                    // Add a tiny bit of power to the envelope to make the center punchier
                    envelope = std::pow(envelope, 0.8f);
                    
                    float amp1 = std::sin(timeSecs * config.speed1 + normalizedX * config.freq1) * config.ampMult;
                    float amp2 = std::sin(timeSecs * config.speed2 - normalizedX * config.freq2) * (config.ampMult * 0.5f);
                    
                    float y = waveCenterY + (amp1 + amp2) * envelope;
                    wavePath.lineTo(waveStartX + x, y);
                }
                
                g.setColour(config.color.withAlpha(0.6f));
                g.strokePath(wavePath, juce::PathStrokeType(config.thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            
            auto progress = audioProcessor.getModelRunner().getProgress();
            auto barArea = bounds.withSizeKeepingCentre(400, 20).translated(0, 190).toFloat();
            
            g.setColour(Theme::surface); // Dark gray/navy background for progress bar
            g.fillRoundedRectangle(barArea, 10.0f);
            
            g.setColour(Theme::gold); // Teal to match theme
            g.fillRoundedRectangle(barArea.withWidth(barArea.getWidth() * static_cast<float>(progress)), 10.0f);
            
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
            g.drawText(juce::String(static_cast<int>(progress * 100)) + "%", 
                       barArea, 
                       juce::Justification::centred);
        }
    }
}

void SubverseSplitterAudioProcessorEditor::resized()
{
    // Header Layout
    auto headerArea = getLocalBounds().removeFromTop(50);
    
    // Loaded file label on the left
    loadedFileLabel.setBounds(headerArea.removeFromLeft(300).withTrimmedLeft(20).reduced(0, 10));
    
    headerArea.removeFromRight(10);
    openFolderButton.setBounds(headerArea.removeFromRight(120).reduced(0, 10));

    // Transport Bar Layout
    auto transportArea = getLocalBounds().removeFromBottom(60);
    transportArea.removeFromTop(4); // Timeline space
    
    playPauseButton.setBounds(transportArea.removeFromLeft(120).reduced(10, 10));
    instrumentalButton.setBounds(transportArea.removeFromRight(130).reduced(10, 12));

    // Center Panels, below the header and the tempo/key strip. The strip
    // measures zero until there is a reading, so an empty window still gives
    // the drop target everything between header and transport.
    auto centerArea = getLocalBounds()
                        .withTrimmedTop (50 + analysisStripHeight())
                        .withTrimmedBottom (60)
                        .reduced (10);

    bool showStems = (vocalsPanel != nullptr);
    playPauseButton.setVisible(showStems);
    instrumentalButton.setVisible(showStems);
    
    if (showStems)
    {
        instructionLabel.setVisible(false);
        selectAudioButton.setVisible(false);
        statusLabel.setVisible(false); // We have a proper badge now
        
        int panelHeight = centerArea.getHeight() / 4;
        vocalsPanel->setBounds(centerArea.removeFromTop(panelHeight).reduced(5));
        drumsPanel->setBounds(centerArea.removeFromTop(panelHeight).reduced(5));
        bassPanel->setBounds(centerArea.removeFromTop(panelHeight).reduced(5));
        otherPanel->setBounds(centerArea.reduced(5));
    }
    else
    {
        bool isRunning = audioProcessor.getModelRunner().isRunning();
        instructionLabel.setVisible(!isRunning);
        selectAudioButton.setVisible(!isRunning);
        statusLabel.setVisible(true);
        
        if (isRunning)
        {
            // The bar is drawn at +190 from the card's centre; this sits above
            // it rather than across it. Both are measured from the same card.
            statusLabel.setBounds (centerArea.withSizeKeepingCentre (400, 24).translated (0, 152));
        }
        else
        {
            instructionLabel.setBounds(centerArea.withSizeKeepingCentre(400, 40).translated(0, -30));
            selectAudioButton.setBounds(centerArea.withSizeKeepingCentre(150, 40).translated(0, 30));

            // Laid out, not cleared. This branch used to wipe the label's text,
            // and since timerCallback reports a failure by setting that text and
            // then calling resized(), the message was erased in the same frame it
            // was written — a separation that failed looked exactly like one that
            // had never been asked for. Deciding what the label says is the
            // caller's job; resized() only decides where it sits.
            statusLabel.setBounds(centerArea.withSizeKeepingCentre(560, 60).translated(0, 96));
        }
    }
}

bool SubverseSplitterAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (audioProcessor.getModelRunner().isRunning()) return false;
    for (auto& file : files)
    {
        if (file.contains ("_vocals") || file.contains ("_drums") || file.contains ("_bass") || file.contains ("_other")) return false;
        auto ext = juce::File(file).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".mp3" || ext == ".aif" || ext == ".aiff" || ext == ".flac") return true;
    }
    return false;
}

void SubverseSplitterAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int, int) { isDraggingActive = true; repaint(); }
void SubverseSplitterAudioProcessorEditor::fileDragMove (const juce::StringArray&, int, int) {}
void SubverseSplitterAudioProcessorEditor::fileDragExit (const juce::StringArray&) { isDraggingActive = false; repaint(); }

void SubverseSplitterAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    isDraggingActive = false;
    if (files.size() > 0)
    {
        juce::File audioFile (files[0]);
        loadedFileLabel.setText (audioFile.getFileName(), juce::dontSendNotification);

        // Clearing the previous run's error belongs here, where a new run
        // actually starts, rather than in resized().
        statusLabel.setColour (juce::Label::textColourId, Theme::textMuted);
        statusLabel.setText ("Analyzing...", juce::dontSendNotification);
        
        vocalsPanel.reset(); drumsPanel.reset(); bassPanel.reset(); otherPanel.reset();

        audioProcessor.getModelRunner().startSeparation (audioFile);
        resized();
    }
    repaint();
}

namespace StemLayout
{
    /*  One definition of the row, used by both paint() and resized().

        They disagreed before: resized() reserved 120 px on the right for the
        controls while paint() trimmed 180 px off the waveform, so every row
        carried a 60 px hole nobody had put anything in, and the four rows did
        not line up with each other. Widths that two functions have to agree on
        belong in one place.
    */
    constexpr int chip      = 4;      // the stem's colour, as a spine
    constexpr int chipGap   = 14;
    constexpr int name      = 104;
    constexpr int volume    = 132;
    constexpr int button    = 38;     // M and S
    constexpr int buttonGap = 6;
    constexpr int exportW   = 104;
    constexpr int gap       = 16;
    constexpr int padding   = 18;

    constexpr int controls  = volume + gap + (button * 2 + buttonGap) + gap + exportW;
}

namespace
{
    // The timeline is the four-pixel bar just above the transport, so the
    // grab area is widened to something a hand can actually hit.
    constexpr int kTimelineGrab = 14;
}

void SubverseSplitterAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    seekFromMouse (e.getPosition());
}

void SubverseSplitterAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    // Once the gesture has started on the bar, following the pointer off it is
    // what a scrub is — so the bounds check is against where the drag began.
    if (! timelineBounds().expanded (0, kTimelineGrab).contains (e.mouseDownPosition.toInt()))
        return;

    const auto track = timelineBounds();
    const double length = audioProcessor.getTotalLength();
    if (length <= 0.0 || track.getWidth() <= 0)
        return;

    audioProcessor.setPlaybackPosition (
        length * juce::jlimit (0.0, 1.0, (double) (e.x - track.getX()) / track.getWidth()));
    repaint();
}

void SubverseSplitterAudioProcessorEditor::seekFromMouse (juce::Point<int> position)
{
    const auto track = timelineBounds();
    if (vocalsPanel == nullptr || track.getWidth() <= 0)
        return;

    // Widened vertically, because a six-pixel target is not one a hand can hit.
    if (! track.expanded (0, kTimelineGrab).contains (position))
        return;

    const double length = audioProcessor.getTotalLength();
    if (length <= 0.0)
        return;

    audioProcessor.setPlaybackPosition (
        length * juce::jlimit (0.0, 1.0, (double) (position.x - track.getX()) / track.getWidth()));
    repaint();
}

void SubverseSplitterAudioProcessorEditor::timerCallback()
{
    // Update Play/Pause button text and timeline
    playPauseButton.setButtonText(audioProcessor.getIsPlaying() ? "PAUSE" : "PLAY");
    
    // Repaint to update timeline cursor
    if (audioProcessor.getIsPlaying()) repaint();

    auto& runner = audioProcessor.getModelRunner();

    // The tempo/key strip appears part-way through a separation, because the
    // analysis finishes in milliseconds while the model takes minutes. Its
    // arrival changes how much room everything below it has, and nothing was
    // telling the layout — so the strip painted straight over the drop card,
    // which is what "half-rendered in odd places" was.
    if (const int height = analysisStripHeight(); height != lastStripHeight)
    {
        lastStripHeight = height;
        resized();
    }

    if (runner.isRunning())
    {
        const float progress = (float) runner.getProgress();

        // The percentage belongs to the bar, which already draws it. A second
        // copy in this label landed on top of the first — two readings of the
        // same number, overlapping, neither legible.
        juce::ignoreUnused (progress);
        statusLabel.setText ("Separating stems", juce::dontSendNotification);
        repaint();
    }
    else if (runner.isFinishedSuccessfully() && vocalsPanel == nullptr)
    {
        // Set, not appended. Appending meant a second separation in the same
        // session produced "song.wav [Separation Complete] [Separation Complete]".
        loadedFileLabel.setText (runner.getVocalsFile().getFileNameWithoutExtension()
                                     .upToLastOccurrenceOf ("_vocals", false, false),
                                 juce::dontSendNotification);
        
        vocalsPanel = std::make_unique<StemPanel> ("Vocals", runner.getVocalsFile(), Theme::stemVocals, 0, audioProcessor, *this);
        drumsPanel = std::make_unique<StemPanel> ("Drums", runner.getDrumsFile(), Theme::stemDrums,  1, audioProcessor, *this);
        bassPanel = std::make_unique<StemPanel> ("Bass", runner.getBassFile(), Theme::stemBass,   2, audioProcessor, *this);
        otherPanel = std::make_unique<StemPanel> ("Other", runner.getOtherFile(), Theme::stemOther,  3, audioProcessor, *this);

        audioProcessor.loadStemForPlayback(0, runner.getVocalsFile());
        audioProcessor.loadStemForPlayback(1, runner.getDrumsFile());
        audioProcessor.loadStemForPlayback(2, runner.getBassFile());
        audioProcessor.loadStemForPlayback(3, runner.getOtherFile());

        addAndMakeVisible (vocalsPanel.get());
        addAndMakeVisible (drumsPanel.get());
        addAndMakeVisible (bassPanel.get());
        addAndMakeVisible (otherPanel.get());

        // Each stem drawn against its own peak, up to a limit.
        //
        // Sharing one scale across the four was tried first and is the more
        // obviously "honest" choice, but it answers a question nobody is asking
        // of these rows. Someone reads a stem waveform to find where the vocal
        // enters or where the drop is — navigation, not level; the level is
        // already in the fader beside it and in the audio itself. Under a
        // shared scale the loudest stem set it and the other three sat flat
        // against the bottom of a tall row, which showed a true fact by making
        // three of the four unreadable.
        //
        // The cap matters, though: an unbounded 1/peak would stretch a stem
        // holding almost nothing — the vocal lane of an instrumental — until it
        // looked as full as any other. Past 6x it stays small, which is the
        // right thing for it to say.
        StemPanel* panels[4] = { vocalsPanel.get(), drumsPanel.get(),
                                 bassPanel.get(), otherPanel.get() };
        for (auto* panel : panels)
        {
            const float stemPeak = panel->getPeak();
            panel->setDisplayScale (stemPeak > 0.0001f
                                        ? juce::jmin (6.0f, 1.0f / stemPeak)
                                        : 1.0f);
        }

        resized();
        repaint();

        if (autoPlayWhenReady)
        {
            autoPlayWhenReady = false;
            audioProcessor.togglePlayPause();
        }
    }
    else if (!runner.isRunning() && !runner.isFinishedSuccessfully() && runner.getErrorMessage().isNotEmpty())
    {
        statusLabel.setText (runner.getErrorMessage(), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, Theme::danger);
        resized();
        repaint();
    }
}

// ==============================================================================
// STEM PANEL IMPLEMENTATION
// ==============================================================================
SubverseSplitterAudioProcessorEditor::StemPanel::StemPanel(const juce::String& name, const juce::File& file, juce::Colour color, int index, SubverseSplitterAudioProcessor& processor, juce::DragAndDropContainer& container)
    : Component(), targetFile(file), themeColor(color), stemIndex(index), audioProcessor(processor), dragContainer(container)
{
    setName(name);
    
    muteButton.setClickingTogglesState(true);
    // Level, separate from mute. The mixer had only M and S, so the one way to
    // hear less of a stem was to hear none of it — which is not how anyone
    // balances four stems against each other.
    volumeSlider.setRange (0.0, 1.0);
    volumeSlider.setValue (audioProcessor.getStemVolume (stemIndex), juce::dontSendNotification);
    volumeSlider.setColour (juce::Slider::trackColourId, themeColor);
    volumeSlider.setColour (juce::Slider::backgroundColourId, Theme::surfaceRaised);
    volumeSlider.setColour (juce::Slider::thumbColourId, Theme::text);
    volumeSlider.onValueChange = [this] {
        audioProcessor.setStemVolume (stemIndex, (float) volumeSlider.getValue());
    };
    addAndMakeVisible (volumeSlider);

    // M and S read as states, so the "on" colour is what distinguishes them:
    // muted goes to the danger tint, soloed to gold. They were both the same
    // grey with only a toggle underneath telling them apart.
    muteButton.setColour(juce::TextButton::buttonColourId, Theme::surfaceRaised);
    muteButton.setColour(juce::TextButton::buttonOnColourId, Theme::danger.withAlpha(0.85f));
    muteButton.setColour(juce::TextButton::textColourOffId, Theme::textMuted);
    muteButton.setColour(juce::TextButton::textColourOnId, Theme::background);
    muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red.withAlpha(0.7f));
    muteButton.onClick = [this]() { audioProcessor.setStemMute(stemIndex, muteButton.getToggleState()); };
    addAndMakeVisible(muteButton);
    
    soloButton.setClickingTogglesState(true);
    soloButton.setColour(juce::TextButton::buttonColourId, Theme::surfaceRaised);
    soloButton.setColour(juce::TextButton::textColourOffId, Theme::textMuted);
    soloButton.setColour(juce::TextButton::textColourOnId, Theme::background);
    soloButton.setColour(juce::TextButton::buttonOnColourId, Theme::gold.withAlpha(0.7f));
    soloButton.onClick = [this]() { audioProcessor.setStemSolo(stemIndex, soloButton.getToggleState()); };
    addAndMakeVisible(soloButton);
    
    exportButton.setColour(juce::TextButton::buttonColourId, Theme::surfaceHigh);
    exportButton.setColour(juce::TextButton::textColourOffId, Theme::text);
    exportButton.addMouseListener(this, false); // For dragging handling
    addAndMakeVisible(exportButton);

    startTimerHz(30);
    generateWaveformPreview();
}

SubverseSplitterAudioProcessorEditor::StemPanel::~StemPanel() { stopTimer(); }

void SubverseSplitterAudioProcessorEditor::StemPanel::generateWaveformPreview()
{
    if (!targetFile.existsAsFile()) return;
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(targetFile));
    if (reader != nullptr)
    {
        // Peak per block, not RMS.
        //
        // A waveform display shows how far the signal went, and RMS shows how
        // much energy it carried on average — which for a drum stem, mostly
        // silence with events in it, is a small number that flattens the row to
        // nothing. It was then divided by a hardcoded 0.35, a reference with no
        // relationship to the material, so how tall a stem drew was decided by
        // a constant rather than by the audio.
        const int numPoints = 400;
        waveformData.assign ((size_t) numPoints, 0.0f);

        const int samplesPerPoint =
            juce::jmax (1, (int) (reader->lengthInSamples / numPoints));
        juce::AudioBuffer<float> tempBuffer (1, samplesPerPoint);

        peak = 0.0f;
        for (int i = 0; i < numPoints; ++i)
        {
            tempBuffer.clear();
            reader->read (&tempBuffer, 0, samplesPerPoint,
                          (juce::int64) i * samplesPerPoint, true, false);
            const float blockPeak = tempBuffer.getMagnitude (0, 0, samplesPerPoint);
            waveformData[(size_t) i] = blockPeak;
            peak = juce::jmax (peak, blockPeak);
        }
    }
}

void SubverseSplitterAudioProcessorEditor::StemPanel::timerCallback()
{
    // Sync buttons with backend state (in case changed elsewhere)
    muteButton.setToggleState(audioProcessor.getStemMute(stemIndex), juce::dontSendNotification);
    soloButton.setToggleState(audioProcessor.getStemSolo(stemIndex), juce::dontSendNotification);
    
    if (isHovering && hoverAlpha < 1.0f) { hoverAlpha = juce::jmin(1.0f, hoverAlpha + 0.2f); repaint(); }
    else if (!isHovering && hoverAlpha > 0.0f) { hoverAlpha = juce::jmax(0.0f, hoverAlpha - 0.1f); repaint(); }

    // The playhead is drawn in this panel, so this panel has to redraw for it
    // to move. Without this it would sit at zero until something else happened
    // to trigger a repaint — a playhead that does not travel is worse than none.
    if (audioProcessor.getIsPlaying())
        repaint();
}

void SubverseSplitterAudioProcessorEditor::StemPanel::mouseEnter(const juce::MouseEvent&) { isHovering = true; }
void SubverseSplitterAudioProcessorEditor::StemPanel::mouseExit(const juce::MouseEvent&) { isHovering = false; }

void SubverseSplitterAudioProcessorEditor::StemPanel::mouseDown(const juce::MouseEvent& e)
{
    // If we click on the exportButton, prepare for drag
    if (e.originalComponent == &exportButton)
        exportButton.setButtonText("Dragging...");
}

void SubverseSplitterAudioProcessorEditor::StemPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (e.originalComponent == &exportButton && !dragContainer.isDragAndDropActive())
    {
        if (targetFile.existsAsFile())
        {
            juce::StringArray files;
            files.add(targetFile.getFullPathName());
            
            juce::Image dragImage(juce::Image::ARGB, 120, 40, true);
            juce::Graphics g(dragImage);
            g.fillAll(themeColor.withAlpha(0.8f));
            g.setColour(juce::Colours::white);
            g.drawText(getName() + " Stem", dragImage.getBounds(), juce::Justification::centred);
            
            dragContainer.performExternalDragDropOfFiles(files, false, this, [this]() {
                exportButton.setButtonText("Drag/Export");
            });
        }
    }
}

void SubverseSplitterAudioProcessorEditor::StemPanel::mouseUp(const juce::MouseEvent& e)
{
    if (e.originalComponent == &exportButton)
        exportButton.setButtonText("Drag/Export");
}

void SubverseSplitterAudioProcessorEditor::StemPanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    
    bool isMuted = muteButton.getToggleState();
    
    // Panel Background — a surface lifted off the window ground, so four rows
    // read as four cards rather than as one flat field.
    g.setColour(Theme::surface);
    g.fillRoundedRectangle(bounds, 10.0f);
    
    // The stem's colour as a spine down the left edge. The name used to carry
    // the colour only through the waveform tint, which a muted stem loses
    // entirely — leaving four grey rows with no way to tell which was which.
    auto inner = bounds.reduced ((float) StemLayout::padding);
    g.setColour (themeColor.withAlpha (isMuted ? 0.35f : 1.0f));
    g.fillRoundedRectangle (inner.getX(), inner.getY() + inner.getHeight() * 0.22f,
                            (float) StemLayout::chip, inner.getHeight() * 0.56f, 2.0f);

    g.setColour (isMuted ? Theme::textMuted : Theme::text);
    g.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("Bold")));
    g.drawText (getName().toUpperCase(),
                (int) inner.getX() + StemLayout::chip + StemLayout::chipGap,
                (int) inner.getY(), StemLayout::name, (int) inner.getHeight(),
                juce::Justification::centredLeft);

    // Waveform rendering, in the column the layout actually left for it.
    const int waveLeft = (int) inner.getX() + StemLayout::chip + StemLayout::chipGap
                         + StemLayout::name + StemLayout::gap;
    // The waveform gets the row's height, not a thin band in the middle of it.
    // It was drawn about 60 px tall inside a 146 px card, so every row carried
    // forty pixels of nothing above and below the only thing worth looking at.
    auto waveArea = juce::Rectangle<float> (
        (float) waveLeft, inner.getY(),
        inner.getRight() - StemLayout::controls - StemLayout::gap - waveLeft,
        inner.getHeight());

    if (!waveformData.empty() && waveArea.getWidth() > 8.0f)
    {
        juce::Path wavePath;
        float width = waveArea.getWidth();
        float height = waveArea.getHeight();
        float center = waveArea.getCentreY();
        float xStart = waveArea.getX();
        float step = width / (float)waveformData.size();
        
        wavePath.startNewSubPath(xStart, center);
        for (size_t i = 0; i < waveformData.size(); ++i)
        {
            float amp = juce::jmin (1.0f, waveformData[i] * displayScale) * (height * 0.46f);
            wavePath.lineTo(xStart + i * step, center - amp);
        }
        for (int i = (int)waveformData.size() - 1; i >= 0; --i)
        {
            float amp = juce::jmin (1.0f, waveformData[i] * displayScale) * (height * 0.46f);
            wavePath.lineTo(xStart + i * step, center + amp);
        }
        wavePath.closeSubPath();
        
        float waveAlpha = isMuted ? 0.12f : (0.45f + (hoverAlpha * 0.2f));
        g.setColour(themeColor.withAlpha(waveAlpha));
        g.fillPath(wavePath);

        // The playhead, on every stem rather than only on the transport bar at
        // the bottom of the window. Someone auditioning a separation is looking
        // at the waveforms; making them look somewhere else to find out where
        // they are is the whole complaint about where the preview sits.
        const double length = audioProcessor.getTotalLength();
        if (length > 0.0)
        {
            const float played = (float) juce::jlimit (0.0, 1.0,
                                     audioProcessor.getPlaybackPosition() / length);
            g.setColour (Theme::text.withAlpha (0.75f));
            const float x = waveArea.getX() + waveArea.getWidth() * played;
            g.fillRect (x, waveArea.getY(), 1.5f, waveArea.getHeight());
        }
    }
    
    // Subtle border
    g.setColour(Theme::line);
    g.drawRoundedRectangle(bounds, 10.0f, 1.0f);
    
    // Highlight border on hover
    if (hoverAlpha > 0.0f && !isMuted)
    {
        g.setColour(themeColor.withAlpha(0.5f * hoverAlpha));
        g.drawRoundedRectangle(bounds, 10.0f, 1.5f);
    }
}

void SubverseSplitterAudioProcessorEditor::StemPanel::resized()
{
    // One horizontal row, so the same control sits at the same x in every stem
    // and the eye can run down a column. They were stacked in a narrow right
    // margin before — volume above export above M and S — which made four rows
    // of four different-looking blocks.
    auto bounds = getLocalBounds().reduced (StemLayout::padding);
    const int rowHeight = 34;
    auto row = bounds.withSizeKeepingCentre (bounds.getWidth(), rowHeight);

    auto controls = row.removeFromRight (StemLayout::controls);

    volumeSlider.setBounds (controls.removeFromLeft (StemLayout::volume));
    controls.removeFromLeft (StemLayout::gap);

    muteButton.setBounds (controls.removeFromLeft (StemLayout::button));
    controls.removeFromLeft (StemLayout::buttonGap);
    soloButton.setBounds (controls.removeFromLeft (StemLayout::button));
    controls.removeFromLeft (StemLayout::gap);

    exportButton.setBounds (controls);
}
