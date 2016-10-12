/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "TSimpleJSONProtocol.hpp"

#include <math.h>

#include <boost/scoped_ptr.hpp>
#include <boost/lexical_cast.hpp>

#include <thrift/protocol/TProtocol.h>
#include <thrift/protocol/TBase64Utils.h>
#include <thrift/transport/TTransportException.h>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"

using namespace apache::thrift::transport;

using base::JSONReader;
using base::Value;
using std::string;

namespace apache { namespace thrift { namespace protocol {


  // Static data

  static const uint8_t kJSONObjectStart = '{';
  static const uint8_t kJSONObjectEnd = '}';
  static const uint8_t kJSONArrayStart = '[';
  static const uint8_t kJSONArrayEnd = ']';
  static const uint8_t kJSONNewline = '\n';
  static const uint8_t kJSONPairSeparator = ':';
  static const uint8_t kJSONElemSeparator = ',';
  static const uint8_t kJSONBackslash = '\\';
  static const uint8_t kJSONStringDelimiter = '"';
  static const uint8_t kJSONZeroChar = '0';
  static const uint8_t kJSONEscapeChar = 'u';

  static const std::string kJSONEscapePrefix("\\u00");

  static const uint32_t kThriftVersion1 = 1;

  static const std::string kThriftNan("NaN");
  static const std::string kThriftInfinity("Infinity");
  static const std::string kThriftNegativeInfinity("-Infinity");

  static const std::string kTypeNameBool("tf");
  static const std::string kTypeNameByte("i8");
  static const std::string kTypeNameI16("i16");
  static const std::string kTypeNameI32("i32");
  static const std::string kTypeNameI64("i64");
  static const std::string kTypeNameDouble("dbl");
  static const std::string kTypeNameStruct("rec");
  static const std::string kTypeNameString("str");
  static const std::string kTypeNameMap("map");
  static const std::string kTypeNameList("lst");
  static const std::string kTypeNameSet("set");

  static const double kMaxPreciselyRepresentableInt64 =
  static_cast<double>(1LL << 53);
  static const double kMinPreciselyRepresentableInt64 =
  -kMaxPreciselyRepresentableInt64;

  // This table describes the handling for the first 0x30 characters
  //  0 : escape using "\u00xx" notation
  //  1 : just output index
  // <other> : escape using "\<other>" notation
  static const uint8_t kJSONCharTable[0x30] = {
    //  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
    0,  0,  0,  0,  0,  0,  0,  0,'b','t','n',  0,'f','r',  0,  0, // 0
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, // 1
    1,  1,'"',  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, // 2
  };


  // This string's characters must match up with the elements in kEscapeCharVals.
  // I don't have '/' on this list even though it appears on www.json.org --
  // it is not in the RFC
  const static std::string kEscapeChars("\"\\bfnrt");

  // The elements of this array must match up with the sequence of characters in
  // kEscapeChars
  const static uint8_t kEscapeCharVals[7] = {
    '"', '\\', '\b', '\f', '\n', '\r', '\t',
  };


  // Static helper functions

  // Return the hex character representing the integer val. The value is masked
  // to make sure it is in the correct range.
  static uint8_t hexChar(uint8_t val) {
    val &= 0x0F;
    if (val < 10) {
      return val + '0';
    }
    else {
      return val - 10 + 'a';
    }
  }

  /**
   * Class to serve as base JSON context and as base class for other context
   * implementations
   */
  class TJSONContext {

  public:

    TJSONContext() {};

    virtual ~TJSONContext() {};

    /**
     * Write context data to the transport. Default is to do nothing.
     */
    virtual uint32_t write(TTransport &trans) {
      (void) trans;
      return 0;
    };

    /**
     * Read context data from the transport. Default is to do nothing.
     */
    virtual uint32_t read() {
      return 0;
    };

    /**
     * Return true if numbers need to be escaped as strings in this context.
     * Default behavior is to return false.
     */
    virtual bool escapeNum() {
      return false;
    }
  };

