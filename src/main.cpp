#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <fftw3.h>
#include <ncurses.h>
#include <iostream>
#include <cmath>
#include <vector>

#define FFT_SIZE 1024  // Size of the FFT window
using namespace std;

// callback function for the audio tap
OSStatus AudioTapCallback(void *inRefCon, 
                          AudioUnitRenderActionFlags *ioActionFlags, 
                          const AudioTimeStamp *inTimeStamp, 
                          UInt32 inBusNumber, 
                          UInt32 inNumberFrames, 
                          AudioBufferList *ioData) {
    // retrieve the Audio Unit from the inRefCon
    AudioUnit audioUnit = *(AudioUnit *)inRefCon;

    // buffer list object to hold audio data
    AudioBufferList bufferList;
    bufferList.mNumberBuffers = 1;
    // create two channels in buffer list object at index 0
    bufferList.mBuffers[0].mNumberChannels = 2; // set to stereo
    bufferList.mBuffers[0].mDataByteSize = inNumberFrames * sizeof(Float32) * 2; //
    bufferList.mBuffers[0].mData = malloc(inNumberFrames * sizeof(Float32) * 2); //

    // render audio -> bufferList
    OSStatus status = AudioUnitRender(audioUnit, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, &bufferList);
    // error handling
    if (status != noErr) {
        cerr << "Error Rendering Audio: " << status << endl;
        free(bufferList.mBuffers[0].mData);
        return status;
    }


    // FFTW3 IMPLEMENTATION
    // audio data processing into fast fourier transform
    Float32 *audioData = (Float32 *)bufferList.mBuffers[0].mData;

    // prepare mono input
    vector<double> fft_in(FFT_SIZE);
    // if input is stereo, we can convert to mono by checking if a second channel exists via index 1
    for (UInt32 i = 0; i < FFT_SIZE; i++) {
        fft_in[i] = (audioData[i * 2] + audioData[i * 2 + 1]) / 2.0; // convert stereo to mono if needed
        }
    
    // fft application
    fftw_complex *fft_out = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * FFT_SIZE);
    fftw_plan plan = fftw_plan_dft_r2c_1d(FFT_SIZE, fft_in.data(), fft_out, FFTW_ESTIMATE);
    fftw_execute(plan);

    // visualization testing with ncurses
    int max_y = LINES - 1;
    clear();
    for (int i = 0; i < FFT_SIZE / 2; ++i) {
        double magnitude = sqrt(fft_out[i][0] * fft_out[i][0] + fft_out[i][1] * fft_out[i][1]);
        int bar_height = (int)((magnitude * max_y) / (FFT_SIZE * 1000)); // Scale magnitude to terminal height
        if (bar_height > max_y) bar_height = max_y; // Clamp to terminal height
        for (int j = 0; j < bar_height; ++j) {
            mvprintw(max_y - j, i * (COLS / (FFT_SIZE / 2)), "|");
        }
    }
    refresh();

    // fft cleanup and malloc clearing
    fftw_destroy_plan(plan);
    fftw_free(fft_out);
    free(bufferList.mBuffers[0].mData);

    return noErr; // success! 
}

void AudioTapSetup(AudioUnit &audioUnit) {
    // initialize audio units
    AudioComponentDescription description = {0};
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_HALOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent component = AudioComponentFindNext(nullptr, &desc);
    AudioComponentInstanceNew(component, &audioUnit);

    UInt32 enableIO = 1; // toggle IO, this is important!!!
    AudioUnitSetProperty(audioUnit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enableIO, sizeof(enableIO));

    // audiostream struct for configuring stereo audio stream
    AudioStreamBasicDescription format;
    format.mSampleRate = 44100.0;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;
    format.mBytesPerPacket = 8;
    format.mBytesPerFrame = 8;

}



int main(int argc, char *argv[])
{
    cout << "cli-vis running" << endl;
}
