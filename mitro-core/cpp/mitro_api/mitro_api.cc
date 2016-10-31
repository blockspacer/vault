#include "mitro_api.h"

#include <map>
#include <string>
#include <vector>

#include <keyczar/base/base64w.h>
#include <keyczar/crypto_factory.h>
#include <keyczar/keyczar.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/Thrift.h>

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "keyczar_json/json_keyset_reader.h"
#include "mitro_api_types.h"
#include "net/http_client.h"
#include "net/uri.h"
#include "TSimpleJSONProtocol.hpp"
#include "json/json.h"

using apache::thrift::protocol::TSimpleJSONProtocol;
using apache::thrift::transport::TMemoryBuffer;
using base::Bind;
using base::Callback;
using base::StringPrintf;
using mitro::MitroPrivateKey;
using net::HttpClient;
using net::HttpHeaders;
using net::HttpRequestCallback;
using net::HttpResponse;
using std::map;
using std::string;
using std::vector;

namespace mitro_api {

  const char* MitroApiClient::kProtocol = "https";
  const char* MitroApiClient::kDefaultHost = "api.vaultapp.xyz";
  const char* MitroApiClient::kApiBasePath = "mitro-core/api";

  MitroApiClient::MitroApiClient(HttpClient* http_client)
  : host_(kDefaultHost),
  http_client_(http_client),
  transport_(new TMemoryBuffer(kTransportBufferSize)),
  protocol_(new TSimpleJSONProtocol(transport_)) {
  }

  /* static */
  string MitroApiClient::GenerateDeviceID() {
    string device_id;
    CHECK(keyczar::CryptoFactory::Rand()->RandBytes(kDeviceIDSize, &device_id));

    string device_id_string;
    CHECK(keyczar::base::Base64WEncode(device_id, &device_id_string));

    return device_id_string;
  }

  const string& MitroApiClient::GetDeviceID() {
    if (device_id_.empty()) {
      device_id_ = GenerateDeviceID();
    }

    return device_id_;
  }

  void MitroApiClient::PostRequest(const string& endpoint,
                                   const SignedRequest& request,
                                   const HttpRequestCallback& callback) {
    // (sully)
    request.write(protocol_.get());
    string json_string = transport_.get()->getBufferAsString();
    transport_->resetBuffer();

    const string& data = json_string;
    LOG(INFO) << "request: " << data;

    string url = net::BuildUri(kProtocol, GetHost(), kApiBasePath + endpoint);
    HttpHeaders headers;
    http_client_->Post(url, headers, data, callback);
  }

  void MitroApiClient::SignAndPostRequest(const string& endpoint,
                                          const string& request_string,
                                          const HttpRequestCallback& callback) {
    SignedRequest request_wrapper;

    request_wrapper.__set_clientIdentifier(GetClientID());
    request_wrapper.__set_identity(GetUsername());
    request_wrapper.__set_request(request_string);

    if (endpoint != "/GetMyPrivateKey") {
      CHECK(user_private_key_.get() != NULL);
      // TODO: Can signing fail?
      string signature;
      CHECK(user_private_key_->Sign(request_string, &signature));
      request_wrapper.__set_signature(signature);
    }

    LOG(INFO) << endpoint << " " << request_string;
    PostRequest(endpoint, request_wrapper, callback);
  }

  void MitroApiClient::Login(const string& username,
                             const string& password,
                             const string& two_factor_auth_code,
                             const string& login_token,
                             const string& login_token_signature,
                             const string& encrypted_private_key,
                             bool save_private_key,
                             const LoginCallback& callback) {
    if (IsLoggedIn()) {
      Logout();
    }

    SetUsername(username);

    GetMyPrivateKeyRequest request;
    request.__set_userId(GetUsername());
    request.__set_deviceId(GetDeviceID());
    request.__set_twoFactorCode(two_factor_auth_code);
    request.__set_loginToken(login_token);
    request.__set_loginTokenSignature(login_token_signature);

    HttpRequestCallback request_callback =
    Bind(&MitroApiClient::OnGetMyPrivateKey,
         base::Unretained(this),
         password,
         encrypted_private_key,
         save_private_key,
         callback);

    // Make request
    string endpoint = "/GetMyPrivateKey";
    request.write(protocol_.get());
    string json_string = transport_.get()->getBufferAsString();
    transport_->resetBuffer();

    SignAndPostRequest(endpoint, json_string, request_callback);
  }