  // Context class for object member key-value pairs
  class JSONPairContext : public TJSONContext {

  public:

    JSONPairContext() :
    first_(true),
    colon_(true) {
    }

    uint32_t write(TTransport &trans) {
      if (first_) {
        first_ = false;
        colon_ = true;
        return 0;
      }
      else {
        trans.write(colon_ ? &kJSONPairSeparator : &kJSONElemSeparator, 1);
        colon_ = !colon_;
        return 1;
      }
    }

    // Numbers must be turned into strings if they are the key part of a pair
    virtual bool escapeNum() {
      return colon_;
    }

  private:

    bool first_;
    bool colon_;
  };

  // Context class for lists
  class JSONListContext : public TJSONContext {

  public:

    JSONListContext() :
    first_(true) {
    }

    uint32_t write(TTransport &trans) {
      if (first_) {
        first_ = false;
        return 0;
      }
      else {
        trans.write(&kJSONElemSeparator, 1);
        return 1;
      }
    }

  private:
    bool first_;
  };


  TSimpleJSONProtocol::TSimpleJSONProtocol(boost::shared_ptr<TTransport> ptrans) :
  TVirtualProtocol<TSimpleJSONProtocol>(ptrans),
  trans_(ptrans.get()),
  context_(new TJSONContext()),
  key_value_("") {
  }

  TSimpleJSONProtocol::~TSimpleJSONProtocol() {}

  void TSimpleJSONProtocol::pushContext(boost::shared_ptr<TJSONContext> c) {
    contexts_.push(context_);
    context_ = c;
  }

  void TSimpleJSONProtocol::popContext() {
    context_ = contexts_.top();
    contexts_.pop();
  }

  // Write the character ch as a JSON escape sequence ("\u00xx")
  uint32_t TSimpleJSONProtocol::writeJSONEscapeChar(uint8_t ch) {
    trans_->write((const uint8_t *)kJSONEscapePrefix.c_str(),
                  static_cast<uint32_t>(kJSONEscapePrefix.length()));
    uint8_t outCh = hexChar(ch >> 4);
    trans_->write(&outCh, 1);
    outCh = hexChar(ch);
    trans_->write(&outCh, 1);
    return 6;
  }

  // Write the character ch as part of a JSON string, escaping as appropriate.
  uint32_t TSimpleJSONProtocol::writeJSONChar(uint8_t ch) {
    if (ch >= 0x30) {
      if (ch == kJSONBackslash) { // Only special character >= 0x30 is '\'
        trans_->write(&kJSONBackslash, 1);
        trans_->write(&kJSONBackslash, 1);
        return 2;
      }
      else {
        trans_->write(&ch, 1);
        return 1;
      }
    }
    else {
      uint8_t outCh = kJSONCharTable[ch];
      // Check if regular character, backslash escaped, or JSON escaped
      if (outCh == 1) {
        trans_->write(&ch, 1);
        return 1;
      }
      else if (outCh > 1) {
        trans_->write(&kJSONBackslash, 1);
        trans_->write(&outCh, 1);
        return 2;
      }
      else {
        return writeJSONEscapeChar(ch);
      }
    }
  }

  // Write out the contents of the string str as a JSON string, escaping
  // characters as appropriate.
  uint32_t TSimpleJSONProtocol::writeJSONString(const std::string &str) {
    uint32_t result = context_->write(*trans_);
    result += 2; // For quotes
    trans_->write(&kJSONStringDelimiter, 1);
    std::string::const_iterator iter(str.begin());
    std::string::const_iterator end(str.end());
    while (iter != end) {
      result += writeJSONChar(*iter++);
    }
    trans_->write(&kJSONStringDelimiter, 1);
    return result;
  }

