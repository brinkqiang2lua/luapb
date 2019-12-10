
// Copyright (c) 2018 brinkqiang (brink.qiang@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "luapb_module.h"

#include "sol.hpp"

#include "luapb_module.hpp"
#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/message.h>

#include <stdio.h>

#include "dmutil.h"

#undef GetMessage


using namespace google::protobuf;
using namespace compiler;

#define SAFE_RELEASE(x) \
  if (x) {              \
    delete x;           \
    x = NULL;           \
  }
#define PRINTF(format, ...) printf("[File:%s, Line:%d]: " format, __FILE__, __LINE__, ##__VA_ARGS__)

class ProtobufLibrary {
public:
    ProtobufLibrary() {
        GOOGLE_PROTOBUF_VERIFY_VERSION;
    };
    virtual ~ProtobufLibrary() {
        // fixed memory leak
        ShutdownProtobufLibrary();
    }
};
static ProtobufLibrary _protobuf_library;

namespace lua_module {
    class ScriptProtobuf {
    public:
        ScriptProtobuf(sol::this_state L, const std::string& file);
        ~ScriptProtobuf();

    public:
        std::string Encode(const char* structName, const sol::table& tab);
        sol::table  Decode(const char* structName, const std::string& msg);
        sol::table  GetEnum(const char* structName);
        sol::table  GetStruct(const char* structName);

    private:
        bool     load_proto_file(const std::string& file);
        Message* create_message(const std::string& typeName);

        const EnumDescriptor* find_enum_descriptor(const std::string& enumName);

        bool load_root_proto(const std::string& file);

        Message* lua2protobuf(const std::string& pbName, const sol::table& tab);
        void     protobuf2lua(const Message& message, sol::table& root);

        bool single_field_lua2pb(Message* message, const Reflection* reflection, const FieldDescriptor* fd, const sol::object& source);
        bool repeated_field_lua2pb(Message* message, const Reflection* reflection, const FieldDescriptor* fd, const sol::object& source);

        template <typename T>
        bool         single_field_pb2lua(const Message& message, const Reflection* reflection, const FieldDescriptor* fd, sol::table& dest, const T& name);
        bool         repeated_field_pb2lua(const Message& message, const Reflection* reflection, const FieldDescriptor* fd, sol::table& sub);
        bool         map_field_pb2lua(const Message& message, const Reflection* reflection, const FieldDescriptor* fd, sol::table& sub);
        sol::object& map_key(const Message& message, const Reflection* reflection, const FieldDescriptor* fd, sol::table& source);

        DiskSourceTree*        m_sourceTree;
        Importer*              m_importer;
        DynamicMessageFactory* m_factory;
        sol::reference         m_nil_object;
    };

    ScriptProtobuf::ScriptProtobuf(sol::this_state L, const std::string& file)
        : m_sourceTree(new DiskSourceTree())
        , m_importer(nullptr)
        , m_factory(nullptr)
        , m_nil_object(L, sol::lua_nil) {
        if (!load_proto_file(file))
            PRINTF("new ScriptProtobuf Error\n");
    }

    ScriptProtobuf::~ScriptProtobuf() {
        SAFE_RELEASE(m_factory);
        SAFE_RELEASE(m_importer);
        SAFE_RELEASE(m_sourceTree);
    }

    Message* ScriptProtobuf::create_message(const std::string& typeName) {
        Message* message = nullptr;
        if (m_importer) {
            const Descriptor* descriptor = m_importer->pool()->FindMessageTypeByName(typeName);
            if (descriptor) {
                const Message* prototype = m_factory->GetPrototype(descriptor);
                if (prototype) {
                    message = prototype->New();
                }
            }
            else {
                const Descriptor* descriptor =
                    DescriptorPool::generated_pool()->FindMessageTypeByName(typeName);
                if (descriptor) {
                    const Message* prototype =
                        MessageFactory::generated_factory()->GetPrototype(descriptor);
                    if (prototype) {
                        message = prototype->New();
                    }
                }
            }
        }
        else {
            const Descriptor* descriptor =
                DescriptorPool::generated_pool()->FindMessageTypeByName(typeName);
            if (descriptor) {
                const Message* prototype =
                    MessageFactory::generated_factory()->GetPrototype(descriptor);
                if (prototype) {
                    message = prototype->New();
                }
            }
        }
        return message;
    }

