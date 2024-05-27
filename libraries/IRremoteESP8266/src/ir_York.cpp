// Copyright 2022 Daniele Gobbetti

/// @file
/// @brief Support for the York AC protocol (remote GRYLH2A)

// Note: Most of the code is autogenerated by the provided tools or assembled
// from other support classes

#include "ir_York.h"
#include <algorithm>
#include <cstring>
#ifndef ARDUINO
#include <string>
#endif
#include "IRrecv.h"
#include "IRremoteESP8266.h"
#include "IRsend.h"
#ifdef UNIT_TEST
#include "IRsend_test.h"
#endif
#include "IRtext.h"
#include "IRutils.h"


using irutils::addBoolToString;
using irutils::addModeToString;
using irutils::addFanToString;
using irutils::addTempToString;
using irutils::addLabeledString;
using irutils::minsToString;


// Constants
const uint16_t kYorkHdrMark = 4887;
const uint16_t kYorkBitMark = 612;
const uint16_t kYorkHdrSpace = 2267;
const uint16_t kYorkOneSpace = 1778;
const uint16_t kYorkZeroSpace = 579;
const uint16_t kYorkFreq = 38000;  // Hz. (Guessing the most common frequency.)

#if SEND_YORK
/// Send a 17 Byte / 136 bit York A/C message.
/// Status: ALPHA / Untested.
/// @param[in] data An array of bytes containing the IR command.
/// @param[in] nbytes Nr. of bytes of data in the array. (>=kStateLength)
/// @param[in] repeat Nr. of times the message is to be repeated.
void IRsend::sendYork(const uint8_t data[], const uint16_t nbytes,
                      const uint16_t repeat) {
  if (nbytes < kYorkStateLength)
    return;
  sendGeneric(kYorkHdrMark, kYorkHdrSpace,
              kYorkBitMark, kYorkOneSpace,
              kYorkBitMark, kYorkZeroSpace,
              kYorkBitMark, kDefaultMessageGap,
              data, nbytes, kYorkFreq,
              false, repeat, kDutyDefault);  // false == LSB
}
#endif  // SEND_YORK

#if DECODE_YORK
/// Decode the supplied  message.
/// Status: ALPHA / Tested, some values still are not mapped to the internal
/// state of AC
/// @param[in,out] results Ptr to the data to decode & where to store the decode
/// @param[in] offset The starting index to use when attempting to decode the
///   raw data. Typically/Defaults to kStartOffset.
/// @param[in] nbits The number of data bits to expect.
/// @param[in] strict Flag indicating if we should perform strict matching.
/// @return A boolean. True if it can decode it, false if it can't.
bool IRrecv::decodeYork(decode_results *results, uint16_t offset,
                        const uint16_t nbits, const bool strict) {
  if (strict && nbits != kYorkBits)
    return false;

  uint16_t used = 0;

  used = matchGeneric(results->rawbuf + offset, results->state,
                      results->rawlen - offset, nbits,
                      kYorkHdrMark, kYorkHdrSpace,
                      kYorkBitMark, kYorkOneSpace,
                      kYorkBitMark, kYorkZeroSpace,
                      kYorkBitMark, kDefaultMessageGap,
                      false, _tolerance, kMarkExcess,
                      false);  // LSB
  if (used == 0) return false;  // We failed to find any data.

  // Succes
  results->decode_type = decode_type_t::YORK;
  results->bits = nbits;

  return true;
}
#endif  // DECODE_YORK

//
//
/// Class constructor
/// @param[in] pin GPIO to be used when sending.
/// @param[in] inverted Is the output signal to be inverted?
/// @param[in] use_modulation Is frequency modulation to be used?
IRYorkAc::IRYorkAc(const uint16_t pin, const bool inverted,
                         const bool use_modulation)
      : _irsend(pin, inverted, use_modulation) {
        stateReset();
      }

// Reset the internal state to a fixed known good state.
void IRYorkAc::stateReset() {
  // This resets to a known-good state.
  setRaw(kYorkKnownGoodState);
}

/// Set up hardware to be able to send a message.
void IRYorkAc::begin(void) { _irsend.begin(); }

/// Get the raw state of the object, suitable to be sent with the appropriate
/// IRsend object method.
/// @return A copy of the internal state.
uint8_t *IRYorkAc::getRaw(void) {
  calcChecksum();
  return _.raw;
}

