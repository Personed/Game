#ifdef _MSC_VER
#pragma once
#endif

#ifndef _WAVE_INPUT_HPP
#define _WAVE_INPUT_HPP

// Used for capturing input audio. For capturing are used functions from Windows Media (waveIn**)

class WaveCapture
{
private:
  enum { BUFFERS_USED = 4 };

private:
  HWAVEIN _hWaveIn;
  WAVEFORMATEX _waveFmt;
  WAVEHDR _waveHdr[BUFFERS_USED];       // audio header chunk definition
  AutoArray<short> _data[BUFFERS_USED];  // audio buffers
  AutoArray<WAVEHDR *> _buffReady;      // queue for captured audio-data prepared for process

  unsigned _length;
  bool _deviceOpened;
  unsigned _samplesCaptured;
  int _write;
  int _free;
  /// indicates that this is the first recorded buffer
  bool _firstBuffer;

public:
  WaveCapture();
  // Opens the given waveform-audio input device for recording.
  bool OpenDevice(const WAVEFORMATEX &waveFmt, unsigned len);
  // Closes the given waveform-audio input device.
  void CloseDevice();
  // Copy captured data into destination buffer. Return number of copied bytes.
  unsigned GetBuffer(int16 *ptr, unsigned length);
  // Starts input on the given waveform-audio input device.
  void Start();
  // Stops waveform-audio input.
  void Stop();
  // Device ready for use.
  bool DeviceOpened() const { return _deviceOpened; }
  // The smallest chunk of captured audio.
  unsigned CapturedBuffSize() const { return _length; }
  // Captured bytes(samples).
  unsigned CapturedSamples() const { return _samplesCaptured; }

private:
  // Callback function used with the waveform-audio input device. Address of this function can be specified in the callback-address parameter of the waveInOpen function.
  static void CALLBACK waveInProc(HWAVEIN hwi, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2);
  // Capturing buffers initialization.
  bool PrepareBuffers();
  // Release of buffers.
  bool UnPrepareBuffers();
  // Audio data processing.
  void ProcessData(WAVEHDR *waveHdr);
};


#endif