    const EnumDescriptor* ScriptProtobuf::find_enum_descriptor(const std::string& enumName) {
        if (m_importer) {
            const EnumDescriptor* descriptor = m_importer->pool()->FindEnumTypeByName(enumName);

            if (descriptor)
                return descriptor;
            else {
                const EnumDescriptor* descriptor =
                    DescriptorPool::generated_pool()->FindEnumTypeByName(enumName);
                return descriptor;
            }
        }
        else {
            const EnumDescriptor* descriptor = DescriptorPool::generated_pool()->FindEnumTypeByName(enumName);
            return descriptor;
        }
    }

    class __ProtobufErrorCollector : public MultiFileErrorCollector {
        virtual void AddError(
            const std::string& filename,
            int                line,
            int                column,
            const std::string& message) {
            PRINTF("%s:%d:%d:%s\n", filename.c_str(), line, column, message.c_str());
        }
    };

    bool ScriptProtobuf::load_root_proto(const std::string& file) {
        std::string strRoot = DMGetRootPath();
        std::string strProtoPath = strRoot + PATH_DELIMITER_STR + "proto";
        std::string strProtoPath2 = strRoot + PATH_DELIMITER_STR + ".." + PATH_DELIMITER_STR + "proto";

        m_sourceTree->MapPath("", strRoot);
        m_sourceTree->MapPath("", strProtoPath);
        m_sourceTree->MapPath("", strProtoPath2);

        SAFE_RELEASE(m_factory);
        SAFE_RELEASE(m_importer);

        __ProtobufErrorCollector pbec;
        m_importer = new Importer(m_sourceTree, &pbec);
        m_factory = new DynamicMessageFactory();
        const FileDescriptor* fileDescriptor = m_importer->Import(file);
        if (!fileDescriptor) {
            PRINTF("ScriptManager::loadRootProto(): import failed!\n");
            return false;
        }
        PRINTF("load proto file: "
            "%s"
            " ok!\n",
            file.c_str());
        return true;
    }

    bool ScriptProtobuf::load_proto_file(const std::string& sfile) {
        return load_root_proto(sfile);
    }

    sol::table ScriptProtobuf::Decode(const char* structName, const std::string& msg) {
        sol::state_view lua(m_nil_object.lua_state());
        sol::table      root = lua.create_table();

        Message* pbMsg = create_message(structName);

        if (pbMsg) {
            if (pbMsg->ParseFromString(msg))
                protobuf2lua(*pbMsg, root);
            else
                PRINTF("decode_pb(): parse failed. name = %s\n", structName);
            delete pbMsg;
        }
        else
            PRINTF("decode_pb(): failed to create pb message. name = %s\n", structName);

        return root;
    }
    sol::table ScriptProtobuf::GetEnum(const char* structName) {
        sol::state_view lua(m_nil_object.lua_state());
        sol::table      root = lua.create_table();
        auto            descriptor = find_enum_descriptor(structName);
        if (descriptor) {
            for (int i = 0; i < descriptor->value_count(); i++) {
                auto desc = descriptor->value(i);
                root[desc->name()] = desc->number();
            }
        }
        else
            PRINTF("cant find message  %s source compiled poll \n", structName);

        return root;
    }

