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

#ifndef TSimpleJSONProtocol_hpp
#define TSimpleJSONProtocol_hpp

#include <stdio.h>

#include <thrift/protocol/TVirtualProtocol.h>

#include <stack>

#include "base/values.h"

namespace apache { namespace thrift { namespace protocol {

  // Forward declaration
  class TJSONContext;

  /**
   * JSON protocol for Thrift.
   *
   * Implements a protocol which uses JSON as the wire-format.
   *
   * Thrift types are represented as described below:
   *
   * 1. Every Thrift integer type is represented as a JSON number.
   *
   * 2. Thrift doubles are represented as JSON numbers. Some special values are
   *    represented as strings:
   *    a. "NaN" for not-a-number values
   *    b. "Infinity" for postive infinity
   *    c. "-Infinity" for negative infinity
   *
   * 3. Thrift string values are emitted as JSON strings, with appropriate
   *    escaping.
   *
   * 4. Thrift binary values are encoded into Base64 and emitted as JSON strings.
   *    The readBinary() method is written such that it will properly skip if
   *    called on a Thrift string (although it will decode garbage data).
   *
   * 5. Thrift structs are represented as JSON dictionaries, where the field
   #    names are the keys and the field values are the values.
   *
   * 6. Thrift lists and sets are represented as JSON arrays.
   *
   * 7. Thrift maps are represented as JSON dictionaries.
   *    Note that JSON keys can only be strings, which means that the key type of
   *    the Thrift map should be restricted to numeric or string types -- in the
   *    case of numerics, they are serialized as strings.
   *    TODO: Implement numeric keys using strings.
   *
   * 8. TODO: Thrift messages.
   *
   */
  class TSimpleJSONProtocol : public TVirtualProtocol<TSimpleJSONProtocol> {
  public:

    TSimpleJSONProtocol(boost::shared_ptr<TTransport> ptrans);

    ~TSimpleJSONProtocol();

  private:

    void pushContext(boost::shared_ptr<TJSONContext> c);

    void popContext();

    uint32_t writeJSONEscapeChar(uint8_t ch);

    uint32_t writeJSONChar(uint8_t ch);

    uint32_t writeJSONString(const std::string &str);

    uint32_t writeJSONBase64(const std::string &str);

    template <typename NumberType>
    uint32_t writeJSONInteger(NumberType num);

    uint32_t writeJSONDouble(double num);

    uint32_t writeJSONObjectStart() ;

    uint32_t writeJSONObjectEnd();

    uint32_t writeJSONArrayStart();

    uint32_t writeJSONArrayEnd();

  public:

    /**
     * Writing functions.
     */

    uint32_t writeMessageBegin(const std::string& name,
                               const TMessageType messageType,
                               const int32_t seqid);

    uint32_t writeMessageEnd();

    uint32_t writeStructBegin(const char* name);

    uint32_t writeStructEnd();

    uint32_t writeFieldBegin(const char* name,
                             const TType fieldType,
                             const int16_t fieldId);

    uint32_t writeFieldEnd();

    uint32_t writeFieldStop();

    uint32_t writeMapBegin(const TType keyType,
                           const TType valType,
                           const uint32_t size);

    uint32_t writeMapEnd();

    uint32_t writeListBegin(const TType elemType,
                            const uint32_t size);

    uint32_t writeListEnd();

    uint32_t writeSetBegin(const TType elemType,
                           const uint32_t size);

    uint32_t writeSetEnd();

    uint32_t writeBool(const bool value);

    uint32_t writeByte(const int8_t byte);

    uint32_t writeI16(const int16_t i16);

    uint32_t writeI32(const int32_t i32);

    uint32_t writeI64(const int64_t i64);

    uint32_t writeDouble(const double dub);

    uint32_t writeString(const std::string& str);

    uint32_t writeBinary(const std::string& str);

    /**
     * Reading functions
     */