  // Write out the contents of the string as JSON string, base64-encoding
  // the string's contents, and escaping as appropriate
  uint32_t TSimpleJSONProtocol::writeJSONBase64(const std::string &str) {
    uint32_t result = context_->write(*trans_);
    result += 2; // For quotes
    trans_->write(&kJSONStringDelimiter, 1);
    uint8_t b[4];
    const uint8_t *bytes = (const uint8_t *)str.c_str();
    if(str.length() > (std::numeric_limits<uint32_t>::max)())
      throw TProtocolException(TProtocolException::SIZE_LIMIT);
    uint32_t len = static_cast<uint32_t>(str.length());
    while (len >= 3) {
      // Encode 3 bytes at a time
      base64_encode(bytes, 3, b);
      trans_->write(b, 4);
      result += 4;
      bytes += 3;
      len -=3;
    }
    if (len) { // Handle remainder
      base64_encode(bytes, len, b);
      trans_->write(b, len + 1);
      result += len + 1;
    }
    trans_->write(&kJSONStringDelimiter, 1);
    return result;
  }

  // Convert the given integer type to a JSON number, or a string
  // if the context requires it (eg: key in a map pair).
  template <typename NumberType>
  uint32_t TSimpleJSONProtocol::writeJSONInteger(NumberType num) {
    uint32_t result = context_->write(*trans_);
    std::string val(boost::lexical_cast<std::string>(num));
    bool escapeNum = context_->escapeNum();
    if (escapeNum) {
      trans_->write(&kJSONStringDelimiter, 1);
      result += 1;
    }
    if(val.length() > (std::numeric_limits<uint32_t>::max)())
      throw TProtocolException(TProtocolException::SIZE_LIMIT);
    trans_->write((const uint8_t *)val.c_str(), static_cast<uint32_t>(val.length()));
    result += static_cast<uint32_t>(val.length());
    if (escapeNum) {
      trans_->write(&kJSONStringDelimiter, 1);
      result += 1;
    }
    return result;
  }

  // Convert the given double to a JSON string, which is either the number,
  // "NaN" or "Infinity" or "-Infinity".
  uint32_t TSimpleJSONProtocol::writeJSONDouble(double num) {
    uint32_t result = context_->write(*trans_);
    std::string val(boost::lexical_cast<std::string>(num));

    // Normalize output of boost::lexical_cast for NaNs and Infinities
    bool special = false;
    switch (val[0]) {
      case 'N':
      case 'n':
        val = kThriftNan;
        special = true;
        break;
      case 'I':
      case 'i':
        val = kThriftInfinity;
        special = true;
        break;
      case '-':
        if ((val[1] == 'I') || (val[1] == 'i')) {
          val = kThriftNegativeInfinity;
          special = true;
        }
        break;
    }

    bool escapeNum = special || context_->escapeNum();
    if (escapeNum) {
      trans_->write(&kJSONStringDelimiter, 1);
      result += 1;
    }
    if(val.length() > (std::numeric_limits<uint32_t>::max)())
      throw TProtocolException(TProtocolException::SIZE_LIMIT);
    trans_->write((const uint8_t *)val.c_str(), static_cast<uint32_t>(val.length()));
    result += static_cast<uint32_t>(val.length());
    if (escapeNum) {
      trans_->write(&kJSONStringDelimiter, 1);
      result += 1;
    }
    return result;
  }

  uint32_t TSimpleJSONProtocol::writeJSONObjectStart() {
    uint32_t result = context_->write(*trans_);
    trans_->write(&kJSONObjectStart, 1);
    pushContext(boost::shared_ptr<TJSONContext>(new JSONPairContext()));
    return result + 1;
  }

  uint32_t TSimpleJSONProtocol::writeJSONObjectEnd() {
    popContext();
    trans_->write(&kJSONObjectEnd, 1);
    return 1;
  }

  uint32_t TSimpleJSONProtocol::writeJSONArrayStart() {
    uint32_t result = context_->write(*trans_);
    trans_->write(&kJSONArrayStart, 1);
    pushContext(boost::shared_ptr<TJSONContext>(new JSONListContext()));
    return result + 1;
  }

  uint32_t TSimpleJSONProtocol::writeJSONArrayEnd() {
    popContext();
    trans_->write(&kJSONArrayEnd, 1);
    return 1;
  }

