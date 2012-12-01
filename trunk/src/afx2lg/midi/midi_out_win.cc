// Copyright (c) 2012, Tomas Gunnarsson
// All rights reserved.

#include "midi/midi_out.h"

#include <windows.h>  // must come first.
#include <mmsystem.h>

#include <iostream>

class Lock {
 public:
  Lock() { ::InitializeCriticalSection(&cs_); }
  ~Lock() { ::DeleteCriticalSection(&cs_); }

  void Acquire() { ::EnterCriticalSection(&cs_); }
  void Release() { ::LeaveCriticalSection(&cs_); }

 private:
  CRITICAL_SECTION cs_;
};

template <typename T>
class AutoLock {
 public:
  AutoLock(T& lock) : lock_(lock) { lock_.Acquire(); }
  ~AutoLock() { lock_.Release(); }
 private:
  T& lock_;
};

namespace midi {
class MidiOutWin : public MidiOut {
 public:
  MidiOutWin(const shared_ptr<MidiDeviceInfo>& device)
      : MidiOut(device),
        midi_out_(NULL) {
  }

  virtual ~MidiOutWin() {
    Close();
  }

  bool Init() {
    MMRESULT res = midiOutOpen(&midi_out_, device_->id(),
        reinterpret_cast<DWORD_PTR>(&MidiOutCallback),
        reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
    return res == MMSYSERR_NOERROR;
  }

  void Close() {
    if (midi_out_) {
      midiOutClose(midi_out_);
      midi_out_ = NULL;
    }
  }

  // MidiOut implementation.

  virtual bool Send(unique_ptr<Message> message) {
    ASSERT(!message->empty());

    // |message| has already been pushed onto the queue.
    MIDIHDR* header = new MIDIHDR();
    header->dwBufferLength = message->size();
    header->lpData = reinterpret_cast<char*>(&message->at(0));
    header->dwUser = reinterpret_cast<DWORD_PTR>(message.get());

    MMRESULT res = midiOutPrepareHeader(midi_out_, header,
                                        sizeof(*header));
    if (res == MMSYSERR_NOERROR) {
      res = midiOutLongMsg(midi_out_, header, sizeof(*header));
      if (res == MMSYSERR_NOERROR)
        message.release();
    }
    
    if (res != MMSYSERR_NOERROR)
      delete header;

    return res == MMSYSERR_NOERROR;
  }

 protected:
  void OnDone(MIDIHDR* header) {
    Message* buffer = reinterpret_cast<Message*>(header->dwUser);
    delete buffer;
    MMRESULT res = midiOutUnprepareHeader(midi_out_, header, sizeof(*header));
    ASSERT(res == MMSYSERR_NOERROR);
    delete header;
  }

  void OnCallback(UINT msg, DWORD_PTR param1, DWORD_PTR param2) {
    switch (msg) {
    case MM_MOM_OPEN:
      // std::cout << "MM_MOM_OPEN" << std::endl;
      break;
    case MM_MOM_CLOSE:
      // std::cout << "MM_MOM_CLOSE" << std::endl;
      break;
    case MM_MOM_DONE:
      OnDone(reinterpret_cast<MIDIHDR*>(param1));
      break;
    default:
#ifdef _DEBUG
      std::cout << __FUNCTION__ << " " << msg << std::endl;
#endif
      break;
    }
  }

  static void CALLBACK MidiOutCallback(HMIDIOUT midi_out, UINT msg,
      DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2) {
    MidiOutWin* me = reinterpret_cast<MidiOutWin*>(instance);
    ASSERT(midi_out == me->midi_out_ || me->midi_out_ == NULL);
    me->OnCallback(msg, param1, param2);
  }

  HMIDIOUT midi_out_;
};

// static
unique_ptr<MidiOut> MidiOut::Create(const shared_ptr<MidiDeviceInfo>& device) {
  unique_ptr<MidiOutWin> ret(new MidiOutWin(device));
  if (!ret->Init())
    ret.reset();
  return std::move(ret);
}

// static
bool MidiOut::EnumerateDevices(DeviceInfos* devices) {
  UINT count = midiOutGetNumDevs();
  for (UINT i = 0; i < count; ++i) {
    MIDIOUTCAPSA caps = {0};
    if (midiOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
      devices->push_back(
          shared_ptr<MidiDeviceInfo>(new MidiDeviceInfo(i, caps.szPname)));
    }
  }
  return true;
}

}  // namespace midi
