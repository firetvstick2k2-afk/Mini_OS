#pragma once
#include <Arduino.h>
#include "RadioCore.h"

// Independent, cooperative Dev-style VHF / All-Band spectrum scanner.
// Each instance owns its keypad, sweep state and sample buffer.
class SpectrumScannerService {
public:
  static constexpr uint16_t kMaxPoints = 201;
  enum class Kind : uint8_t { VhfFm, AllBand };
  enum class State : uint8_t { Idle, Input, Running, Done, Aborted, Error };
  struct Point { uint32_t frequencyKHz=0; uint8_t rssi=0; uint8_t snr=0; };

  explicit SpectrumScannerService(Kind kind=Kind::VhfFm) : kind_(kind) {}
  void open();
  bool rotateInput(int16_t delta);
  bool clickInput(bool shortPress, RadioCore& radio);
  void cancel(RadioCore& radio);
  void tick(RadioCore& radio);
  void rotatePeak(int16_t delta, RadioCore& radio);
  bool accept(RadioCore& radio);
  State state() const { return state_; }
  bool isInput() const { return state_==State::Input; }
  bool isRunning() const { return state_==State::Running; }
  uint16_t count() const { return count_; }
  uint16_t selected() const { return selected_; }
  uint16_t capacity() const { return planned_; }
  const Point& point(uint16_t i) const { return points_[i< count_?i:0]; }
  uint32_t startKHz() const { return startKHz_; }
  uint32_t endKHz() const { return endKHz_; }
  uint16_t stepKHz() const { return stepKHz_; }
  uint8_t modeIndex() const { return modeIndex_; }
  uint8_t inputStage() const { return inputStage_; }
  uint8_t keyIndex() const { return keyIndex_; }
  bool inputError() const { return inputError_; }
  const char* inputValue() const { return input_[inputStage_]; }
  const char* modeName() const;
private:
  Kind kind_;
  State state_=State::Idle;
  Point points_[kMaxPoints]{};
  uint16_t count_=0, planned_=0, selected_=0, scanIndex_=0;
  uint32_t startKHz_=88000, endKHz_=108000;
  uint16_t stepKHz_=100;
  uint8_t stepIndex_=0, modeIndex_=0, inputStage_=0, keyIndex_=5;
  char input_[2][12]{};
  bool inputError_=false, savedValid_=false;
  uint32_t tuneStartedMs_=0, lastTunePollMs_=0;
  bool spectrumScanActive_=false;
  RadioCore::SavedState saved_{};
  bool parseValue(uint32_t& value) const;
  bool beginSweep(RadioCore& radio);
  void restore(RadioCore& radio);
  uint32_t minKHz() const { return kind_==Kind::VhfFm?88000UL:100UL; }
  uint32_t maxKHz() const { return kind_==Kind::VhfFm?108000UL:30000UL; }
  const char* const* keys() const;
};