  uint32_t TSimpleJSONProtocol::writeMessageBegin(const std::string& name,
                                                  const TMessageType messageType,
                                                  const int32_t seqid) {
    uint32_t result = writeJSONArrayStart();
    result += writeJSONInteger(kThriftVersion1);
    result += writeJSONString(name);
    result += writeJSONInteger(messageType);
    result += writeJSONInteger(seqid);
    return result;
  }

  uint32_t TSimpleJSONProtocol::writeMessageEnd() {
    return writeJSONArrayEnd();
  }

  uint32_t TSimpleJSONProtocol::writeStructBegin(const char* name) {
    (void) name;
    return writeJSONObjectStart();
  }

  uint32_t TSimpleJSONProtocol::writeStructEnd() {
    return writeJSONObjectEnd();
  }

  uint32_t TSimpleJSONProtocol::writeFieldBegin(const char* name,
                                                const TType fieldType,
                                                const int16_t fieldId) {
    return writeJSONString(name);
  }

  uint32_t TSimpleJSONProtocol::writeFieldEnd() {
    return 0;
  }

  uint32_t TSimpleJSONProtocol::writeFieldStop() {
    return 0;
  }

  uint32_t TSimpleJSONProtocol::writeMapBegin(const TType keyType,
                                              const TType valType,
                                              const uint32_t size) {
    return writeJSONObjectStart();
  }

  uint32_t TSimpleJSONProtocol::writeMapEnd() {
    return writeJSONObjectEnd();
  }

  uint32_t TSimpleJSONProtocol::writeListBegin(const TType elemType,
                                               const uint32_t size) {
    return writeJSONArrayStart();
  }

  uint32_t TSimpleJSONProtocol::writeListEnd() {
    return writeJSONArrayEnd();
  }

  uint32_t TSimpleJSONProtocol::writeSetBegin(const TType elemType,
                                              const uint32_t size) {
    return writeJSONArrayStart();
  }

  uint32_t TSimpleJSONProtocol::writeSetEnd() {
    return writeJSONArrayEnd();
  }

#define STRLEN(s) (sizeof(s)/sizeof(s[0])-1)

  uint32_t TSimpleJSONProtocol::writeBool(const bool value) {
    uint32_t result = context_->write(*trans_);

    const unsigned char TRUE_STRING[] = "true";
    const unsigned char FALSE_STRING[] = "false";

    if (value) {
      trans_->write(TRUE_STRING, STRLEN(TRUE_STRING));
      result += STRLEN(TRUE_STRING);
    } else {
      trans_->write(FALSE_STRING, STRLEN(FALSE_STRING));
      result += STRLEN(FALSE_STRING);
    }

    return result;
  }

  uint32_t TSimpleJSONProtocol::writeByte(const int8_t byte) {
    // writeByte() must be handled specially becuase boost::lexical cast sees
    // int8_t as a text type instead of an integer type
    return writeJSONInteger((int16_t)byte);
  }

  uint32_t TSimpleJSONProtocol::writeI16(const int16_t i16) {
    return writeJSONInteger(i16);
  }

  uint32_t TSimpleJSONProtocol::writeI32(const int32_t i32) {
    return writeJSONInteger(i32);
  }

  uint32_t TSimpleJSONProtocol::writeI64(const int64_t i64) {
    return writeJSONInteger(i64);
  }

  uint32_t TSimpleJSONProtocol::writeDouble(const double dub) {
    return writeJSONDouble(dub);
  }

  uint32_t TSimpleJSONProtocol::writeString(const std::string& str) {
    return writeJSONString(str);
  }

  uint32_t TSimpleJSONProtocol::writeBinary(const std::string& str) {
    return writeJSONBase64(str);
  }

  /**
   * Reading functions
   */

  void TSimpleJSONProtocol::readAllInputFromTransport(string* s) {
    s->clear();

    const uint32_t BUF_SIZE = 1024;
    uint8_t buf[BUF_SIZE];
    uint32_t num_read;

    while ((num_read = trans_->read(buf, BUF_SIZE)) > 0) {
      s->append(reinterpret_cast<char*>(buf), num_read);
    }
  }

