#include "LoginBlob.h"

#include <stdio.h>           // for sprintf
#include <initializer_list>  // for initializer_list
#include <stdexcept>

#include "BellLogger.h"                  // for AbstractLogger
#include "ConstantParameters.h"          // for brandName, cspot, protoc...
#include "Logger.h"                      // for CSPOT_LOG
#include "protobuf/authentication.pb.h"  // for AuthenticationType_AUTHE...
#ifdef BELL_ONLY_CJSON
#include "cJSON.h"
#else
#include "nlohmann/detail/json_pointer.hpp"  // for json_pointer<>::string_t
#include "nlohmann/json.hpp"      // for basic_json<>::object_t, basic_json
#include "nlohmann/json_fwd.hpp"  // for json
#endif

using namespace cspot;

LoginBlob::LoginBlob(std::string name) {
  char hash[32];
  sprintf(hash, "%016zu", std::hash<std::string>{}(name));
  // base is 142137fd329622137a14901634264e6f332e2411
  this->deviceId = std::string("142137fd329622137a149016") + std::string(hash);
  this->crypto = std::make_unique<Crypto>();
  this->name = name;

  this->crypto->dhInit();
}

std::vector<uint8_t> LoginBlob::decodeBlob(
    const std::vector<uint8_t>& blob, const std::vector<uint8_t>& sharedKey) {
  // 0:16 - iv; 17:-20 - blob; -20:0 - checksum
  if (blob.size() <= 36) throw std::invalid_argument("Truncated ZeroConf blob");
  auto iv = std::vector<uint8_t>(blob.begin(), blob.begin() + 16);
  auto encrypted = std::vector<uint8_t>(blob.begin() + 16, blob.end() - 20);
  auto checksum = std::vector<uint8_t>(blob.end() - 20, blob.end());

  // baseKey = sha1(sharedKey) 0:16
  crypto->sha1Init();

  crypto->sha1Update(sharedKey);
  auto baseKey = crypto->sha1FinalBytes();
  baseKey = std::vector<uint8_t>(baseKey.begin(), baseKey.begin() + 16);

  auto checksumMessage = std::string("checksum");
  auto checksumKey = crypto->sha1HMAC(
      baseKey,
      std::vector<uint8_t>(checksumMessage.begin(), checksumMessage.end()));

  auto encryptionMessage = std::string("encryption");
  auto encryptionKey = crypto->sha1HMAC(
      baseKey,
      std::vector<uint8_t>(encryptionMessage.begin(), encryptionMessage.end()));

  auto mac = crypto->sha1HMAC(checksumKey, encrypted);

  // Check checksum
  if (mac != checksum) {
    throw std::invalid_argument("ZeroConf blob checksum mismatch");
  }

  encryptionKey =
      std::vector<uint8_t>(encryptionKey.begin(), encryptionKey.begin() + 16);
  crypto->aesCTRXcrypt(encryptionKey, iv, encrypted.data(), encrypted.size());

  return encrypted;
}

uint32_t LoginBlob::readBlobInt(const std::vector<uint8_t>& data) {
  if (blobSkipPosition >= data.size()) throw std::invalid_argument("Truncated credential integer");
  auto lo = data[blobSkipPosition];
  if ((int)(lo & 0x80) == 0) {
    this->blobSkipPosition += 1;
    return lo;
  }

  if (data.size()-blobSkipPosition < 2) throw std::invalid_argument("Truncated credential integer");
  auto hi = data[blobSkipPosition + 1];
  this->blobSkipPosition += 2;

  return (uint32_t)((lo & 0x7f) | (hi << 7));
}