  void MitroApiClient::Logout() {
    username_ = "";
    user_group_id_ = -1;
    user_private_key_.reset();
    user_group_private_key_.reset();

    key_cache_.Clear();
    decryption_cache_.clear();
  }

  bool MitroApiClient::ParseResponse(const HttpResponse& response,
                                     Json::Value& root,
                                     MitroApiError* error) {
    MitroException exception;
    Json::Reader reader;

    // Check http response
    if (!response.IsOk()) {
      error->SetMessage(response.GetError().GetMessage());
      return false;
    }

    if (!reader.parse(response.GetBody(), root))
    {
      // report to the user the failure and their locations in the document.
      error->SetMessage(reader.getFormattedErrorMessages());
      return false;
    }

    // Check for server exception
    if (root.isMember("exceptionType")) {
      // See cpp/mitro_api/mitro_api.thrift for optionals
      exception.__set_exceptionId(root["exceptionId"].asString());
      exception.__set_stackTraceString(root["stackTraceString"].asString());
      exception.__set_rawMessage(root["rawMessage"].asString());
      exception.__set_userVisibleError(root["userVisibleError"].asString());
      if (root.isMember("exceptionType"))
        exception.__set_exceptionType(root["exceptionType"].asString());

      error->SetMessage(exception.userVisibleError);
      error->SetExceptionType(exception.exceptionType);

      return false;
    }
    return true;
  }

  void MitroApiClient::OnGetMyPrivateKey(const string& password,
                                         const string& encrypted_private_key,
                                         bool save_private_key,
                                         const LoginCallback& callback,
                                         const HttpResponse& response) {
    //LOG(INFO) << "response: " << response.GetBody();

    MitroApiError error;
    GetMyPrivateKeyResponse private_key_response;
    Json::Value root;
    bool success = true;

    // Parse server exeption
    if (!ParseResponse(response, root, &error)) {
      success = false;
    }

    // Parse GetMyPrivateKeyResponse
    if (success) {
      // See cpp/mitro_api/mitro_api.thrift for optionals
      private_key_response.__set_transactionId(root["transactionId"].asString());
      private_key_response.__set_deviceId(root["deviceId"].asString());
      private_key_response.__set_myUserId(root["myUserId"].asString());
      if (root.isMember("encryptedPrivateKey"))
        private_key_response.__set_encryptedPrivateKey(root["encryptedPrivateKey"].asString());
      private_key_response.__set_changePasswordOnNextLogin(root["changePasswordOnNextLogin"].asBool());
      private_key_response.__set_verified(root["verified"].asBool());
      private_key_response.__set_unsignedLoginToken(root["unsignedLoginToken"].asString());
      if (root.isMember("deviceKeyString"))
        private_key_response.__set_deviceKeyString(root["deviceKeyString"].asString());
    }

    // Proccess response
    if (success) {
      if (!encrypted_private_key.empty() &&
          private_key_response.__isset.deviceKeyString) {
        mitro::JsonKeysetReader reader;
        reader.ReadKeyString(private_key_response.deviceKeyString);
        scoped_ptr<keyczar::Keyczar> crypter(keyczar::Crypter::Read(reader));
        string private_key_string = crypter->Decrypt(encrypted_private_key);

        user_private_key_.reset(new MitroPrivateKey);
        if (!user_private_key_->Read(private_key_string)) {
          user_private_key_.reset();
          error.SetMessage("Error decrypting private key");
        }
      } else if (private_key_response.__isset.encryptedPrivateKey) {
        user_private_key_.reset(new MitroPrivateKey);

        if (!user_private_key_->ReadEncrypted(
                                              private_key_response.encryptedPrivateKey, password)) {
          user_private_key_.reset();
          error.SetMessage("Invalid password");
        } else if (save_private_key) {
          GetDeviceKeyCallback device_key_callback =
          Bind(&MitroApiClient::OnGetMyPrivateKeyDeviceKeyCallback,
               base::Unretained(this),
               callback,
               private_key_response.unsignedLoginToken);
          GetDeviceKey(device_key_callback);
          return;
        }
      } else {
        error.SetMessage("Missing encrypted private key");
      }
    } else if (error.GetMessage().empty()) {
      error.SetMessage("Unknown error parsing private key response");
    }

    if (error.GetMessage().empty()) {
      const string& login_token = private_key_response.unsignedLoginToken;
      FinishGetMyPrivateKey(callback, login_token, "", NULL);
    } else {
      FinishGetMyPrivateKey(callback, "", "", &error);
    }
  }