  const Value* TSimpleJSONProtocol::parseJSONString(const string& json_string) {
    int options = base::JSON_ALLOW_TRAILING_COMMAS;
    int error_code;
    string error_message;

    const Value* value = JSONReader::ReadAndReturnError(json_string,
                                                        options,
                                                        &error_code,
                                                        &error_message);
    if (value == NULL) {
      throw TProtocolException(TProtocolException::UNKNOWN,
                               "Error parsing json: " + error_message);
    }

    return value;
  }

  const Value& TSimpleJSONProtocol::nextValue() {
    if (parser_context_.empty()) {
      throw TProtocolException(TProtocolException::UNKNOWN,
                               "Attempt to getValue from empty stack");
    }

    ParserContext& context = parser_context_.top();

    switch (context.type) {
      case ParserContext::DICTIONARY_CONTEXT:
        return context.dict_iter.value();
        break;
      case ParserContext::LIST_CONTEXT: {
        const Value& value = **context.list_iter;
        ++context.list_iter;
        return value;
        break;
      }
      case ParserContext::MAP_KEY_CONTEXT:
        context.type = ParserContext::MAP_VALUE_CONTEXT;
        key_value_ = base::StringValue(context.dict_iter.key());
        return key_value_;
        break;
      case ParserContext::MAP_VALUE_CONTEXT: {
        context.type = ParserContext::MAP_KEY_CONTEXT;
        const Value& value = context.dict_iter.value();
        context.dict_iter.Advance();
        return value;
        break;
      }
    }
  }

  string TSimpleJSONProtocol::getParserStackTrace() const {
    // TODO: This should print a full stack trace.
    if (parser_context_.empty()) {
      return "Empty parser context";
    }

    const ParserContext& context = parser_context_.top();

    switch (context.type) {
      case ParserContext::DICTIONARY_CONTEXT:
      case ParserContext::MAP_KEY_CONTEXT:
      case ParserContext::MAP_VALUE_CONTEXT:
        return context.dict_iter.key();
        break;
      case ParserContext::LIST_CONTEXT: {
        return "list field";
        break;
      }
    }
  }

  // TODO: If null values are encountered, it should skip the field (currently
  // throws an exception).

  uint32_t TSimpleJSONProtocol::readMessageBegin(std::string& name,
                                                 TMessageType& messageType,
                                                 int32_t& seqid) {
    T_DEBUG("readMessageBegin");
    // TODO: Implement
    throw TProtocolException(TProtocolException::NOT_IMPLEMENTED);
    return 0;
  }

  uint32_t TSimpleJSONProtocol::readMessageEnd() {
    T_DEBUG("readMessageEnd");
    // TODO: Implement
    throw TProtocolException(TProtocolException::NOT_IMPLEMENTED);
    return 0;
  }

  uint32_t TSimpleJSONProtocol::readStructBegin(std::string& name) {
    T_DEBUG("readStructBegin");
    uint32_t result = 0;
    const Value* value = NULL;

    if (parser_context_.empty()) {
      string json_string;
      readAllInputFromTransport(&json_string);
      result = json_string.size();
      T_DEBUG("input_size: %z", json_string.size());
      T_DEBUG("input: %s", json_string.c_str());

      value = parseJSONString(json_string);
    } else {
      value = &nextValue();
    }

    const base::DictionaryValue* dict_value;
    if (!value->GetAsDictionary(&dict_value)) {
      throw TProtocolException(TProtocolException::INVALID_DATA,
                               "Expected dictionary value\n" +
                               getParserStackTrace());
    }

    parser_context_.push(
                         ParserContext(ParserContext::DICTIONARY_CONTEXT, dict_value));

    return result;
  }

  uint32_t TSimpleJSONProtocol::readStructEnd() {
    T_DEBUG("readStructEnd");
    parser_context_.pop();
    return 0;
  }