std::vector<uint8_t> LoginBlob::decodeBlobSecondary(
    const std::vector<uint8_t>& blob, const std::string& username,
    const std::string& deviceId) {
  auto encryptedString = std::string(blob.begin(), blob.end());
  auto blobData = crypto->base64Decode(encryptedString);
  if (blobData.size()<16 || blobData.size()%16)
    throw std::invalid_argument("Invalid credential block length");

  crypto->sha1Init();
  crypto->sha1Update(std::vector<uint8_t>(deviceId.begin(), deviceId.end()));
  auto secret = crypto->sha1FinalBytes();
  auto pkBaseKey = crypto->pbkdf2HmacSha1(
      secret, std::vector<uint8_t>(username.begin(), username.end()), 256, 20);

  crypto->sha1Init();
  crypto->sha1Update(pkBaseKey);
  auto key = std::vector<uint8_t>({0x00, 0x00, 0x00, 0x14});  // len of base key
  auto baseKeyHashed = crypto->sha1FinalBytes();
  key.insert(key.begin(), baseKeyHashed.begin(), baseKeyHashed.end());

  crypto->aesECBdecrypt(key, blobData);

  auto l = blobData.size();

  for (size_t i=l; i>16; --i) {
    blobData[i-1] ^= blobData[i-17];
  }

  return blobData;
}

void LoginBlob::loadZeroconf(const std::vector<uint8_t>& blob,
                             const std::vector<uint8_t>& sharedKey,
                             const std::string& deviceId,
                             const std::string& username) {

  auto partDecoded = this->decodeBlob(blob, sharedKey);
  auto loginData = this->decodeBlobSecondary(partDecoded, username, deviceId);

  // Parse blob
  auto advance = [&](size_t count) {
    if (blobSkipPosition>loginData.size() || count>loginData.size()-blobSkipPosition)
      throw std::invalid_argument("Truncated credential field");
    blobSkipPosition+=count;
  };
  blobSkipPosition = 0;
  advance(1);
  auto userSize = readBlobInt(loginData);
  advance(userSize);
  advance(1);
  auto newAuthType = readBlobInt(loginData);
  advance(1);
  auto authSize = readBlobInt(loginData);
  if (!authSize || authSize>loginData.size()-blobSkipPosition)
    throw std::invalid_argument("Invalid credential payload length");
  auto newAuthData = std::vector<uint8_t>(loginData.begin()+blobSkipPosition,
                                        loginData.begin()+blobSkipPosition+authSize);
  // Do not replace an existing identity when a new blob is rejected.
  this->username = username;
  this->authType = newAuthType;
  this->authData = std::move(newAuthData);
}

void LoginBlob::loadUserPass(const std::string& username,
                             const std::string& password) {
  this->username = username;
  this->authData = std::vector<uint8_t>(password.begin(), password.end());
  this->authType =
      static_cast<uint32_t>(AuthenticationType_AUTHENTICATION_USER_PASS);
}

void LoginBlob::loadJson(const std::string& json) {
#ifdef BELL_ONLY_CJSON
  cJSON* root = cJSON_Parse(json.c_str());
  this->authType = cJSON_GetObjectItem(root, "authType")->valueint;
  this->username = cJSON_GetObjectItem(root, "username")->valuestring;
  std::string authDataObject =
      cJSON_GetObjectItem(root, "authData")->valuestring;
  this->authData = crypto->base64Decode(authDataObject);
  cJSON_Delete(root);
#else
  auto root = nlohmann::json::parse(json);
  this->authType = root["authType"];
  this->username = root["username"];
  std::string authDataObject = root["authData"];

  this->authData = crypto->base64Decode(authDataObject);
#endif
}

std::string LoginBlob::toJson() {
#ifdef BELL_ONLY_CJSON
  cJSON* json_obj = cJSON_CreateObject();
  cJSON_AddStringToObject(json_obj, "authData",
                          crypto->base64Encode(authData).c_str());
  cJSON_AddNumberToObject(json_obj, "authType", this->authType);
  cJSON_AddStringToObject(json_obj, "username", this->username.c_str());

  char* str = cJSON_PrintUnformatted(json_obj);
  cJSON_Delete(json_obj);
  std::string json_objStr(str);
  free(str);

  return json_objStr;
#else
  nlohmann::json obj;
  obj["authData"] = crypto->base64Encode(authData);
  obj["authType"] = this->authType;
  obj["username"] = this->username;

  return obj.dump();
#endif
}