    sol::table ScriptProtobuf::GetStruct(const char* structName) {
        sol::state_view lua(m_nil_object.lua_state());
        sol::table      root = lua.create_table();
        Message*        pbMsg = create_message(structName);
        if (pbMsg) {
            auto descriptor = pbMsg->GetDescriptor();

            if (descriptor) {
                for (int i = 0; i < descriptor->field_count(); i++) {
                    auto desc = descriptor->field(i);

                    if (desc->is_repeated()) {
                        root[desc->name()] = lua.create_table();
                    }
                    else {
                        switch (desc->cpp_type()) {
                        case FieldDescriptor::CPPTYPE_INT32: {
                            auto value = desc->has_default_value() ? desc->default_value_int32() : 0;
                            root[desc->name()] = value;
                            break;
                        }
                        case FieldDescriptor::CPPTYPE_INT64: {
                            auto value = desc->has_default_value() ? desc->default_value_int64() : 0;
                            root[desc->name()] = value;
                            break;
                        }
                        case FieldDescriptor::CPPTYPE_UINT32: {
                            auto value = desc->has_default_value() ? desc->default_value_uint32() : 0;
                            root[desc->name()] = value;
                            break;
                        }
                        case FieldDescriptor::CPPTYPE_UINT64: {
                            auto value = desc->has_default_value() ? desc->default_value_uint64() : 0;
                            root[desc->name()] = value;
                            break;
                        }
                        case FieldDescriptor::CPPTYPE_DOUBLE: {
                            auto value = desc->has_default_value() ? desc->default_value_double() : 0.0;
                            root[desc->name()] = value;
                            break;
                        }
                        case FieldDescriptor::CPPTYPE_FLOAT: {
                            auto value = desc->has_default_value() ? desc->default_value_float() : 0.0;
                            root[desc->name()] = value;
                            break;
                        }
                        case FieldDescriptor::CPPTYPE_BOOL: {
                            auto value = desc->has_default_value() ? desc->default_value_bool() : 0;
                            root[desc->name()] = value;
                            break;
                        }
                        case FieldDescriptor::CPPTYPE_ENUM: {
                            auto value = desc->has_default_value() ? desc->default_value_enum() : 0;
                            root[desc->name()] = value;
                            break;
                        }
                        case FieldDescriptor::CPPTYPE_STRING: {
                            auto value = desc->has_default_value() ? desc->default_value_string() : "";
                            root[desc->name()] = value;
                            break;
                        }
                        case FieldDescriptor::CPPTYPE_MESSAGE: {
                            auto prototype = m_factory->GetPrototype(desc->message_type());
                            if (prototype) {
                                auto sname = prototype->GetTypeName().c_str();
                                root[desc->name()] = GetStruct(sname);
                            }
                            else {
                                root[desc->name()] = lua.create_table();
                            }
                            break;
                        }
                        default:
                            root[desc->name()] = 0;
                            break;
                        }
                    }
                }
            }
            else
                PRINTF("cant find message descriptor %s source compiled poll \n", structName);

            delete pbMsg;
        }
        else
            PRINTF("cant find message  %s source compiled poll \n", structName);

        return root;
    }

    std::string ScriptProtobuf::Encode(const char* structName, const sol::table& tab) {
        Message* message = lua2protobuf(structName, tab);
        if (message) {
            std::string b;
            message->SerializeToString(&b);
            delete message;

            return b;
        }
        else {
            PRINTF("Encode(): failed to convert to pb message. name = %s\n", structName);
        }

        return std::string("");
    }

