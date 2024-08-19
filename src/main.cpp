#include <iostream>
#include <vector>
#include <cmath>
#include <fftw3.h>
#include <ncurses.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>

#define FFT_SIZE 4096
#define kAudioUnitErr_NoMemory -108

using namespace std;

// callback function for main
OSStatus AudioTapCallback(void *inRefCon, 
                          AudioUnitRenderActionFlags *ioActionFlags, 
                          const AudioTimeStamp *inTimeStamp, 
                          UInt32 inBusNumber, 
                          UInt32 inNumberFrames, 
                          AudioBufferList *ioData) {
    // Retrieve the Audio Unit from the inRefCon
    AudioUnit audioUnit = *(AudioUnit *)inRefCon;

    // Allocate memory for the buffer list
    AudioBufferList *bufferList = (AudioBufferList *)malloc(sizeof(AudioBufferList) + sizeof(AudioBuffer));
    if (bufferList == nullptr) {
        return kAudioUnitErr_NoMemory;
    }

    bufferList->mNumberBuffers = 1;
    bufferList->mBuffers[0].mNumberChannels = 2; // Set to stereo
    bufferList->mBuffers[0].mDataByteSize = inNumberFrames * sizeof(Float32) * 2;
    bufferList->mBuffers[0].mData = malloc(bufferList->mBuffers[0].mDataByteSize);
    if (bufferList->mBuffers[0].mData == nullptr) {
        free(bufferList);
        return kAudioUnitErr_NoMemory;
    }

    // Render audio data into the buffer list
    OSStatus status = AudioUnitRender(audioUnit, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, bufferList);
    if (status != noErr) {
        free(bufferList->mBuffers[0].mData);
        free(bufferList);
        return status;
    }


    Float32 *audioData = (Float32 *)bufferList->mBuffers[0].mData;

    // Prepare mono input
    vector<double> fft_in(FFT_SIZE, 0.0);
    // Ensure we do not exceed the bounds of the input data
    for (UInt32 i = 0; i < FFT_SIZE && i < inNumberFrames; i++) {
        fft_in[i] = (audioData[i * 2] + audioData[i * 2 + 1]) / 2.0; // Convert stereo to mono
    }

    // FFT application
    fftw_complex *fft_out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
    if (!fft_out) {
        cerr << "Error allocating memory for FFT output." << endl;
        free(bufferList->mBuffers[0].mData);
        free(bufferList);
        return kAudioUnitErr_NoMemory;
    }

    fftw_plan plan = fftw_plan_dft_r2c_1d(FFT_SIZE, fft_in.data(), fft_out, FFTW_ESTIMATE);
    fftw_execute(plan);

    // Visualization with ncurses
    int max_y = LINES - 1;
    int num_bins = FFT_SIZE / 2; // Only half of FFT output is useful (the positive frequencies)
    int bin_width = COLS / num_bins;
    clear();
    for (int i = 0; i < num_bins; ++i) {
        double magnitude = sqrt(fft_out[i][0] * fft_out[i][0] + fft_out[i][1] * fft_out[i][1]);
        int bar_height = static_cast<int>((magnitude * max_y) / 10000.0); // Scale magnitude to terminal height
        if (bar_height > max_y) bar_height = max_y; // Clamp to terminal height
        // Draw vertical bars for each bin
        for (int j = 0; j < bar_height; ++j) {
            mvprintw(max_y - j, i * bin_width, "|");
        }
    }
    refresh();

    // FFT cleanup and malloc clearing
    fftw_destroy_plan(plan);
    fftw_free(fft_out);
    free(bufferList->mBuffers[0].mData);
    free(bufferList);

    return noErr; // success!
}

void AudioTapSetup(AudioUnit &audioUnit) {
    // Describe the Audio Unit
    AudioComponentDescription description = {0};
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_HALOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;

    // Find and instantiate the Audio Unit
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    AudioComponentInstanceNew(component, &audioUnit);

    // Enable input on the Audio Unit
    UInt32 enableIO = 1;
    AudioUnitSetProperty(audioUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enableIO, sizeof(enableIO));

    // Set the current device to the default output device (for capturing system-wide audio)
    AudioDeviceID deviceID = 0;
    UInt32 size = sizeof(deviceID);
    AudioObjectPropertyAddress defaultOutputDeviceProperty = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultOutputDeviceProperty, 0, nullptr, &size, &deviceID);

    AudioUnitSetProperty(audioUnit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &deviceID, sizeof(deviceID));

    // Set the audio format (stereo, 32-bit float, 44.1 kHz)
    AudioStreamBasicDescription format;
    format.mSampleRate = 44100.0;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;
    format.mBytesPerPacket = 8;
    format.mBytesPerFrame = 8;

    AudioUnitSetProperty(audioUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format));

    // Add the render callback to tap the audio data
    OSStatus status = AudioUnitAddRenderNotify(audioUnit, AudioTapCallback, &audioUnit);
    if (status != noErr) {
        cerr << "Error adding render notify: " << status << endl;
    }

    // Initialize the Audio Unit
    AudioUnitInitialize(audioUnit);
}

int main() {
    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    AudioUnit audioUnit = nullptr;
    OSStatus status;

    // Setup audio unit
    AudioTapSetup(audioUnit);

    // Start audio unit
    status = AudioOutputUnitStart(audioUnit);
    if (status != noErr) {
        cerr << "Error starting audio unit: " << status << endl;
        endwin();
        return 1;
    }

    // Main loop
    cout << "Press 'q' to exit" << endl;
    while (true) {
        int ch = getch();
        if (ch == 'q') {
            break;
        }
    }

    // Stop audio unit
    status = AudioOutputUnitStop(audioUnit);
    if (status != noErr) {
        cerr << "Error stopping audio unit: " << status << endl;
    }

    // Uninitialize and dispose audio unit
    status = AudioUnitUninitialize(audioUnit);
    if (status != noErr) {
        cerr << "Error uninitializing audio unit: " << status << endl;
    }

    status = AudioComponentInstanceDispose(audioUnit);
    if (status != noErr) {
        cerr << "Error disposing audio unit: " << status << endl;
    }

    // End ncurses
    endwin();
}