void LoginBlob::loadZeroconfQuery(
    std::map<std::string, std::string>& queryParams) {
  // Get all urlencoded params
  const auto& username = queryParams.at("userName");
  const auto& blobString = queryParams.at("blob");
  const auto& clientKeyString = queryParams.at("clientKey");
  if (username.empty() || blobString.empty() || clientKeyString.empty())
    throw std::invalid_argument("Missing ZeroConf credentials");

  // client key and bytes are urlencoded
  auto clientKeyBytes = crypto->base64Decode(clientKeyString);
  auto blobBytes = crypto->base64Decode(blobString);
  if (clientKeyBytes.empty() || clientKeyBytes.size()>DH_KEY_SIZE || blobBytes.size()<=36)
    throw std::invalid_argument("Invalid ZeroConf key or blob length");

  // Generated secret based on earlier generated DH
  auto secretKey = crypto->dhCalculateShared(clientKeyBytes);

  this->loadZeroconf(blobBytes, secretKey, deviceId, username);
}

std::string LoginBlob::buildZeroconfInfo() {
  // Encode publicKey into base64

  auto encodedKey = crypto->base64Encode(crypto->publicKey);
#ifdef BELL_ONLY_CJSON
  cJSON* json_obj = cJSON_CreateObject();
  cJSON_AddNumberToObject(json_obj, "status", 101);
  cJSON_AddStringToObject(json_obj, "statusString", "OK");
  cJSON_AddStringToObject(json_obj, "version", cspot::protocolVersion);
  cJSON_AddStringToObject(json_obj, "libraryVersion", cspot::swVersion);
  cJSON_AddStringToObject(json_obj, "accountReq", "PREMIUM");
  cJSON_AddStringToObject(json_obj, "brandDisplayName", cspot::brandName);
  cJSON_AddStringToObject(json_obj, "modelDisplayName", name.c_str());
  cJSON_AddStringToObject(json_obj, "voiceSupport", "NO");
  cJSON_AddStringToObject(json_obj, "availability", this->username.c_str());
  cJSON_AddNumberToObject(json_obj, "productID", 0);
  cJSON_AddStringToObject(json_obj, "tokenType", "default");
  cJSON_AddStringToObject(json_obj, "groupStatus", "NONE");
  cJSON_AddStringToObject(json_obj, "resolverVersion", "0");
  cJSON_AddStringToObject(json_obj, "scope",
                          "streaming,client-authorization-universal");
  cJSON_AddStringToObject(json_obj, "activeUser", "");
  cJSON_AddStringToObject(json_obj, "deviceID", deviceId.c_str());
  cJSON_AddStringToObject(json_obj, "remoteName", name.c_str());
  cJSON_AddStringToObject(json_obj, "publicKey", encodedKey.c_str());
  cJSON_AddStringToObject(json_obj, "deviceType", "deviceType");

  char* str = cJSON_PrintUnformatted(json_obj);
  cJSON_Delete(json_obj);
  std::string json_objStr(str);
  free(str);

  return json_objStr;
#else
  nlohmann::json obj;
  obj["status"] = 101;
  obj["statusString"] = "OK";
  obj["version"] = cspot::protocolVersion;
  obj["spotifyError"] = 0;
  obj["libraryVersion"] = cspot::swVersion;
  obj["accountReq"] = "PREMIUM";
  obj["brandDisplayName"] = cspot::brandName;
  obj["modelDisplayName"] = name;
  obj["voiceSupport"] = "NO";
  obj["availability"] = this->username;
  obj["productID"] = 0;
  obj["tokenType"] = "default";
  obj["groupStatus"] = "NONE";
  obj["resolverVersion"] = "0";
  obj["scope"] = "streaming,client-authorization-universal";
  obj["activeUser"] = "";
  obj["deviceID"] = deviceId;
  obj["remoteName"] = name;
  obj["publicKey"] = encodedKey;
  obj["deviceType"] = "SPEAKER";

  return obj.dump();
#endif
}

std::string LoginBlob::getDeviceId() {
  return this->deviceId;
}
std::string LoginBlob::getDeviceName() {
  return this->name;
}
std::string LoginBlob::getUserName() {
  return this->username;
}