  void MitroApiClient::OnGetMyPrivateKeyDeviceKeyCallback(
                                                          const LoginCallback& callback,
                                                          const string& login_token,
                                                          const string& device_key_string,
                                                          MitroApiError* error) {
    if (error != NULL) {
      FinishGetMyPrivateKey(callback, login_token, "", error);
      return;
    }

    MitroApiError device_key_error;
    mitro::JsonKeysetReader reader;
    string encrypted_private_key_string;

    if (!reader.ReadKeyString(device_key_string)) {
      device_key_error.SetMessage("Error reading device key");
      FinishGetMyPrivateKey(callback, login_token, "", &device_key_error);
      return;
    }

    scoped_ptr<keyczar::Keyczar> crypter(keyczar::Crypter::Read(reader));
    encrypted_private_key_string = crypter->Encrypt(user_private_key_->ToJson());
    FinishGetMyPrivateKey(callback, login_token, encrypted_private_key_string, NULL);
  }

  void MitroApiClient::FinishGetMyPrivateKey(
                                             const LoginCallback& callback,
                                             const string& login_token,
                                             const string& encrypted_private_key,
                                             MitroApiError* error) {
    if (error != NULL) {
      callback.Run(username_, "", "", "", error);
      return;
    }

    string login_token_signature;
    // TODO: Can signing fail?
    CHECK(user_private_key_->Sign(login_token, &login_token_signature));

    callback.Run(username_, login_token, login_token_signature, encrypted_private_key, NULL);
  }

  void MitroApiClient::GetDeviceKey(const GetDeviceKeyCallback& callback) {
    GetMyDeviceKeyRequest request;
    request.__set_deviceId(GetDeviceID());

    HttpRequestCallback request_callback =
    Bind(&MitroApiClient::OnGetMyDeviceKey, base::Unretained(this), callback);

    // Make request
    string endpoint = "/GetMyDeviceKey";
    request.write(protocol_.get());
    string json_string = transport_.get()->getBufferAsString();
    transport_->resetBuffer();

    SignAndPostRequest(endpoint, json_string, request_callback);
  }

  void MitroApiClient::OnGetMyDeviceKey(const GetDeviceKeyCallback& callback,
                                        const HttpResponse& response) {
    // Parse response
    MitroApiError error;
    GetMyDeviceKeyResponse device_key_response;
    Json::Value root;
    bool success = true;

    // Parse server exeption
    if (!ParseResponse(response, root, &error)) {
      success = false;
    }

    // Parse GetMyDeviceKeyResponse
    if (success) {
      device_key_response.__set_transactionId(root["transactionId"].asString());
      device_key_response.__set_deviceId(root["deviceId"].asString());
      device_key_response.__set_deviceKeyString(root["deviceKeyString"].asString());
    }

    if (!success) {
      callback.Run("", &error);
      return;
    }

    string device_key_string = device_key_response.deviceKeyString;
    LOG(INFO) << "device key: " << device_key_string;

    callback.Run(device_key_string, NULL);
  }

  void MitroApiClient::GetSecretsList(const GetSecretsListCallback& callback) {
    ListMySecretsAndGroupKeysRequest request;

    request.__set_deviceId(GetDeviceID());

    HttpRequestCallback request_callback =
    Bind(&MitroApiClient::OnGetSecretsList,
         base::Unretained(this),
         callback);

    // Make request
    string endpoint = "/ListMySecretsAndGroupKeys";
    request.write(protocol_.get());
    string json_string = transport_.get()->getBufferAsString();
    transport_->resetBuffer();

    SignAndPostRequest(endpoint, json_string, request_callback);
  }

