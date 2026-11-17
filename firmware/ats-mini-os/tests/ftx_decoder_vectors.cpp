#include "../src/digital/ftx/FtxDecoder.h"
#include "../src/digital/ftx/ft8_lib/ft8/constants.h"
#include "../src/digital/ftx/ft8_lib/ft8/encode.h"
#include "../src/digital/ftx/ft8_lib/ft8/message.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
namespace {
constexpr int kFs = 4000;
constexpr float kPi = 3.14159265358979323846f;
std::vector<float> makeSignal(const uint8_t* tones, int symbols, float carrier, float period, float bt) {
  const int sps = static_cast<int>(std::lround(kFs * period));
  const int length = symbols * sps;
  const float peak = 2.0f * kPi / sps;
  std::vector<float> pulse(3 * sps), dphi(length + 2 * sps, 2.0f * kPi * carrier / kFs), signal(length);
  constexpr float k = 5.336446f;
  for (int i = 0; i < 3 * sps; ++i) {
    const float t = static_cast<float>(i) / sps - 1.5f;
    pulse[i] = (std::erf(k * bt * (t + 0.5f)) - std::erf(k * bt * (t - 0.5f))) * 0.5f;
  }
  for (int n = 0; n < symbols; ++n)
    for (int j = 0; j < 3 * sps; ++j) dphi[j + n * sps] += peak * tones[n] * pulse[j];
  for (int j = 0; j < 2 * sps; ++j) {
    dphi[j] += peak * pulse[j + sps] * tones[0];
    dphi[j + length] += peak * pulse[j] * tones[symbols - 1];
  }
  float phase = 0.0f;
  for (int i = 0; i < length; ++i) {
    signal[i] = std::sin(phase);
    phase = std::fmod(phase + dphi[i + sps], 2.0f * kPi);
  }
  const int ramp = sps / 8;
  for (int i = 0; i < ramp; ++i) {
    const float env = (1.0f - std::cos(2.0f * kPi * i / (2.0f * ramp))) * 0.5f;
    signal[i] *= env; signal[length - 1 - i] *= env;
  }
  return signal;
}
bool run(FtxMode mode, bool transmit, unsigned seed) {
  const bool ft4 = mode == FtxMode::FT4;
  const int samplesInSlot = static_cast<int>((ft4 ? FT4_SLOT_TIME : FT8_SLOT_TIME) * kFs);
  const int symbolCount = ft4 ? FT4_NN : FT8_NN;
  const float period = ft4 ? FT4_SYMBOL_PERIOD : FT8_SYMBOL_PERIOD;
  const char* expected = "CQ EA1ABC IN52";
  std::mt19937 rng(seed);
  std::normal_distribution<float> noise(0.0f, 0.015f);
  std::vector<float> audio(samplesInSlot);
  for (float& sample : audio) sample = noise(rng);
  if (transmit) {
    ftx_message_t message{};
    if (ftx_message_encode(&message, nullptr, expected) != FTX_MESSAGE_RC_OK) return false;
    uint8_t tones[FT4_NN] = {};
    if (ft4) ft4_encode(message.payload, tones); else ft8_encode(message.payload, tones);
    const auto signal = makeSignal(tones, symbolCount, ft4 ? 900.0f : 700.0f, period, ft4 ? 1.0f : 2.0f);
    const size_t offset = kFs / 2;
    for (size_t i = 0; i < signal.size() && offset + i < audio.size(); ++i) audio[offset + i] += 0.08f * signal[i];
  }
  std::vector<int16_t> adc(audio.size());
  for (size_t i = 0; i < audio.size(); ++i) {
    const int raw = static_cast<int>(std::lround(2048.0f + audio[i] * 1800.0f));
    adc[i] = static_cast<int16_t>(std::clamp(raw, 0, 4095));
  }
  FtxDecoder decoder;
  if (!decoder.begin(mode) || !decoder.startSlot(static_cast<time_t>(1790000000))) return false;
  for (size_t i = 0; i < adc.size(); i += 24) {
    const size_t count = std::min<size_t>(24, adc.size() - i);
    decoder.pushSamples(adc.data() + i, count);
  }
  const bool completed = decoder.slotReady();
  decoder.decodeSlot();
  bool found = false;
  for (size_t i = 0; i < decoder.resultCount(); ++i) found |= std::strcmp(decoder.result(i).text, expected) == 0;
  const bool ok = completed && (transmit ? found : decoder.resultCount() == 0);
  if (!ok) std::fprintf(stderr, "%s %s failed (slot=%d, results=%u)\n", ft4 ? "FT4" : "FT8", transmit ? "decode" : "noise-only", completed ? 1 : 0, static_cast<unsigned>(decoder.resultCount()));
  else std::printf("%s %s: PASS\n", ft4 ? "FT4" : "FT8", transmit ? "decode" : "noise-only");
  decoder.end();
  return ok;
}
}
int main() {
  bool ok = true;
  ok &= run(FtxMode::FT8, true, 1);
  ok &= run(FtxMode::FT4, true, 2);
  ok &= run(FtxMode::FT8, false, 3);
  ok &= run(FtxMode::FT4, false, 4);
  std::printf("FT8/FT4 host vectors: %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
