#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    
    openFolderButton.setColour (juce::TextButton::buttonColourId, juce::Colour(0xff222222));
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
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (statusLabel);

    selectAudioButton.setButtonText("Browse Files...");
    selectAudioButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
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
    playPauseButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff14b8a6)); // Teal
    playPauseButton.onClick = [this]() {
        audioProcessor.togglePlayPause();
    };
    addAndMakeVisible(playPauseButton);

    // Load AI generated assets
    int bgSize, logoSize, workersSize;
    const char* bgData = BinaryData::getNamedResource("background_png", bgSize);
    const char* logoData = BinaryData::getNamedResource("logo_png", logoSize);
    const char* workersData = BinaryData::getNamedResource("loading_workers_png", workersSize);
    if (bgData != nullptr) backgroundImage = juce::ImageCache::getFromMemory(bgData, bgSize);
    if (logoData != nullptr) logoImage = juce::ImageCache::getFromMemory(logoData, logoSize);
    if (workersData != nullptr) loadingWorkersImage = juce::ImageCache::getFromMemory(workersData, workersSize);

    setSize (1000, 780);
    startTimer(50); // 20 FPS refresh for UI timeline
}

SubverseSplitterAudioProcessorEditor::~SubverseSplitterAudioProcessorEditor()
{
}

void SubverseSplitterAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Modern sleek navy gradient background
    juce::Colour bgTop(0xff131825);
    juce::Colour bgBottom(0xff0b0f19);
    juce::ColourGradient bgGradient(bgTop, 0.0f, 0.0f, bgBottom, 0.0f, (float)getHeight(), false);
    g.setGradientFill(bgGradient);
    g.fillRect(getLocalBounds());
    
    // Draw Header separator line
    g.setColour(juce::Colour(0xff1e293b).withAlpha(0.5f));
    g.drawLine(0.0f, 50.0f, (float)getWidth(), 50.0f, 1.0f);
    
    // Draw Logo in header (centered)
    if (logoImage.isValid())
    {
        // Calculate center X of the header (which is getWidth() / 2)
        // Header height is 50. So center Y is 25.
        g.drawImageWithin(logoImage, getWidth() / 2 - 20, 5, 40, 40, juce::RectanglePlacement::centred);
    }
    
    // Draw Transport Bar separator line
    g.setColour(juce::Colour(0xff1e293b).withAlpha(0.5f));
    g.drawLine(0.0f, (float)(getHeight() - 60), (float)getWidth(), (float)(getHeight() - 60), 1.0f);
    
    // Draw timeline if loaded
    if (vocalsPanel != nullptr)
    {
        double pos = audioProcessor.getPlaybackPosition();
        double len = audioProcessor.getTotalLength();
        if (len > 0.0)
        {
            float progress = static_cast<float>(pos / len);
            int barY = getHeight() - 60;
            g.setColour(juce::Colour(0xff222222));
            g.fillRect(0, barY - 4, getWidth(), 4);
            g.setColour(juce::Colour(0xff14b8a6)); // Teal
            g.fillRect(0, barY - 4, static_cast<int>(getWidth() * progress), 4);
            
            // Format time
            auto formatTime = [](double seconds) {
                int m = static_cast<int>(seconds) / 60;
                int s = static_cast<int>(seconds) % 60;
                return juce::String::formatted("%02d:%02d", m, s);
            };
            
            g.setColour(juce::Colours::grey);
            g.setFont(12.0f);
            juce::String timeStr = formatTime(pos) + " / " + formatTime(len);
            g.drawText(timeStr, getWidth() / 2 - 50, getHeight() - 35, 100, 20, juce::Justification::centred);
        }
    }

    // Drag overlay
    if (vocalsPanel == nullptr)
    {
        auto bounds = getLocalBounds().withTrimmedTop(50).withTrimmedBottom(60).reduced(20);
        
        g.setColour(juce::Colour(0xff161b22).withAlpha(0.7f));
        g.fillRoundedRectangle(bounds.toFloat(), 12.0f);
        
        if (isDraggingActive)
        {
            g.setColour(juce::Colour(0xff3b82f6));
            g.drawRoundedRectangle(bounds.toFloat(), 12.0f, 2.0f);
        }
        else
        {
            g.setColour(juce::Colour(0xff333333));
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
                { juce::Colour(0xfff59e0b),  2.0f, -1.5f, 10.0f,  18.0f, 80.0f, 2.0f }, // Orange
                { juce::Colour(0xff14b8a6), -2.5f,  3.0f, 12.0f,  22.0f, 110.0f, 3.0f }, // Teal
                { juce::Colour(0xff0ea5e9),  3.5f, -2.0f, 15.0f,  25.0f, 95.0f, 2.5f }, // Light Blue
                { juce::Colour(0xff6366f1), -1.8f,  2.8f,  8.0f,  20.0f, 65.0f, 1.5f }  // Indigo
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
            
            g.setColour(juce::Colour(0xff1e293b)); // Dark gray/navy background for progress bar
            g.fillRoundedRectangle(barArea, 10.0f);
            
            g.setColour(juce::Colour(0xff14b8a6)); // Teal to match theme
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

    // Center Panels
    auto centerArea = getLocalBounds().withTrimmedTop(50).withTrimmedBottom(60).reduced(10);

    bool showStems = (vocalsPanel != nullptr);
    playPauseButton.setVisible(showStems);
    
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
            statusLabel.setBounds(centerArea.withSizeKeepingCentre(400, 30).translated(0, 160));
        }
        else
        {
            instructionLabel.setBounds(centerArea.withSizeKeepingCentre(400, 40).translated(0, -30));
            selectAudioButton.setBounds(centerArea.withSizeKeepingCentre(150, 40).translated(0, 30));
            statusLabel.setText("", juce::dontSendNotification);
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
        statusLabel.setText ("Analyzing...", juce::dontSendNotification);
        
        vocalsPanel.reset(); drumsPanel.reset(); bassPanel.reset(); otherPanel.reset();

        audioProcessor.getModelRunner().startSeparation (audioFile);
        resized();
    }
    repaint();
}