  void MitroApiClient::ParseSecret(const Json::Value &json_secret,
                                   Secret &secret) {
    // Secret
    if (json_secret.isMember("secretId"))
      secret.__set_secretId(json_secret["secretId"].asInt());
    if (json_secret.isMember("hostname"))
      secret.__set_hostname(json_secret["hostname"].asString());
    if (json_secret.isMember("encryptedClientData"))
      secret.__set_encryptedClientData(json_secret["encryptedClientData"].asString());
    if (json_secret.isMember("encryptedCriticalData"))
      secret.__set_encryptedCriticalData(json_secret["encryptedCriticalData"].asString());
    if (json_secret.isMember("groups")) {
      Json::Value groups = json_secret["groups"];
      for(Json::ValueIterator itr = groups.begin() ; itr != groups.end() ; itr++ ) {
        secret.groups.push_back(itr->asInt());
        secret.__isset.groups = true;
      }
    }
    if (json_secret.isMember("hiddenGroups")) {
      Json::Value hidden_groups = json_secret["hiddenGroups"];
      for(Json::ValueIterator itr = hidden_groups.begin() ; itr != hidden_groups.end() ; itr++ ) {
        secret.hiddenGroups.push_back(itr->asInt());
        secret.__isset.hiddenGroups = true;
      }
    }
    if (json_secret.isMember("users")) {
      Json::Value users = json_secret["users"];
      for(Json::ValueIterator itr = users.begin() ; itr != users.end() ; itr++ ) {
        secret.users.push_back(itr->asString());
        secret.__isset.users = true;
      }
    }
    if (json_secret.isMember("icons")) {
      Json::Value icons = json_secret["icons"];
      for(Json::ValueIterator itr = icons.begin() ; itr != icons.end() ; itr++ ) {
        secret.icons.push_back(itr->asString());
        secret.__isset.icons = true;
      }
    }
    if (json_secret.isMember("groupNames")) {

      Json::Value group_names = json_secret["groupNames"];
      Json::Value::Members group_names_keys = group_names.getMemberNames();
      for (vector<string>::iterator it = group_names_keys.begin() ; it != group_names_keys.end(); ++it) {
        string key = *it;
        secret.groupNames[key] = group_names[key].asString();
        secret.__isset.groupNames = true;
      }
    }
    if (json_secret.isMember("title"))
      secret.__set_title(json_secret["title"].asString());
    if (json_secret.isMember("groupIdPath")) {
      Json::Value group_id_path = json_secret["groupIdPath"];
      for(Json::ValueIterator itr = group_id_path.begin() ; itr != group_id_path.end() ; itr++ ) {
        secret.groupIdPath.push_back(itr->asInt());
        secret.__isset.groupIdPath = true;
      }
    }

    // SecretClientData
    if (json_secret.isMember("clientData")) {
      Json::Value client_data = json_secret["clientData"];
      ParseSecretClientData(client_data, secret.clientData);
      secret.__isset.clientData = true;
    }

    // SecretCriticalData
    if (json_secret.isMember("criticalData")) {
      Json::Value critical_data = json_secret["criticalData"];
      ParseSecretCriticalData(critical_data, secret.criticalData);
      secret.__isset.criticalData = true;
    }
  }

  void MitroApiClient::ParseSecretClientData(const Json::Value &json_client_data,
                                             SecretClientData &clientData) {
    // SecretClientData
    if (json_client_data.isMember("type"))
      clientData.__set_type(json_client_data["type"].asString());
    if (json_client_data.isMember("loginUrl"))
      clientData.__set_loginUrl(json_client_data["loginUrl"].asString());
    if (json_client_data.isMember("username"))
      clientData.__set_username(json_client_data["username"].asString());
    if (json_client_data.isMember("usernameField"))
      clientData.__set_usernameField(json_client_data["usernameField"].asString());
    if (json_client_data.isMember("passwordField"))
      clientData.__set_passwordField(json_client_data["passwordField"].asString());
    if (json_client_data.isMember("title"))
      clientData.__set_title(json_client_data["title"].asString());
  }

  void MitroApiClient::ParseSecretCriticalData(const Json::Value &json_critical_data,
                                               SecretCriticalData &criticalData) {
    // SecretCriticalData
    if (json_critical_data.isMember("password"))
      criticalData.__set_password(json_critical_data["password"].asString());
    if (json_critical_data.isMember("note"))
      criticalData.__set_note(json_critical_data["note"].asString());
  }