/// Set the internal state from a valid code for this protocol.
/// @param[in] new_code A valid code for this protocol.
/// @param[in] length Length of the code in bytes.
void IRYorkAc::setRaw(const uint8_t new_code[], const uint16_t length) {
  std::memcpy(_.raw, new_code, length);
}

#if SEND_YORK
/// Send the current internal state as an IR message.
/// @param[in] repeat Nr. of times the message will be repeated.
void IRYorkAc::send(const uint16_t repeat) {
  _irsend.sendYork(getRaw(), kYorkStateLength, repeat);
}
#endif  // SEND_YORK

/// Get the current operation mode setting.
/// @return The current operation mode.
uint8_t IRYorkAc::getMode(void) const {
  return _.Mode;
}

/// Set the desired operation mode.
/// @param[in] mode The desired operation mode.
void IRYorkAc::setMode(const uint8_t mode) {
  switch (mode) {
    case kYorkFan:
    case kYorkCool:
    case kYorkHeat:
    case kYorkDry:
      _.Mode = mode;
      break;
    default:
      _.Mode = kYorkAuto;
  }
  setFan(getFan());  // Ensure the fan is at the correct speed for the new mode.
}

/// Convert a stdAc::opmode_t enum into its native mode.
/// @param[in] mode The enum to be converted.
/// @return The native equivalent of the enum.
uint8_t IRYorkAc::convertMode(const stdAc::opmode_t mode) {
  switch (mode) {
    case stdAc::opmode_t::kCool: return kYorkCool;
    case stdAc::opmode_t::kHeat: return kYorkHeat;
    case stdAc::opmode_t::kDry:  return kYorkDry;
    case stdAc::opmode_t::kFan:  return kYorkFan;
    default:                     return kYorkAuto;
  }
}

/// Convert a native mode into its stdAc equivalent.
/// @param[in] mode The native setting to be converted.
/// @return The stdAc equivalent of the native setting.
stdAc::opmode_t IRYorkAc::toCommonMode(const uint8_t mode) {
  switch (mode) {
    case kYorkCool: return stdAc::opmode_t::kCool;
    case kYorkHeat: return stdAc::opmode_t::kHeat;
    case kYorkDry:  return stdAc::opmode_t::kDry;
    case kYorkFan:  return stdAc::opmode_t::kFan;
    default:        return stdAc::opmode_t::kAuto;
  }
}

/// Set the speed of the fan.
/// @param[in] speed The desired setting.
/// @note The fan speed is locked to Low when in Dry mode, to auto when in auto
/// mode. "Fan" mode has no support for "auto" speed.
void IRYorkAc::setFan(const uint8_t speed) {
  switch (getMode()) {
    case kYorkDry:
      _.Fan = kYorkFanLow;
      break;
    case kYorkFan:
      _.Fan = std::min(speed, kYorkFanHigh);
      break;
    case kYorkAuto:
      _.Fan = kYorkFanAuto;
      break;
    default:
      _.Fan = std::min(speed, kYorkFanAuto);
  }
}

/// Get the current fan speed setting.
/// @return The current fan speed.
uint8_t IRYorkAc::getFan(void) const {
  return _.Fan;
}

/// Convert a stdAc::fanspeed_t enum into it's native speed.
/// @param[in] speed The enum to be converted.
/// @return The native equivalent of the enum.
uint8_t IRYorkAc::convertFan(const stdAc::fanspeed_t speed) {
  switch (speed) {
    case stdAc::fanspeed_t::kMin:
    case stdAc::fanspeed_t::kLow:
      return kYorkFanLow;
    case stdAc::fanspeed_t::kMedium:
      return kYorkFanMedium;
    case stdAc::fanspeed_t::kHigh:
    case stdAc::fanspeed_t::kMax:
      return kYorkFanHigh;
    default:
      return kYorkFanAuto;
  }
}

/// Convert a native fan speed into its stdAc equivalent.
/// @param[in] speed The native setting to be converted.
/// @return The stdAc equivalent of the native setting.
stdAc::fanspeed_t IRYorkAc::toCommonFanSpeed(const uint8_t speed) {
  switch (speed) {
    case kYorkFanHigh:   return stdAc::fanspeed_t::kMax;
    case kYorkFanMedium: return stdAc::fanspeed_t::kMedium;
    case kYorkFanLow:    return stdAc::fanspeed_t::kMin;
    default:             return stdAc::fanspeed_t::kAuto;
  }
}

