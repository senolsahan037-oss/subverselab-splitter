/*  Renders the editor to a PNG without a screen.

    `screencapture` needs macOS Screen Recording permission, which a terminal
    does not have, so it hands back the desktop wallpaper instead of the window.
    `createComponentSnapshot` draws the component into an offscreen image
    through the same paint path the real window uses, needs no permission, and
    can be pointed at any state — including the one that only exists after a
    separation has finished.

        ./build/test_ui_snapshot_artefacts/Release/test_ui_snapshot <audio> <out.png>
*/
#include <JuceHeader.h>

#include "Source/PluginProcessor.h"
#include "Source/PluginEditor.h"

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::cout << "usage: test_ui_snapshot <audio file> <out.png>\n";
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    const bool duringMode = juce::String (argv[1]) == "--during";
    const juce::File input { juce::String (duringMode ? argv[2] : argv[1]) };
    const juce::File output { juce::String (duringMode ? argv[3] : argv[2]) };

    SubverseSplitterAudioProcessor processor;
    std::unique_ptr<SubverseSplitterAudioProcessorEditor> editor (
        dynamic_cast<SubverseSplitterAudioProcessorEditor*> (processor.createEditor()));

    if (editor == nullptr)
    {
        std::cout << "  editor could not be created\n";
        return 1;
    }

    // The editor builds its stem panels from a timerCallback, so the message
    // loop has to actually run — this is not a sleep with extra steps.
    auto pump = [] (int milliseconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    };

    // "--empty" snapshots the state before any file is loaded, which is the
    // other half of the layout and the one a first-time user sees.
    const bool separate = juce::String (argv[1]) != "--empty";

    // Prepared before anything is loaded, exactly as the Standalone wrapper
    // does it: the audio device opens when the window appears, long before a
    // file has been separated. Preparing afterwards — as this test used to —
    // hides any bug that depends on that order.
    processor.prepareToPlay (44100.0, 512);

    if (separate)
    {
        std::cout << "  separating " << input.getFileName() << " ...\n";
        processor.getModelRunner().startSeparation (input);
    }

    // "--during <file>" captures the screen a separation shows while it runs,
    // which is a different layout from both the empty state and the finished
    // one, and the only one a person looks at for minutes at a time.
    const bool captureDuring = juce::String (argv[1]) == "--during";
    if (captureDuring)
    {
        pump (6000);
        editor->setSize (1000, 780);
        editor->resized();
        pump (300);
        const auto midImage = editor->createComponentSnapshot (editor->getLocalBounds(), true);
        juce::PNGImageFormat midPng;
        output.deleteFile();
        if (auto stream = output.createOutputStream())
        {
            midPng.writeImageToStream (midImage, *stream);
            std::cout << "  wrote mid-separation snapshot\n";
        }
        return 0;
    }

    for (int waited = 0; separate && waited < 600; ++waited)     // up to ten minutes
    {
        pump (1000);
        auto& runner = processor.getModelRunner();
        if (! runner.isRunning())
        {
            if (! runner.isFinishedSuccessfully())
            {
                std::cout << "  separation failed: " << runner.getErrorMessage() << "\n";
                return 1;
            }
            break;
        }
        if (waited % 10 == 0)
            std::cout << "    " << juce::roundToInt (runner.getProgress() * 100.0) << "%\n";
    }

    pump (500);                                       // let the panels appear
    editor->setSize (1000, 780);
    editor->resized();
    pump (300);

    // Does it actually make a sound?
    //
    // The stem files can be perfectly good and the mixer still silent, and the
    // two failures look identical from the outside. This pulls blocks through
    // processBlock exactly as the host would and measures what comes out.
    if (separate)
    {
        processor.togglePlayPause();
        pump (200);

        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        float loudest = 0.0f;
        for (int i = 0; i < 200; ++i)                 // about 2.3 seconds
        {
            block.clear();
            processor.processBlock (block, midi);
            loudest = juce::jmax (loudest, block.getMagnitude (0, block.getNumSamples()));
        }
        std::cout << "  playback peak over 2.3 s: " << loudest
                  << (loudest > 0.001f ? "   AUDIBLE" : "   SILENT") << "\n";
        processor.togglePlayPause();
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true);
    juce::PNGImageFormat png;
    output.deleteFile();

    if (auto stream = output.createOutputStream())
    {
        png.writeImageToStream (image, *stream);
        std::cout << "  wrote " << output.getFullPathName()
                  << "  " << image.getWidth() << "x" << image.getHeight() << "\n";
        return 0;
    }

    std::cout << "  could not write " << output.getFullPathName() << "\n";
    return 1;
}