    bool ScriptProtobuf::single_field_lua2pb(Message* message, const Reflection* reflection, const FieldDescriptor* fd, const sol::object& source) {
        if (!source.valid()) {
            if (fd->is_required()) {
                PRINTF("lose required field %s", fd->name().c_str());
                return false;
            }
            //PRINTF("skip invalid field: %s\n", fd->name().c_str());
            return true;
        }

        switch (fd->cpp_type()) {
        case FieldDescriptor::CPPTYPE_DOUBLE: {
            reflection->SetDouble(message, fd, source.as<double>());
            break;
        }
        case FieldDescriptor::CPPTYPE_FLOAT: {
            reflection->SetFloat(message, fd, source.as<float>());
            break;
        }
        case FieldDescriptor::CPPTYPE_INT64: {
            reflection->SetInt64(message, fd, source.as<int64>());
            break;
        }
        case FieldDescriptor::CPPTYPE_UINT64: {
            reflection->SetUInt64(message, fd, source.as<uint64>());
            break;
        }
        case FieldDescriptor::CPPTYPE_ENUM: {
            const EnumDescriptor* enumDescriptor = fd->enum_type();
            if (source.is<string>()) {
                std::string                s = source.as<string>();
                const EnumValueDescriptor* valueDescriptor = enumDescriptor->FindValueByName(s);
                if (!valueDescriptor) {
                    PRINTF("cant find enum name %s:%s \n", enumDescriptor->name().c_str(), s.c_str());
                    return false;
                }
                reflection->SetEnum(message, fd, valueDescriptor);
            }
            else {
                int32_t                    n = source.as<int32>();
                const EnumValueDescriptor* valueDescriptor = enumDescriptor->FindValueByNumber(n);
                if (!valueDescriptor) {
                    PRINTF("cant find enum number %s:%d \n", enumDescriptor->name().c_str(), n);
                    return false;
                }
                reflection->SetEnum(message, fd, valueDescriptor);
            }
            break;
        }
        case FieldDescriptor::CPPTYPE_INT32: {
            reflection->SetInt32(message, fd, source.as<int32>());
            break;
        }
        case FieldDescriptor::CPPTYPE_UINT32: {
            reflection->SetUInt32(message, fd, source.as<uint32>());
            break;
        }
        case FieldDescriptor::CPPTYPE_STRING: {
            reflection->SetString(message, fd, source.as<string>());
            break;
        }
        case FieldDescriptor::CPPTYPE_BOOL: {
            reflection->SetBool(message, fd, source.as<bool>());
            break;
        }
        case FieldDescriptor::CPPTYPE_MESSAGE: {
            sol::table tt = source.as<sol::table>();
            Message*   v = lua2protobuf(fd->message_type()->full_name(), tt);
            if (!v) {
                PRINTF("convert to message %s failed whith value %s \n", fd->message_type()->full_name().c_str(), fd->name().c_str());
                return false;
            }
            reflection->MutableMessage(message, fd)->CopyFrom(*v);
            delete v;
            break;
        }
        default: {
            PRINTF("UNKNOWN CPP TYPE %d", fd->cpp_type());
            return false;
        }
        }  // switch
        return true;
    }
    // lua table -> array
    bool ScriptProtobuf::repeated_field_lua2pb(Message* message, const Reflection* reflection, const FieldDescriptor* fd, const sol::object& source) {
        if (!source.valid()) {
            //				PRINTF("%s value invalid.\n", fd->name().c_str());
            return true;
        }
        auto curr = source.as<sol::table>();
        for (size_t i = 1; i <= curr.size(); i++) {
            auto value = curr[i];
            switch (fd->cpp_type()) {
            case FieldDescriptor::CPPTYPE_DOUBLE: {
                reflection->AddDouble(message, fd, value.get<double>());
                break;
            }
            case FieldDescriptor::CPPTYPE_FLOAT: {
                reflection->AddFloat(message, fd, value.get<float>());
                break;
            }
            case FieldDescriptor::CPPTYPE_INT64: {
                int64 n = value.get<int64>();
                reflection->AddInt64(message, fd, n);
                break;
            }
            case FieldDescriptor::CPPTYPE_UINT64: {
                uint64 n = value.get<uint64>();
                reflection->AddUInt64(message, fd, n);
                break;
            }
            case FieldDescriptor::CPPTYPE_ENUM:  // support enum name or number
            {
                const EnumDescriptor* enumDescriptor = fd->enum_type();
                if (value.get_type() == sol::type::string) {
                    std::string                s = value.get<string>();
                    const EnumValueDescriptor* valueDescriptor = enumDescriptor->FindValueByName(s);
                    if (!valueDescriptor) {
                        PRINTF("cant find enum name %s:%s \n", enumDescriptor->name().c_str(), s.c_str());
                        return false;
                    }
                    reflection->AddEnum(message, fd, valueDescriptor);
                }
                else {
                    int32_t                    n = value.get<int32>();
                    const EnumValueDescriptor* valueDescriptor = enumDescriptor->FindValueByNumber(n);
                    if (!valueDescriptor) {
                        PRINTF("cant find enum number %s:%d \n", enumDescriptor->name().c_str(), n);
                        return false;
                    }
                    reflection->AddEnum(message, fd, valueDescriptor);
                }
                break;
            }
            case FieldDescriptor::CPPTYPE_INT32: {
                reflection->AddInt32(message, fd, value.get<int32>());
                break;
            }
            case FieldDescriptor::CPPTYPE_UINT32: {
                reflection->AddUInt32(message, fd, value.get<uint32>());
                break;
            }
            case FieldDescriptor::CPPTYPE_STRING: {
                reflection->AddString(message, fd, value.get<string>());
                break;
            }
            case FieldDescriptor::CPPTYPE_BOOL: {
                reflection->AddBool(message, fd, value.get<bool>());
                break;
            }
            case FieldDescriptor::CPPTYPE_MESSAGE: {
                Message* v = lua2protobuf(fd->message_type()->full_name(), value.get<sol::table>());
                if (!v) {
                    PRINTF("convert to message %s failed whith value %s \n", fd->message_type()->full_name().c_str(), fd->name().c_str());
                    return false;
                }
                reflection->AddMessage(message, fd)->CopyFrom(*v);
                delete v;
                break;
            }
            default: {
                PRINTF("UNKNOWN CPP TYPE %d", fd->cpp_type());
                return false;
            }
            }  // switch
        }    // for each lua table
        return true;
    }