/// Set the temperature.
/// @param[in] degrees The temperature in degrees celsius.
void IRYorkAc::setTemp(const uint8_t degrees) {
  _.Temp = std::min(kYorkMaxTemp, std::max(kYorkMinTemp, degrees));
}

/// Get the current temperature setting.
/// @return Get current setting for temp. in degrees celsius.
uint8_t IRYorkAc::getTemp(void) const {
  return _.Temp;
}

/// Set the On Timer value of the A/C.
/// @param[in] nr_of_mins The number of minutes the timer should be.
/// @note The timer time only has a resolution of 10 mins.
/// @note Setting the On Timer active will cancel the Sleep timer/setting.
void IRYorkAc::setOnTimer(const uint16_t nr_of_mins) {
  _.OnTimer = nr_of_mins / 10;
}

/// Set the Off Timer value of the A/C.
/// @param[in] nr_of_mins The number of minutes the timer should be.
/// @note The timer time only has a resolution of 10 mins.
/// @note Setting the Off Timer active will cancel the Sleep timer/setting.
void IRYorkAc::setOffTimer(const uint16_t nr_of_mins) {
  _.OffTimer = nr_of_mins / 10;
}


/// Get the On Timer setting of the A/C.
/// @return The Nr. of minutes the On Timer is set for.
uint16_t IRYorkAc::getOnTimer(void) const {
  return _.OnTimer * 10;
}

/// Get the Off Timer setting of the A/C.
/// @return The Nr. of minutes the Off Timer is set for.
/// @note Sleep & Off Timer share the same timer.
uint16_t IRYorkAc::getOffTimer(void) const {
  return _.OffTimer * 10;
}

/// CRC16-16 (a.k.a. CRC-16-IBM)
void IRYorkAc::calcChecksum() {
  uint8_t length = 14;
  uint16_t reg_crc = 0x0000;
  uint8_t* data = _.raw;
  while (length--) {
    reg_crc ^= *data++;
    for (uint16_t index = 0; index < 8; index++) {
      if (reg_crc & 0x01) {
        reg_crc = (reg_crc>>1) ^ 0xA001;
      } else {
        reg_crc = reg_crc >>1;
      }
    }
  }
  _.Chk1 = (reg_crc & 0xff);
  _.Chk2 = ((reg_crc >> 8) & 0x00ff);
}

/// Convert the current internal state into its stdAc::state_t equivalent.
/// @param[in] prev Ptr to the previous state if required.
/// @return The stdAc equivalent of the native settings.
stdAc::state_t IRYorkAc::toCommon(const stdAc::state_t *prev) const {
  stdAc::state_t result{};
  // Start with the previous state if given it.
  if (prev != NULL) {
    result = *prev;
  } else {
    // Set defaults for non-zero values that are not implicitly set for when
    // there is no previous state.
    // e.g. Any setting that toggles should probably go here.
    result.power = false;
  }
  result.protocol = decode_type_t::YORK;
  result.mode = toCommonMode(_.Mode);
  result.celsius = true;
  result.degrees = getTemp();
  result.fanspeed = toCommonFanSpeed(_.Fan);
  result.swingv = _.SwingV ? stdAc::swingv_t::kAuto : stdAc::swingv_t::kOff;
  result.sleep = getOffTimer();
  // Not supported.
  result.model = -1;
  result.turbo = false;
  result.swingh = stdAc::swingh_t::kOff;
  result.light = false;
  result.filter = false;
  result.econo = false;
  result.quiet = false;
  result.clean = false;
  result.beep = false;
  result.clock = -1;
  return result;
}

/// Convert the current internal state into a human readable string.
/// @return A human readable string.
String IRYorkAc::toString(void) const {
  String result = "";
  result.reserve(70);  // Reserve some heap for the string to reduce fragging.
  result += addBoolToString(_.Power, kPowerStr, false);

  result += addModeToString(_.Mode, kYorkAuto, kYorkCool,
                            kYorkHeat, kYorkDry, kYorkFan);
  result += addFanToString(_.Fan, kYorkFanHigh, kYorkFanLow,
                           kYorkFanAuto, kYorkFanAuto,
                           kYorkFanMedium);
  result += addTempToString(getTemp(), true);
  result += addBoolToString(_.SwingV, kSwingVStr);
  result += addLabeledString(minsToString(getOnTimer()), kOnTimerStr);
  result += addLabeledString(minsToString(getOffTimer()), kOffTimerStr);

  return result;
}