  void MitroApiClient::ParseGroupInfo(const Json::Value &json_group_info,
                                      GroupInfo &group_info) {
    // GroupInfo
    group_info.__set_groupId(json_group_info["groupId"].asInt());
    group_info.__set_autoDelete(json_group_info["autoDelete"].asBool());
    group_info.__set_name(json_group_info["name"].asString());
    group_info.__set_encryptedPrivateKey(json_group_info["encryptedPrivateKey"].asString());
  }


  void MitroApiClient::OnGetSecretsList(const GetSecretsListCallback& callback,
                                        const HttpResponse& response) {
    //LOG(INFO) << "response: " << response.GetBody();

    MitroApiError error;
    ListMySecretsAndGroupKeysResponse secrets_list_response;
    Json::Value root;
    bool success = true;

    // Parse server exeption
    if (!ParseResponse(response, root, &error)) {
      success = false;
    }

    // Parse ListMySecretsAndGroupKeysResponse
    if (success) {
      secrets_list_response.__set_transactionId(root["transactionId"].asString());
      secrets_list_response.__set_deviceId(root["deviceId"].asString());
      secrets_list_response.__set_myUserId(root["myUserId"].asString());

      Json::Value secret_to_path = root["secretToPath"];
      Json::Value::Members secret_to_path_keys = secret_to_path.getMemberNames();
      for (vector<string>::iterator it = secret_to_path_keys.begin() ; it != secret_to_path_keys.end(); ++it) {
        string key = *it;
        Secret secret;
        Json::Value json_secret = secret_to_path[key];
        ParseSecret(json_secret, secret);
        secrets_list_response.secretToPath[key] = secret;
        secrets_list_response.__isset.secretToPath = true;
      }

      Json::Value groups = root["groups"];
      Json::Value::Members groups_keys = groups.getMemberNames();
      for (vector<string>::iterator it = groups_keys.begin() ; it != groups_keys.end(); ++it) {
        string key = *it;
        GroupInfo group_info;
        Json::Value json_group_info = groups[key];
        ParseGroupInfo(json_group_info, group_info);
        secrets_list_response.groups[key] = group_info;
        secrets_list_response.__isset.groups = true;
      }

      Json::Value autocomplete_users = root["autocompleteUsers"];
      for(Json::ValueIterator itr = autocomplete_users.begin() ; itr != autocomplete_users.end() ; itr++ ) {
        secrets_list_response.autocompleteUsers.push_back(itr->asString());
        secrets_list_response.__isset.autocompleteUsers = true;
      }
      secrets_list_response.printTo(std::cout);
    }

    // Set group id and group key for this username so secrets can be added
    SetUserGroupIdAndKeyFromSecretsListResponse(secrets_list_response);

    if (!success) {
      callback.Run(secrets_list_response, &error);
      return;
    }

    callback.Run(secrets_list_response, NULL);
  }

  void MitroApiClient::GetSecret(int secret_id,
                                 int group_id,
                                 bool include_critical_data,
                                 GetSecretCallback& callback) {
    GetSecretRequest request;

    request.__set_deviceId(GetDeviceID());
    request.__set_userId(GetUsername());
    request.__set_secretId(secret_id);
    request.__set_groupId(group_id);
    request.__set_includeCriticalData(include_critical_data);

    HttpRequestCallback request_callback =
    Bind(&MitroApiClient::OnGetSecret,
         base::Unretained(this),
         callback);

    // Make request
    string endpoint = "/GetSecret";
    request.write(protocol_.get());
    string json_string = transport_.get()->getBufferAsString();
    transport_->resetBuffer();

    SignAndPostRequest(endpoint, json_string, request_callback);
  }

  void MitroApiClient::OnGetSecret(const GetSecretCallback& callback,
                                   const net::HttpResponse& response) {
    Secret empty_secret;
    MitroApiError error;
    GetSecretResponse secret_response;
    Json::Value root;
    bool success = true;

    // Parse server exeption
    if (!ParseResponse(response, root, &error)) {
      success = false;
    }

    // Parse GetSecretResponse
    if (success) {
      secret_response.__set_transactionId(root["transactionId"].asString());
      secret_response.__set_deviceId(root["deviceId"].asString());
      ParseSecret(root["secret"], secret_response.secret);
      secret_response.__isset.secret = true;
    }

    if (!success) {
      callback.Run(empty_secret, &error);
      return;
    }

    secret_response.secret.printTo(std::cout);
    callback.Run(secret_response.secret, NULL);
  }