    Message* ScriptProtobuf::lua2protobuf(const std::string& pbName, const sol::table& tab) {
        if (tab.empty()) {
            PRINTF("the %s is empty.\n", pbName.c_str());
            return nullptr;
        }

        Message* message = create_message(pbName);
        if (!message) {
            PRINTF("cant find message  %s source compiled poll \n", pbName.c_str());
            return nullptr;
        }
        const Reflection* reflection = message->GetReflection();
        const Descriptor* descriptor = message->GetDescriptor();
        for (int i = 0; i < descriptor->field_count(); ++i) {
            const FieldDescriptor* fd = descriptor->field(i);
            const string&          name = fd->name();
            sol::object            value = tab[name];

            if (fd->is_repeated()) {
                if (value.get_type() == sol::type::table) {
                    if (fd->is_map()) {
                        auto curr = value.as<sol::table>();
                        curr.for_each(
                            [&](sol::object key, sol::object value) {
                            auto new_record = reflection->AddMessage(message, fd);
                            auto ref = new_record->GetReflection();
                            auto desc = new_record->GetDescriptor();
                            auto fd_key = desc->field(0);
                            auto fd_value = desc->field(1);
                            if (!single_field_lua2pb(new_record, ref, fd_key, key) ||
                                !single_field_lua2pb(new_record, ref, fd_value, value))
                            {
                                PRINTF("(lua map error) key=%s \n", key.as<std::string>().c_str());
                            } });

                    }
                    else {  // else is array

                        if (!repeated_field_lua2pb(message, reflection, fd, value)) {
                            delete message;
                            return nullptr;
                        }
                    }
                }
                else {  //
                       //						PRINTF("warning: field %s type is %d", name.c_str(), value.get_type());
                }
            }
            else  // else is single field
            {
                if (!single_field_lua2pb(message, reflection, fd, value)) {
                    delete message;
                    return nullptr;
                }
            }
        }
        return message;
    }

