#include "../src/rds/RdsDecoder.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void send(RdsDecoder& decoder, uint8_t group, uint8_t version,
                 uint16_t b, uint16_t c, uint16_t d, uint16_t pi,
                 uint32_t frequency, const char* rt, RdsDecodedFields& out) {
  out = RdsDecodedFields{};
  decoder.process(group, version, b, c, d, pi, frequency, 0, 0, rt, out);
}

static void testOdaAndRtPlus() {
  RdsDecoder decoder;
  RdsDecodedFields out;

  // IEC/PIRA reference vector: 3A block B=0x3016 assigns RT+ to 11A,
  // block D carries AID 0x4BD7. Block C is application message data.
  send(decoder, 3, 0, 0x3016, 0x0000, 0x4BD7, 0x1234, 95400, "", out);
  assert(out.odaUpdated);
  assert(out.odaAid == 0x4BD7);
  assert(out.odaGroupType == 11);
  assert(out.odaGroupVersion == 0);

  const char* text = "Enigma - The Eyes of Truth";
  send(decoder, 11, 0, 0xB008, 0x24A0, 0x2005, 0x1234, 95400, text, out);
  assert(out.rtPlusUpdated);
  assert(out.rtPlusItemRunning);
  assert(out.rtPlus[0].valid);
  assert(out.rtPlus[0].contentType == 1);
  assert(out.rtPlus[0].start == 9);
  assert(out.rtPlus[0].length == 16);
  assert(strcmp(out.rtPlus[0].text, "The Eyes of Truth") == 0);
  assert(out.rtPlus[1].valid);
  assert(out.rtPlus[1].contentType == 4);
  assert(out.rtPlus[1].start == 0);
  assert(out.rtPlus[1].length == 5);
  assert(strcmp(out.rtPlus[1].text, "Enigma") == 0);

  // A temporary encoder data fault (AGTC=11111) must not withdraw RT+.
  send(decoder, 3, 0, 0x301F, 0x0000, 0x4BD7, 0x1234, 95400, "", out);
  send(decoder, 11, 0, 0xB008, 0x24A0, 0x2005, 0x1234, 95400, text, out);
  assert(out.rtPlusUpdated && out.rtPlus[0].valid);

  // An unrelated ODA assignment must not replace or clear the RT+ mapping.
  send(decoder, 3, 0, 0x3016, 0x0000, 0x1235, 0x1234, 95400, "", out);
  send(decoder, 11, 0, 0xB008, 0x24A0, 0x2005, 0x1234, 95400, text, out);
  assert(out.rtPlusUpdated && out.rtPlus[1].valid);

  // AGTC=00000 withdraws only the matching AID.
  send(decoder, 3, 0, 0x3000, 0x0000, 0x4BD7, 0x1234, 95400, "", out);
  send(decoder, 11, 0, 0xB008, 0x24A0, 0x2005, 0x1234, 95400, text, out);
  assert(!out.rtPlusUpdated);
}

static void testAfMethodA() {
  RdsDecoder decoder;
  RdsDecodedFields out;

  // #3 followed by AF1 in the header block, then AF2/AF3.
  send(decoder, 0, 0, 0x0000, 0xE312, 0, 0x1234, 95400, "", out);
  send(decoder, 0, 0, 0x0000, 0x1415, 0, 0x1234, 95400, "", out);
  assert(out.afUpdated);
  assert(out.afCount == 3);
  assert(out.afKHz[0] == 89300);
  assert(out.afKHz[1] == 89500);
  assert(out.afKHz[2] == 89600);

  // LF/MF follows code 250; ITU Regions 1/3 use 9 kHz spacing.
  decoder.reset();
  send(decoder, 0, 0, 0x0000, 0xE3FA, 0, 0x1234, 95400, "", out);
  send(decoder, 0, 0, 0x0000, 0x1011, 0, 0x1234, 95400, "", out);
  send(decoder, 0, 0, 0x0000, 0x1200, 0, 0x1234, 95400, "", out);
  assert(out.afCount == 3);
  assert(out.afKHz[0] == 531);
  assert(out.afKHz[1] == 89200);
  assert(out.afKHz[2] == 89300);
}

static void testAfMethodB() {
  RdsDecoder decoder;
  RdsDecodedFields out;

  // #11, tuned frequency 89.3 MHz, then ascending and descending pairs.
  send(decoder, 0, 0, 0x0000, 0xEB12, 0, 0x1234, 95400, "", out);
  send(decoder, 0, 0, 0x0000, 0x1214, 0, 0x1234, 95400, "", out);
  send(decoder, 0, 0, 0x0000, 0x1512, 0, 0x1234, 95400, "", out);
  assert(out.afMethodBUpdated);
  assert(out.afMethodBCount == 1);
  assert(out.afMethodBLists[0].valid);
  assert(out.afMethodBLists[0].tuningKHz == 89300);
  assert(out.afMethodBLists[0].afCount == 2);
  assert(out.afMethodBLists[0].afKHz[0] == 89500);
  assert(out.afMethodBLists[0].afKHz[1] == 89600);
}