  void MitroApiClient::AddSecretCommon(const SecretClientData& client_data,
                                       const SecretCriticalData& critical_data,
                                       const AddSecretCallback& callback) {
    AddSecretRequest request;

    // Build json data
    string json_data;
    string json_critical_data;
    client_data.write(protocol_.get());
    json_data = transport_.get()->getBufferAsString();
    transport_->resetBuffer();
    critical_data.write(protocol_.get());
    json_critical_data = transport_.get()->getBufferAsString();
    transport_->resetBuffer();

    // Encrypt data
    string encrypted_data;
    string encrypted_critical_data;
    user_group_private_key_->Encrypt(json_data, &encrypted_data);
    user_group_private_key_->Encrypt(json_critical_data, &encrypted_critical_data);

    // Build request
    request.__set_deviceId(GetDeviceID());
    request.__set_myUserId(GetUsername());
    request.__set_hostname("none");
    request.__set_ownerGroupId(user_group_id_);
    request.__set_encryptedClientData(encrypted_data);
    request.__set_encryptedCriticalData(encrypted_critical_data);

    // Build request string
    string endpoint = "/AddSecret";
    request.write(protocol_.get());
    string json_string = transport_.get()->getBufferAsString();
    transport_->resetBuffer();

    HttpRequestCallback request_callback =
    Bind(&MitroApiClient::OnAddSecret,
         base::Unretained(this),
         callback);

    SignAndPostRequest(endpoint, json_string, request_callback);
  }


  void MitroApiClient::AddSecretPassword(const std::string& title,
                                         const std::string& login_url,
                                         const std::string& username,
                                         const std::string& password,
                                         const AddSecretCallback& callback) {

    // Build secret data
    SecretClientData data;
    SecretCriticalData critical_data;
    data.__set_title(title);
    data.__set_loginUrl(login_url);
    data.__set_username(username);
    data.__set_type("manual");
    critical_data.__set_password(password);

    AddSecretCommon(data, critical_data, callback);
  }

  void MitroApiClient::AddSecretNote(const std::string& title,
                                     const std::string& note,
                                     const AddSecretCallback& callback) {
    // Build secret data
    SecretClientData data;
    SecretCriticalData critical_data;
    data.__set_title(title);
    data.__set_type("note");
    critical_data.__set_note(note);

    AddSecretCommon(data, critical_data, callback);
  }

  void MitroApiClient::OnAddSecret(const AddSecretCallback& callback,
                                   const net::HttpResponse& response) {
    //LOG(INFO) << "response: " << response.GetBody();

    MitroApiError error;
    AddSecretResponse add_secret_response;
    Json::Value root;
    bool success = true;

    // Parse server exeption
    if (!ParseResponse(response, root, &error)) {
      success = false;
    }

    if (!success) {
      callback.Run(&error);
      return;
    }

    callback.Run(NULL);
  }

  void MitroApiClient::SetUserGroupIdAndKeyFromSecretsListResponse(
                                                                   const ListMySecretsAndGroupKeysResponse& secrets_list_response) {
    map<string, GroupInfo>::const_iterator group_iter;
    map<string, GroupInfo>::const_iterator group_end_iter =
    secrets_list_response.groups.end();

    for (group_iter = secrets_list_response.groups.begin();
         group_iter != group_end_iter;
         ++group_iter) {
      if (group_iter->second.name.empty()) {
        string encrypted_private_key = group_iter->second.encryptedPrivateKey;
        user_group_private_key_.reset(user_private_key_->DecryptPrivateKey(encrypted_private_key));
        user_group_id_= group_iter->second.groupId;

        // Success, we're done
        return;
      }
    }
    // No user group info found, set to unknown values
    user_group_id_ = -1;
    user_group_private_key_.reset();
  }

  bool MitroApiClient::DecryptUsingCache(const MitroPrivateKey& key,
                                         const std::string& encrypted_data,
                                         std::string* decrypted_data) {
    DecryptionCache::const_iterator iter = decryption_cache_.find(encrypted_data);
    if (iter == decryption_cache_.end()) {
      if (!key.Decrypt(encrypted_data, decrypted_data)) {
        return false;
      }
      decryption_cache_.insert(
                               std::pair<string, string>(encrypted_data, *decrypted_data));
    } else {
      *decrypted_data = iter->second;
    }

    return true;
  }