void SubverseSplitterAudioProcessorEditor::timerCallback()
{
    // Update Play/Pause button text and timeline
    playPauseButton.setButtonText(audioProcessor.getIsPlaying() ? "PAUSE" : "PLAY");
    
    // Repaint to update timeline cursor
    if (audioProcessor.getIsPlaying()) repaint();

    auto& runner = audioProcessor.getModelRunner();
    if (runner.isRunning())
    {
        float progress = runner.getProgress();
        juce::String stageText = "Analyzing Audio...";
        if (progress > 0.2f && progress <= 0.6f) stageText = "Separating Stems...";
        else if (progress > 0.6f && progress <= 0.9f) stageText = "Extracting Vocals...";
        else if (progress > 0.9f) stageText = "Finalizing Output...";
        
        statusLabel.setText (stageText, juce::dontSendNotification);
        repaint();
    }
    else if (runner.isFinishedSuccessfully() && vocalsPanel == nullptr)
    {
        loadedFileLabel.setText (loadedFileLabel.getText() + " [Separation Complete]", juce::dontSendNotification);
        
        vocalsPanel = std::make_unique<StemPanel> ("Vocals", runner.getVocalsFile(), juce::Colour(0xfff59e0b), 0, audioProcessor, *this); // Orange
        drumsPanel = std::make_unique<StemPanel> ("Drums", runner.getDrumsFile(), juce::Colour(0xff14b8a6), 1, audioProcessor, *this);   // Teal
        bassPanel = std::make_unique<StemPanel> ("Bass", runner.getBassFile(), juce::Colour(0xff0ea5e9), 2, audioProcessor, *this);    // Light Blue
        otherPanel = std::make_unique<StemPanel> ("Other", runner.getOtherFile(), juce::Colour(0xff6366f1), 3, audioProcessor, *this); // Indigo

        audioProcessor.loadStemForPlayback(0, runner.getVocalsFile());
        audioProcessor.loadStemForPlayback(1, runner.getDrumsFile());
        audioProcessor.loadStemForPlayback(2, runner.getBassFile());
        audioProcessor.loadStemForPlayback(3, runner.getOtherFile());

        addAndMakeVisible (vocalsPanel.get());
        addAndMakeVisible (drumsPanel.get());
        addAndMakeVisible (bassPanel.get());
        addAndMakeVisible (otherPanel.get());

        resized();
        repaint();
    }
    else if (!runner.isRunning() && !runner.isFinishedSuccessfully() && runner.getErrorMessage().isNotEmpty())
    {
        statusLabel.setText ("Error: " + runner.getErrorMessage(), juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::red);
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
    muteButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff222222));
    muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red.withAlpha(0.7f));
    muteButton.onClick = [this]() { audioProcessor.setStemMute(stemIndex, muteButton.getToggleState()); };
    addAndMakeVisible(muteButton);
    
    soloButton.setClickingTogglesState(true);
    soloButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff222222));
    soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::gold.withAlpha(0.7f));
    soloButton.onClick = [this]() { audioProcessor.setStemSolo(stemIndex, soloButton.getToggleState()); };
    addAndMakeVisible(soloButton);
    
    exportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333333));
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
        const int numPoints = 150;
        waveformData.resize(numPoints, 0.0f);
        int samplesPerPoint = static_cast<int>(reader->lengthInSamples / numPoints);
        juce::AudioBuffer<float> tempBuffer(1, samplesPerPoint);
        
        float maxRms = 0.35f;
        for (int i = 0; i < numPoints; ++i)
        {
            tempBuffer.clear();
            reader->read(&tempBuffer, 0, samplesPerPoint, i * samplesPerPoint, true, false);
            float rms = tempBuffer.getRMSLevel(0, 0, samplesPerPoint);
            waveformData[i] = juce::jmin(1.0f, rms / maxRms);
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
    
    // Panel Background
    g.setColour(juce::Colour(0xff131825));
    g.fillRoundedRectangle(bounds, 8.0f);
    
    // Header text
    g.setColour(isMuted ? juce::Colours::grey : juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(18.0f).withStyle("Bold")));
    g.drawText(getName(), 20, bounds.getHeight() / 2 - 15, 120, 30, juce::Justification::centredLeft);
    
    // Waveform rendering
    auto waveArea = bounds.withTrimmedLeft(140).withTrimmedRight(180).reduced(10, 20);
    if (!waveformData.empty())
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
            float amp = waveformData[i] * (height * 0.45f);
            wavePath.lineTo(xStart + i * step, center - amp);
        }
        for (int i = (int)waveformData.size() - 1; i >= 0; --i)
        {
            float amp = waveformData[i] * (height * 0.45f);
            wavePath.lineTo(xStart + i * step, center + amp);
        }
        wavePath.closeSubPath();
        
        float waveAlpha = isMuted ? 0.1f : (0.4f + (hoverAlpha * 0.2f));
        g.setColour(themeColor.withAlpha(waveAlpha));
        g.fillPath(wavePath);
    }
    
    // Subtle border
    g.setColour(juce::Colour(0xff2a2a2a));
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
    
    // Highlight border on hover
    if (hoverAlpha > 0.0f && !isMuted)
    {
        g.setColour(themeColor.withAlpha(0.5f * hoverAlpha));
        g.drawRoundedRectangle(bounds, 8.0f, 1.5f);
    }
}

void SubverseSplitterAudioProcessorEditor::StemPanel::resized()
{
    auto bounds = getLocalBounds().reduced(20);
    auto rightArea = bounds.removeFromRight(120);
    
    auto topRow = rightArea.removeFromTop(40);
    soloButton.setBounds(topRow.removeFromRight(40));
    topRow.removeFromRight(10); // Spacing
    muteButton.setBounds(topRow.removeFromRight(40));
    
    exportButton.setBounds(rightArea.removeFromBottom(40));
}