  uint32_t TSimpleJSONProtocol::readFieldBegin(std::string& name,
                                               TType& fieldType,
                                               int16_t& fieldId) {
    if (parser_context_.empty()) {
      throw TProtocolException(TProtocolException::UNKNOWN,
                               "Illegal parser state; stack empty");
    }

    const ParserContext& context = parser_context_.top();
    if (context.dict_iter.IsAtEnd()) {
      fieldType = ::apache::thrift::protocol::T_STOP;
    } else {
      if (context.dict_iter.value().GetType() == base::Value::TYPE_NULL) {
        // Setting an empty name causes null fields to be skipped.
        name = "";
      } else {
        name = context.dict_iter.key();
      }
//      fieldType = T_UNKNOWN;
//      fieldId = kFieldIdUnknown;
    }
    T_DEBUG("readFieldBegin: %s", name.c_str());

    return 0;
  }

  uint32_t TSimpleJSONProtocol::readFieldEnd() {
    T_DEBUG("readFieldEnd");
    if (parser_context_.empty()) {
      throw TProtocolException(TProtocolException::UNKNOWN,
                               "Illegal parser state; stack empty");
    }

    ParserContext& context = parser_context_.top();
    if (!context.dict_iter.IsAtEnd()) {
      context.dict_iter.Advance();
    }

    return 0;
  }

  uint32_t TSimpleJSONProtocol::readMapBegin(TType& keyType,
                                             TType& valType,
                                             uint32_t& size) {
    const Value& value = nextValue();

    const base::DictionaryValue* dict_value;
    if (!value.GetAsDictionary(&dict_value)) {
      throw TProtocolException(TProtocolException::INVALID_DATA,
                               "Expected dictionary value\n" +
                               getParserStackTrace());
    }

    size = dict_value->size();
    parser_context_.push(
                         ParserContext(ParserContext::MAP_KEY_CONTEXT, dict_value));

    T_DEBUG("readMapBegin: %d", size);
    return 0;
  }

  uint32_t TSimpleJSONProtocol::readMapEnd() {
    T_DEBUG("readMapEnd");
    parser_context_.pop();
    return 0;
  }

  uint32_t TSimpleJSONProtocol::readListBegin(TType& elemType, uint32_t& size) {
    const Value& value = nextValue();

    const base::ListValue* list_value;
    if (!value.GetAsList(&list_value)) {
      throw TProtocolException(TProtocolException::INVALID_DATA,
                               "Expected list value\n" +
                               getParserStackTrace());
    }

    size = list_value->GetSize();
    parser_context_.push(ParserContext(ParserContext::LIST_CONTEXT, list_value));

    T_DEBUG("readListBegin: %d", size);
    return 0;
  }

  uint32_t TSimpleJSONProtocol::readListEnd() {
    T_DEBUG("readListEnd");
    parser_context_.pop();
    return 0;
  }

  uint32_t TSimpleJSONProtocol::readSetBegin(TType& elemType, uint32_t& size) {
    T_DEBUG("readSetBegin");
    return 0;
  }

  uint32_t TSimpleJSONProtocol::readSetEnd() {
    T_DEBUG("readSetEnd");
    return 0;
  }

  uint32_t TSimpleJSONProtocol::readBool(bool& b) {
    const Value& value = nextValue();

    if (!value.GetAsBoolean(&b)) {
      throw TProtocolException(TProtocolException::INVALID_DATA,
                               "Expected boolean value\n" +
                               getParserStackTrace());
    }
    T_DEBUG("readBool: %d", b);
    return 0;
  }

  int TSimpleJSONProtocol::readIntegerValue() {
    // TODO: Doesn't handle parsing double as string keys of map.
    const Value& value = nextValue();

    int int_value;

    if (!value.GetAsInteger(&int_value)) {
      double double_value;
      if (!value.GetAsDouble(&double_value)) {
        throw TProtocolException(TProtocolException::INVALID_DATA,
                                 "Expected integer value\n" +
                                 getParserStackTrace());
      }

      if (fmod(double_value, 1.0) != 0.0) {
        throw TProtocolException(TProtocolException::INVALID_DATA,
                                 "Expected integer value\n" +
                                 getParserStackTrace());
      }
      int_value = static_cast<int>(double_value);
    }

    return int_value;
  }