  MitroPrivateKey* MitroApiClient::GetDecryptionKeyForSecret(
                                                             const Secret& secret,
                                                             const map<string, GroupInfo>& groups,
                                                             MitroApiError* error) {
    if (!secret.__isset.groupIdPath) {
      error->SetMessage("Secret does not contain group id path");
      return NULL;
    }

    MitroPrivateKey* decryption_key = user_private_key_.get();
    vector<int>::const_iterator group_id_iter;

    for (group_id_iter = secret.groupIdPath.begin();
         group_id_iter != secret.groupIdPath.end();
         ++group_id_iter) {
      map<string, GroupInfo>::const_iterator group_iter;
      group_iter = groups.find(base::IntToString(*group_id_iter));

      if (group_iter == groups.end()) {
        error->SetMessage(StringPrintf("Group %d not found", *group_id_iter));
        return NULL;
      }

      string encrypted_group_key = group_iter->second.encryptedPrivateKey;

      MitroPrivateKey* next_decryption_key = key_cache_.Find(encrypted_group_key);
      if (next_decryption_key == NULL) {
        string group_key_string;
        if (!DecryptUsingCache(*decryption_key,
                               encrypted_group_key,
                               &group_key_string)) {
          error->SetMessage("Error decrypting group key");
          return NULL;
        }

        MitroPrivateKey* group_key = new MitroPrivateKey;
        if (!group_key->Read(group_key_string)) {
          delete group_key;
          error->SetMessage("Error reading group key");
          return NULL;
        }

        key_cache_.Insert(encrypted_group_key, group_key);
        next_decryption_key = group_key;
      }
      decryption_key = next_decryption_key;
    }

    return decryption_key;
  }

  bool MitroApiClient::DecryptSecret(Secret* secret,
                                     const map<string, GroupInfo>& groups,
                                     MitroApiError* error) {
    MitroPrivateKey* decryption_key =
    GetDecryptionKeyForSecret(*secret, groups, error);

    if (decryption_key == NULL) {
      // Error message set inside of GetDecryptionKeyForSecret.
      return false;
    }

    if (secret->__isset.encryptedClientData) {
      string client_data_string;
      if (!DecryptUsingCache(*decryption_key,
                             secret->encryptedClientData,
                             &client_data_string)) {
        error->SetMessage("Error decrypting secret client data");
        return false;
      }

      // Parse json data
      SecretClientData client_data;
      Json::Reader reader;
      Json::Value json_client_data;

      if (reader.parse(client_data_string, json_client_data)) {
        ParseSecretClientData(json_client_data, client_data);
      } else {
        error->SetMessage("Error decrypting secret client data");
        return false;
      }
      secret->clientData = client_data;
    }

    if (secret->__isset.encryptedCriticalData) {
      string critical_data_string;
      if (!decryption_key->Decrypt(secret->encryptedCriticalData,
                                   &critical_data_string)) {
        error->SetMessage("Error decrypting secret critical data");
        return false;
      }

      // Parse json data
      SecretCriticalData critical_data;
      Json::Reader reader;
      Json::Value json_critical_data;

      if (reader.parse(critical_data_string, json_critical_data)) {
        ParseSecretCriticalData(json_critical_data, critical_data);
      } else {
        error->SetMessage("Error decrypting secret critical data");
        return false;
      }
      secret->criticalData = critical_data;
    }

    return true;
  }

  MitroApiClient::KeyCache::~KeyCache() {
    STLDeleteValues(&cache_);
  }

  void MitroApiClient::KeyCache::Insert(const string& encrypted_key_string,
                                        MitroPrivateKey* key) {
    CHECK(Find(encrypted_key_string) == NULL);
    cache_.insert(std::pair<string, MitroPrivateKey*>(encrypted_key_string, key));
  }
  
  MitroPrivateKey* MitroApiClient::KeyCache::Find(
                                                  const string& encrypted_key_string) const {
    KeyMap::const_iterator key_iter = cache_.find(encrypted_key_string);
    return key_iter == cache_.end() ? NULL : key_iter->second;
  }
  
}  // namespace mitro_api