    uint32_t readMessageBegin(std::string& name,
                              TMessageType& messageType,
                              int32_t& seqid);

    uint32_t readMessageEnd();

    uint32_t readStructBegin(std::string& name);

    uint32_t readStructEnd();

    uint32_t readFieldBegin(std::string& name,
                            TType& fieldType,
                            int16_t& fieldId);

    uint32_t readFieldEnd();

    uint32_t readMapBegin(TType& keyType,
                          TType& valType,
                          uint32_t& size);

    uint32_t readMapEnd();

    uint32_t readListBegin(TType& elemType,
                           uint32_t& size);

    uint32_t readListEnd();

    uint32_t readSetBegin(TType& elemType,
                          uint32_t& size);

    uint32_t readSetEnd();

    uint32_t readBool(bool& value);

    // Provide the default readBool() implementation for std::vector<bool>
    using TVirtualProtocol<TSimpleJSONProtocol>::readBool;

    uint32_t readByte(int8_t& byte);

    uint32_t readI16(int16_t& i16);

    uint32_t readI32(int32_t& i32);

    uint32_t readI64(int64_t& i64);

    uint32_t readDouble(double& dub);

    uint32_t readString(std::string& str);

    uint32_t readBinary(std::string& str);

  private:
    struct ParserContext {
      enum ParserContextType {
        DICTIONARY_CONTEXT,
        MAP_KEY_CONTEXT,
        MAP_VALUE_CONTEXT,
        LIST_CONTEXT
      };

      // Needed to satisfy DictionaryValue::Iterator constructor.
      static base::DictionaryValue default_dict;

      ParserContext(ParserContextType type, const base::DictionaryValue* value)
      : type(type), dict_iter(*value) {}
      ParserContext(ParserContextType type, const base::ListValue* value)
      : type(type), dict_iter(default_dict), list_iter(value->begin()), end_list_iter(value->end()) {}

      ParserContextType type;
      base::DictionaryValue::Iterator dict_iter;
      base::ListValue::const_iterator list_iter;
      base::ListValue::const_iterator end_list_iter;
    };

    void readAllInputFromTransport(std::string* s);
    const base::Value* parseJSONString(const std::string& json_string);
    const base::Value& nextValue();
    std::string getParserStackTrace() const;

    int readIntegerValue();

    TTransport* trans_;

    std::stack<boost::shared_ptr<TJSONContext> > contexts_;
    boost::shared_ptr<TJSONContext> context_;

    std::stack<ParserContext> parser_context_;

    // Used by nextValue to store key string.
    base::StringValue key_value_;
  };

  /**
   * Constructs input and output protocol objects given transports.
   */
  class TSimpleJSONProtocolFactory : public TProtocolFactory {
  public:
    TSimpleJSONProtocolFactory() {}

    virtual ~TSimpleJSONProtocolFactory() {}

    boost::shared_ptr<TProtocol> getProtocol(boost::shared_ptr<TTransport> trans) {
      return boost::shared_ptr<TProtocol>(new TSimpleJSONProtocol(trans));
    }
  };

}}} // apache::thrift::protocol


// TODO(dreiss): Move part of ThriftJSONString into a .cpp file and remove this.
#include <thrift/transport/TBufferTransports.h>

namespace apache { namespace thrift {
  
  template<typename ThriftStruct>
  std::string ThriftJSONString(const ThriftStruct& ts) {
    using namespace apache::thrift::transport;
    using namespace apache::thrift::protocol;
    TMemoryBuffer* buffer = new TMemoryBuffer;
    boost::shared_ptr<TTransport> trans(buffer);
    TSimpleJSONProtocol protocol(trans);
    
    ts.write(&protocol);
    
    uint8_t* buf;
    uint32_t size;
    buffer->getBuffer(&buf, &size);
    return std::string((char*)buf, (unsigned int)size);
  }
  
}} // apache::thrift


#endif /* TSimpleJSONProtocol_hpp */
