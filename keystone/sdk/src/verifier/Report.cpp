//******************************************************************************
// Copyright (c) 2018, The Regents of the University of California (Regents).
// All Rights Reserved. See LICENSE for license details.
//------------------------------------------------------------------------------
#include <Report.hpp>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include "ed25519/ed25519.h"

using json11::Json;
std::string
Report::BytesToHex(byte* bytes, size_t len) {
  unsigned int i;
  std::string str;
  for (i = 0; i < len; i += 1) {
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(2) << std::hex << (uintptr_t)bytes[i];

    str += ss.str();
  }
  return str;
}

void
Report::HexToBytes(byte* bytes, size_t len, std::string hexstr) {
  unsigned int i;
  for (i = 0; i < len; i++) {
    unsigned int data = 0;
    std::stringstream ss;
    ss << hexstr.substr(i * 2, 2);
    ss >> std::hex >> data;
    bytes[i] = (byte)data;
  }
}

bool Report::fromJsonChecked(const std::string &jsonstr)
{
    std::string error;
    Json json;
    double length;
    std::string value;
    size_t expected;
    const char *fields[] = {"device_pubkey",	"sm_hash",	"sm_pubkey",
			    "sm_signature",	"enclave_hash", "enclave_data",
			    "enclave_signature"};
    size_t sizes[] = {32, 64, 32, 64, 64, 0, 64};
    size_t i;
    size_t j;
    unsigned char c;

    memset(&report, 0, sizeof report);
    report.enclave.data_len = UINT64_MAX;
    json = Json::parse(jsonstr, error);
    if (!error.empty() || !json["enclave"]["datalen"].is_number()) {
	return false;
    }
    length = json["enclave"]["datalen"].number_value();
    if (!(length >= 0 && length <= ATTEST_DATA_MAXLEN) ||
	length != static_cast<uint64_t>(length)) {
	return false;
    }
    sizes[5] = static_cast<size_t>(length);
    for (i = 0; i < 7; i++) {
	if (i == 0) {
	    value = json[fields[i]].string_value();
	} else if (i < 4) {
	    const char *names[] = {"", "hash", "pubkey", "signature"};
	    value = json["security_monitor"][names[i]].string_value();
	} else {
	    const char *names[] = {"hash", "data", "signature"};
	    value = json["enclave"][names[i - 4]].string_value();
	}
	expected = sizes[i] * 2;
	if (value.size() != expected) {
	    return false;
	}
	for (j = 0; j < value.size(); j++) {
	    c = value[j];
	    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
		  (c >= 'A' && c <= 'F'))) {
		return false;
	    }
	}
    }
    HexToBytes(report.dev_public_key, 32, json["device_pubkey"].string_value());
    HexToBytes(report.sm.hash, 64,
	       json["security_monitor"]["hash"].string_value());
    HexToBytes(report.sm.public_key, 32,
	       json["security_monitor"]["pubkey"].string_value());
    HexToBytes(report.sm.signature, 64,
	       json["security_monitor"]["signature"].string_value());
    HexToBytes(report.enclave.hash, 64, json["enclave"]["hash"].string_value());
    report.enclave.data_len = static_cast<uint64_t>(length);
    HexToBytes(report.enclave.data, report.enclave.data_len,
	       json["enclave"]["data"].string_value());
    HexToBytes(report.enclave.signature, 64,
	       json["enclave"]["signature"].string_value());
    return true;
}

void Report::fromJson(std::string jsonstr)
{
    (void)fromJsonChecked(jsonstr);
}

bool Report::fromBytesChecked(const byte *bin, size_t size)
{
    memset(&report, 0, sizeof report);
    report.enclave.data_len = UINT64_MAX;
    if (!bin || size != sizeof report) {
	return false;
    }
    memcpy(&report, bin, sizeof report);
    return report.enclave.data_len <= ATTEST_DATA_MAXLEN;
}

void Report::fromBytes(byte *bin)
{
    (void)fromBytesChecked(bin, sizeof report);
}