static void testEon() {
  RdsDecoder decoder;
  RdsDecodedFields out;

  // 14A: block C is the information block, block D is PI(ON).
  send(decoder, 14, 0, 0xE000, 0x4142, 0x1234, 0x1111, 95400, "", out);
  assert(out.eonUpdated && out.eonCount == 1);
  assert(out.eon.pi == 0x1234);
  assert(strcmp(out.eon.ps, "AB") == 0);

  // Variant 4: AF method A list for the other network.
  send(decoder, 14, 0, 0xE004, 0xE312, 0x1234, 0x1111, 95400, "", out);
  send(decoder, 14, 0, 0xE004, 0x1415, 0x1234, 0x1111, 95400, "", out);
  assert(out.eon.afCount == 3);
  assert(out.eon.afKHz[0] == 89300);
  assert(out.eon.afKHz[1] == 89500);
  assert(out.eon.afKHz[2] == 89600);

  // Variant 5 maps a tuned FM frequency to the corresponding other-network FM.
  send(decoder, 14, 0, 0xE005, 0x1214, 0x1234, 0x1111, 95400, "", out);
  assert(out.eon.mappedTuningKHz == 89300);
  assert(out.eon.mappedFmValid[0]);
  assert(out.eon.mappedFmKHz[0] == 89500);

  // Variant 9 maps to LF/MF; code 16 is 531 kHz in ITU regions 1 and 3.
  send(decoder, 14, 0, 0xE009, 0x1210, 0x1234, 0x1111, 95400, "", out);
  assert(out.eon.mappedAmValid);
  assert(out.eon.mappedAmKHz == 531);

  // Variant 10 maps across FM bands: Band II tuned, Band I mapped.
  send(decoder, 14, 0, 0xE00A, 0x1276, 0x1234, 0x1111, 95400, "", out);
  assert(out.eon.mappedOtherBandValid);
  assert(out.eon.mappedOtherBandKHz == 75800);

  // Variant 12 linkage information.
  send(decoder, 14, 0, 0xE00C, 0xD123, 0x1234, 0x1111, 95400, "", out);
  assert(out.eon.linkageValid);
  assert(out.eon.linkageActuator);
  assert(out.eon.extendedGeneric);
  assert(out.eon.internationalLink);
  assert(out.eon.linkageSetNumber == 0x123);

  // Variant 13 carries the other network PTY and TA flag.
  send(decoder, 14, 0, 0xE00D, 0x8801, 0x1234, 0x1111, 95400, "", out);
  assert(out.eon.pty == 17);
  assert(out.eon.ta);

  // 14B: block C is PI(TN), block D is PI(ON); TP/TA belong to the ON.
  send(decoder, 14, 1, 0xE019, 0x1111, 0x5678, 0x1111, 95400, "", out);
  assert(out.eonUpdated);
  assert(out.eon.pi == 0x5678);
  assert(out.eon.tp);
  assert(out.eon.ta);
  assert(out.eonCount == 2);
}

static void testEonAfSpecialCode() {
  RdsDecoder decoder;
  RdsDecodedFields out;
  // EON variant 4 also supports code 250 followed by an LF/MF code.
  send(decoder, 14, 0, 0xE004, 0xE3FA, 0x2222, 0x1111, 95400, "", out);
  send(decoder, 14, 0, 0xE004, 0x1011, 0x2222, 0x1111, 95400, "", out);
  send(decoder, 14, 0, 0xE004, 0x1200, 0x2222, 0x1111, 95400, "", out);
  assert(out.eon.afCount == 3);
  assert(out.eon.afKHz[0] == 531);
  assert(out.eon.afKHz[1] == 89200);
  assert(out.eon.afKHz[2] == 89300);
}

static void testSlowLabellingAndLinkage() {
  RdsDecoder decoder;
  RdsDecodedFields out;
  // Group 1A: b15 is LA, b14..b12 variant 0, b7..b0 ECC.
  send(decoder, 1, 0, 0x1000, 0x80E1, 0, 0x1234, 95400, "", out);
  assert(out.linkageActuatorValid && out.linkageActuator);
  assert(out.eccValid && out.ecc == 0xE1);

  // Variant 1 still carries the LA bit but does not carry the ECC.
  send(decoder, 1, 0, 0x1000, 0x9000, 0, 0x1234, 95400, "", out);
  assert(out.linkageActuatorValid && out.linkageActuator);
  assert(!out.eccValid);

  // 15A is long PS, not a TA/MS/DI status group.
  send(decoder, 15, 0, 0xF010, 0, 0, 0x1234, 95400, "", out);
  assert(!out.taValid);
}

static void testPtynSegments() {
  RdsDecoder decoder;
  RdsDecodedFields out;
  send(decoder, 10, 0, 0xA010, 0x4142, 0x4344, 0x1234, 95400, "", out);
  assert(!out.ptynUpdated);
  send(decoder, 10, 0, 0xA011, 0x4546, 0x4748, 0x1234, 95400, "", out);
  assert(out.ptynUpdated);
  assert(strcmp(out.ptyn, "ABCDEFGH") == 0);
}

int main() {
  testOdaAndRtPlus();
  testAfMethodA();
  testAfMethodB();
  testEon();
  testEonAfSpecialCode();
  testSlowLabellingAndLinkage();
  testPtynSegments();
  puts("RDS decoder vectors: PASS");
  return 0;
}