    bool ScriptProtobuf::map_field_pb2lua(const Message& message, const Reflection* reflection, const FieldDescriptor* fd, sol::table& sub) {
        int size = reflection->FieldSize(message, fd);
        for (int i = 0; i < size; ++i) {
            if (fd->cpp_type() != FieldDescriptor::CPPTYPE_MESSAGE)
                return false;

            auto& msg = reflection->GetRepeatedMessage(message, fd, i);
            auto  ref = msg.GetReflection();
            auto  desc = msg.GetDescriptor();
            auto  key = desc->field(0);
            auto  value = desc->field(1);
            switch (key->cpp_type()) {
            case FieldDescriptor::CPPTYPE_DOUBLE:
                single_field_pb2lua(msg, ref, value, sub, (int64_t)ref->GetDouble(msg, key));  // Key in map fields cannot be float/double, bytes or message types.
                break;
            case FieldDescriptor::CPPTYPE_FLOAT:
                single_field_pb2lua(msg, ref, value, sub, (int64_t)ref->GetFloat(msg, key));
                break;
            case FieldDescriptor::CPPTYPE_INT64:
                single_field_pb2lua(msg, ref, value, sub, ref->GetInt64(msg, key));
                break;
            case FieldDescriptor::CPPTYPE_UINT64:
                single_field_pb2lua(msg, ref, value, sub, ref->GetUInt64(msg, key));
                break;
            case FieldDescriptor::CPPTYPE_ENUM:
                single_field_pb2lua(msg, ref, value, sub, ref->GetEnum(msg, key)->number());
                break;
            case FieldDescriptor::CPPTYPE_INT32:
                single_field_pb2lua(msg, ref, value, sub, ref->GetInt32(msg, key));
                break;
            case FieldDescriptor::CPPTYPE_UINT32:
                single_field_pb2lua(msg, ref, value, sub, ref->GetUInt32(msg, key));
                break;
            case FieldDescriptor::CPPTYPE_STRING:
                single_field_pb2lua(msg, ref, value, sub, ref->GetString(msg, key));
                break;
            case FieldDescriptor::CPPTYPE_BOOL:
                single_field_pb2lua(msg, ref, value, sub, ref->GetBool(msg, key));
                break;
            default:
                PRINTF("unknown key type: %d", key->cpp_type());
                return false;
            }
        }
        return true;
    }

    bool ScriptProtobuf::repeated_field_pb2lua(const Message& message, const Reflection* reflection, const FieldDescriptor* fd, sol::table& sub) {
        int size = reflection->FieldSize(message, fd);
        for (int i = 0; i < size; ++i) {
            switch (fd->cpp_type()) {
            case FieldDescriptor::CPPTYPE_DOUBLE:
                sub.add(reflection->GetRepeatedDouble(message, fd, i));
                break;
            case FieldDescriptor::CPPTYPE_FLOAT:
                sub.add(reflection->GetRepeatedFloat(message, fd, i));
                break;
            case FieldDescriptor::CPPTYPE_INT64:
                sub.add(reflection->GetRepeatedInt64(message, fd, i));
                break;
            case FieldDescriptor::CPPTYPE_UINT64:
                sub.add(reflection->GetRepeatedUInt64(message, fd, i));
                break;
            case FieldDescriptor::CPPTYPE_ENUM:
                sub.add(reflection->GetRepeatedEnum(message, fd, i)->number());
                break;
            case FieldDescriptor::CPPTYPE_INT32:
                sub.add(reflection->GetRepeatedInt32(message, fd, i));
                break;
            case FieldDescriptor::CPPTYPE_UINT32:
                sub.add(reflection->GetRepeatedUInt32(message, fd, i));
                break;
            case FieldDescriptor::CPPTYPE_STRING: {
                sub.add(reflection->GetRepeatedString(message, fd, i));
                break;
            }
            case FieldDescriptor::CPPTYPE_BOOL:
                sub.add(reflection->GetRepeatedBool(message, fd, i));
                break;
            case FieldDescriptor::CPPTYPE_MESSAGE: {
                sol::state_view lua(m_nil_object.lua_state());
                auto            ext = lua.create_table();
                auto&           msg = reflection->GetRepeatedMessage(message, fd, i);
                protobuf2lua(msg, ext);
                sub.add(ext);
                break;
            }
            default:
                PRINTF("unknown type: %d", fd->cpp_type());
                break;
            }
        }
        return true;
    }
    template <typename T>
    bool ScriptProtobuf::single_field_pb2lua(const Message& message, const Reflection* reflection, const FieldDescriptor* fd, sol::table& dest, const T& name) {
        switch (fd->cpp_type()) {
        case FieldDescriptor::CPPTYPE_DOUBLE:
            dest[name] = reflection->GetDouble(message, fd);
            break;
        case FieldDescriptor::CPPTYPE_FLOAT:
            dest[name] = reflection->GetFloat(message, fd);
            break;
        case FieldDescriptor::CPPTYPE_INT64:
            dest[name] = reflection->GetInt64(message, fd);
            break;
        case FieldDescriptor::CPPTYPE_UINT64:
            dest[name] = reflection->GetUInt64(message, fd);
            break;
        case FieldDescriptor::CPPTYPE_ENUM:
            dest[name] = (int)reflection->GetEnum(message, fd)->number();
            break;
        case FieldDescriptor::CPPTYPE_INT32:
            dest[name] = reflection->GetInt32(message, fd);
            break;
        case FieldDescriptor::CPPTYPE_UINT32:
            dest[name] = reflection->GetUInt32(message, fd);
            break;
        case FieldDescriptor::CPPTYPE_STRING:
            dest[name] = reflection->GetString(message, fd);
            break;
        case FieldDescriptor::CPPTYPE_BOOL:
            dest[name] = reflection->GetBool(message, fd);
            break;
        case FieldDescriptor::CPPTYPE_MESSAGE: {
            sol::state_view lua(m_nil_object.lua_state());
            sol::table      ext = lua.create_table();
            auto&           msg = reflection->GetMessage(message, fd);
            protobuf2lua(msg, ext);
            dest[name] = ext;
            break;
        }
        default:
            PRINTF("unknown type: %d", fd->cpp_type());
            break;
        }
        return true;
    }

