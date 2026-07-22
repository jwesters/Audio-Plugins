# Building BinauralJourney

## Requirements

- macOS
- Xcode
- JUCE 8.0.14
- Projucer

JUCE is not included in this source package. Obtain JUCE separately before
building BinauralJourney.

## Configure the JUCE module path

The project is configured to use Projucer's global JUCE module path.

1. Open Projucer.
2. Choose `Projucer > Global Paths`.
3. Set the JUCE modules path to the `modules` directory inside your
   JUCE 8.0.14 installation. For example:

   `/path/to/JUCE-8.0.14/modules`

The JUCE installation does not need to be beside the BinauralJourney source
folder.

## Generate the Xcode project

1. Open `BinauralJourney.jucer` in Projucer.
2. Confirm that all JUCE modules resolve correctly.
3. Choose **Save Project and Open in IDE**.
4. Projucer generates the Xcode project under:

   `Builds/MacOSX/`

## Build the Audio Unit

1. Select the `BinauralJourney - AU` scheme.
2. Select the Release configuration.
3. Choose **Product > Clean Build Folder**.
4. Press Command-B.

Expected installed location:

`~/Library/Audio/Plug-Ins/Components/BinauralJourney.component`

## Build the VST3

1. Select the `BinauralJourney - VST3` scheme.
2. Select the Release configuration.
3. Press Command-B.

Expected installed location:

`~/Library/Audio/Plug-Ins/VST3/BinauralJourney.vst3`

## Validate the Audio Unit

Quit Logic Pro and run:

`auval -v aumu Bnjr JWst`

The final result should say:

`AU VALIDATION SUCCEEDED`
