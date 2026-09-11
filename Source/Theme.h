#pragma once

#include <JuceHeader.h>

/*  The SubverseLab palette, in one place.

    What was here before was Tailwind's default ramp scattered as literals
    through the editor — teal-500, amber-500, indigo-500, sky-500, slate-800.
    Those are the colours a framework gives you when nobody has chosen any, and
    they belong to no product; beside Sensei, Arrangement GPS and the web
    Splitter this plugin read as something from a different company.

    The stem colours are deliberately the *same five* Arrangement GPS and the
    web Splitter use. A drum track is one colour across the platform, which is
    worth more than any single tool having a prettier set of its own — someone
    moving between the plugin and the browser should not have to relearn which
    lane is which.
*/
namespace Theme
{
    // Ground and surfaces
    const juce::Colour background   { 0xff032825 };
    const juce::Colour surface      { 0xff063730 };
    const juce::Colour surfaceRaised{ 0xff0a4740 };
    const juce::Colour surfaceHigh  { 0xff0e554a };
    const juce::Colour line         { 0xff1a5b52 };

    // The one accent, shared with the site
    const juce::Colour gold         { 0xffC5A059 };
    const juce::Colour goldLight    { 0xffDFBF7A };
    const juce::Colour goldDark     { 0xff8C6F35 };

    const juce::Colour teal         { 0xff2A9D8F };
    const juce::Colour tealLight    { 0xff3BBFAF };

    // Type
    const juce::Colour text         { 0xffF2EFE7 };
    const juce::Colour textMuted    { 0xff8faaa5 };
    const juce::Colour danger       { 0xffe8917f };

    // Stems — the platform's shared track palette
    const juce::Colour stemDrums    { 0xffb8935a };
    const juce::Colour stemBass     { 0xffcf7f5c };
    const juce::Colour stemOther    { 0xffa58cc9 };
    const juce::Colour stemVocals   { 0xff5cba95 };
    const juce::Colour stemInstrumental { 0xff5f9ec4 };
}