std::string
Report::stringfy() {
  if (report.enclave.data_len > ATTEST_DATA_MAXLEN) {
    return "{ \"error\" : \"invalid data length\" }";
  }
  auto json = Json::object{
      {"device_pubkey", BytesToHex(report.dev_public_key, PUBLIC_KEY_SIZE)},
      {
          "security_monitor",
          Json::object{
              {"hash", BytesToHex(report.sm.hash, MDSIZE)},
              {"pubkey", BytesToHex(report.sm.public_key, PUBLIC_KEY_SIZE)},
              {"signature", BytesToHex(report.sm.signature, SIGNATURE_SIZE)}},
      },
      {
          "enclave",
          Json::object{
              {"hash", BytesToHex(report.enclave.hash, MDSIZE)},
              {"datalen", static_cast<int>(report.enclave.data_len)},
              {"data",
               BytesToHex(report.enclave.data, report.enclave.data_len)},
              {"signature",
               BytesToHex(report.enclave.signature, SIGNATURE_SIZE)},
          },
      },
  };

  return json11::Json(json).dump();
}

void
Report::printJson() {
  std::cout << stringfy() << std::endl;
}

void
Report::printPretty() {
    if (report.enclave.data_len > ATTEST_DATA_MAXLEN) {
	std::cout << "invalid report data length" << std::endl;
	return;
    }
  std::cout << "\t\t=== Security Monitor ===" << std::endl;
  std::cout << "Hash: " << BytesToHex(report.sm.hash, MDSIZE) << std::endl;
  std::cout << "Pubkey: " << BytesToHex(report.sm.public_key, PUBLIC_KEY_SIZE)
            << std::endl;
  std::cout << "Signature: " << BytesToHex(report.sm.signature, SIGNATURE_SIZE)
            << std::endl;
  std::cout << std::endl << "\t\t=== Enclave Application ===" << std::endl;
  std::cout << "Hash: " << BytesToHex(report.enclave.hash, MDSIZE) << std::endl;
  std::cout << "Signature: "
            << BytesToHex(report.enclave.signature, SIGNATURE_SIZE)
            << std::endl;
  std::cout << "Enclave Data: "
            << BytesToHex(report.enclave.data, report.enclave.data_len)
            << std::endl;
  std::cout << "\t\t-- Device pubkey --" << std::endl;
  std::cout << BytesToHex(report.dev_public_key, PUBLIC_KEY_SIZE) << std::endl;
}

byte*
Report::getEnclaveHash() {
    return report.enclave.hash;
}

byte*
Report::getSmHash() {
    return report.sm.hash;
}

int
Report::verify(
    const byte* expected_enclave_hash, const byte* expected_sm_hash,
    const byte* dev_public_key) {
  /* verify that enclave hash matches */
  int encl_hash_valid =
      memcmp(expected_enclave_hash, report.enclave.hash, MDSIZE) == 0;
  int sm_hash_valid = memcmp(expected_sm_hash, report.sm.hash, MDSIZE) == 0;

  int signature_valid = checkSignaturesOnly(dev_public_key);

  return encl_hash_valid && sm_hash_valid && signature_valid;
}

int
Report::checkSignaturesOnly(const byte* dev_public_key) {
    int sm_valid = 0;
    int enclave_valid = 0;

    if (!dev_public_key || report.enclave.data_len > ATTEST_DATA_MAXLEN ||
	memcmp(report.dev_public_key, dev_public_key, PUBLIC_KEY_SIZE)) {
	return 0;
    }

  /* verify SM report */
  sm_valid = ed25519_verify(
      report.sm.signature, reinterpret_cast<byte*>(&report.sm),
      MDSIZE + PUBLIC_KEY_SIZE, dev_public_key);

  /* verify Enclave report */
  enclave_valid = ed25519_verify(
      report.enclave.signature, reinterpret_cast<byte*>(&report.enclave),
      MDSIZE + sizeof(uint64_t) + report.enclave.data_len,
      report.sm.public_key);

  return sm_valid && enclave_valid;
}

void*
Report::getDataSection() {
  return report.enclave.data;
}

size_t
Report::getDataSize() {
  return report.enclave.data_len;
}