  uint32_t TSimpleJSONProtocol::readByte(int8_t& byte) {
    int int_value = readIntegerValue();
    if (int_value > std::numeric_limits<int8_t>::max() ||
        int_value < std::numeric_limits<int8_t>::min()) {
      throw TProtocolException(TProtocolException::SIZE_LIMIT,
                               base::StringPrintf("Value too large for int8: %d", int_value));
    }

    byte = static_cast<int8_t>(int_value);
    T_DEBUG("readByte: %d", static_cast<int>(byte));
    return 0;
  }

  uint32_t TSimpleJSONProtocol::readI16(int16_t& i16) {
    int int_value = readIntegerValue();
    if (int_value > std::numeric_limits<int16_t>::max() ||
        int_value < std::numeric_limits<int16_t>::min()) {
      throw TProtocolException(TProtocolException::SIZE_LIMIT,
                               base::StringPrintf("Value too large for int8: %d", int_value));
    }

    i16 = static_cast<int16_t>(int_value);
    T_DEBUG("readI16: %hd", i16);
    return 0;
  }

  uint32_t TSimpleJSONProtocol::readI32(int32_t& i32) {
    i32 = readIntegerValue();
    T_DEBUG("readI32: %d", i32);
    return 0;
  }

  uint32_t TSimpleJSONProtocol::readI64(int64_t& i64) {
    // TODO: Doesn't handle parsing int64 as string keys of map.
    // This could be combined with readIntegerValue() with a slight performance
    // degradagation
    const Value& value = nextValue();

    int int_value;
    if (value.GetAsInteger(&int_value)) {
      i64 = int_value;
    } else {
      double double_value;
      if (!value.GetAsDouble(&double_value)) {
        throw TProtocolException(TProtocolException::INVALID_DATA,
                                 "Expected integer value\n" +
                                 getParserStackTrace());
      }
      
      if (fmod(double_value, 1.0) != 0.0) {
        throw TProtocolException(TProtocolException::INVALID_DATA,
                                 "Expected integer value\n" +
                                 getParserStackTrace());
      }
      
      if (double_value < kMinPreciselyRepresentableInt64 ||
          double_value > kMaxPreciselyRepresentableInt64) {
        // TODO: Should this be a warning instead?
        throw TProtocolException(TProtocolException::SIZE_LIMIT,
                                 base::StringPrintf("Possible loss of precision: %lf", double_value)); 
      }
      i64 = static_cast<int64_t>(double_value);
    }
    
    // TODO: %lld doesn't work with MSVC
    T_DEBUG("readI64: %lld", i64);
    return 0;
  }
  
  uint32_t TSimpleJSONProtocol::readDouble(double& dub) {
    // TODO: Doesn't handle parsing double as string keys of map.
    const Value& value = nextValue();
    
    if (!value.GetAsDouble(&dub)) {
      throw TProtocolException(TProtocolException::INVALID_DATA,
                               "Expected double value\n" +
                               getParserStackTrace());
    }
    
    T_DEBUG("readDouble: %lf", dub);
    return 0;
  }
  
  uint32_t TSimpleJSONProtocol::readString(std::string &str) {
    const Value& value = nextValue();
    
    if (!value.GetAsString(&str)) {
      throw TProtocolException(TProtocolException::INVALID_DATA,
                               "Expected string value\n" +
                               getParserStackTrace());
    }
    
    T_DEBUG("readString: %s", str.c_str());
    return 0;
  }
  
  uint32_t TSimpleJSONProtocol::readBinary(std::string &str) {
    T_DEBUG("readBinary");
    // TODO: Implement
    throw TProtocolException(TProtocolException::NOT_IMPLEMENTED);
    return 0;
  }
  
  base::DictionaryValue TSimpleJSONProtocol::ParserContext::default_dict;
  
}}} // apache::thrift::protocol