    void ScriptProtobuf::protobuf2lua(const Message& message, sol::table& root) {
        const Reflection* reflection = message.GetReflection();
        const Descriptor* descriptor = message.GetDescriptor();
        sol::state_view   lua(m_nil_object.lua_state());

        for (int32_t index = 0; index < descriptor->field_count(); ++index) {
            const FieldDescriptor* fd = descriptor->field(index);
            const std::string&     name = fd->name();

            if (fd->is_repeated()) {
                auto sub = lua.create_table();
                if (fd->is_map()) {
                    if (map_field_pb2lua(message, reflection, fd, sub))
                        root[name] = sub;
                }
                else {
                    if (repeated_field_pb2lua(message, reflection, fd, sub))
                        root[name] = sub;
                }

            }
            else {
                single_field_pb2lua(message, reflection, fd, root, name);
            }
        }
    }
#ifdef PRIVATE_REQUIRE
    // register to a table
    static sol::table require_api(sol::this_state L) {
        sol::state_view lua(L);

        sol::table module = lua.create_table();
        module.new_usertype<ScriptProtobuf>("pb",
            sol::constructors<ScriptProtobuf(sol::this_state, const std::string&)>(),
            "encode",
            &ScriptProtobuf::Encode,
            "decode",
            &ScriptProtobuf::Decode,
            "get_enum",
            &ScriptProtobuf::GetEnum,
            "get_message",
            &ScriptProtobuf::GetStruct);

        return module;
    }
}
// lua require
LUA_API int luaopen_luapb(lua_State* L) {
    return sol::stack::call_lua(L, 1, lua_module::require_api);
}

// c++ register
LUA_API int require_luapb(lua_State* L) {
    luaL_requiref(L, "luapb", luaopen_luapb, 0);
    PRINTF("lua module: require luapb\n");
    return 1;
}

#else
    //register to public
    static int require_api(sol::state_view lua) {
        lua.new_usertype<ScriptProtobuf>("pb",
            sol::constructors<ScriptProtobuf(sol::this_state, const std::string&)>(),
            "encode",
            &ScriptProtobuf::Encode,
            "decode",
            &ScriptProtobuf::Decode,
            "get_enum",
            &ScriptProtobuf::GetEnum,
            "get_message",
            &ScriptProtobuf::GetStruct);

        return 1;
    }
}
// lua require
LUA_API int luaopen_luapb(lua_State* L) {
    return lua_module::require_api(sol::state_view(L));
}

// c++ register
LUA_API int require_luapb(lua_State* L) {
    luaopen_luapb(L);
    PRINTF("lua module: require luapb\n");
    return 1;
}
#endif 
