#pragma once
#include "MagicEventModel.h"
#include <set>
#include <algorithm>
#include <cctype>

namespace Workflow::Authoring
{
    inline void Keys(const Json& object, std::initializer_list<const char*> allowed)
    {
        if(!object.is_object())throw std::runtime_error("Expected an object in the change file.");
        for(auto it=object.begin();it!=object.end();++it)
            if(std::none_of(allowed.begin(),allowed.end(),[&](const char* key){return it.key()==key;}))
                throw std::runtime_error("Unknown change field: "+it.key());
    }
    inline void Identifier(const std::string& value)
    {
        if(value.empty() || value.size()>63 || value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos || isdigit(static_cast<unsigned char>(value[0])))
            throw std::runtime_error("Actor IDs use up to 63 letters, digits and underscores, starting with a letter or underscore.");
    }
    inline void Validate(const Json& document, const std::string& map)
    {
        Keys(document,{"format","version","map","description","operations","expect"});
        if(document.at("format")!="scct.map-changes" || document.at("version")!=1 || document.at("map")!=map)
            throw std::runtime_error("Unsupported change file or wrong map. Export the destination map first.");
        if(document.contains("description"))document.at("description").get<std::string>();
        if(document.contains("expect") && !document.at("expect").is_object())throw std::runtime_error("expect must map existing object paths to their exported values.");
        const auto& ops=document.at("operations");
        if(!ops.is_array() || ops.empty() || ops.size()>2000)throw std::runtime_error("Supply between 1 and 2000 operations.");
        std::set<std::string> ids,updates;
        for(const auto& op:ops)
        {
            auto kind=op.at("op").get<std::string>();
            if(kind=="create" || kind=="component")
            {
                Keys(op,kind=="create"?std::initializer_list<const char*>{"op","id","class","geometry","properties"}:std::initializer_list<const char*>{"op","id","class","owner","properties"});
                auto id=op.at("id").get<std::string>();Identifier(id);
                if(!ids.insert(Fold(id)).second)throw std::runtime_error("Duplicate creation ID: "+id);
                if(!op.at("class").is_string())throw std::runtime_error("A creation needs a class.");
                if(kind=="component" && !op.at("owner").is_string())throw std::runtime_error("A component needs an owner.");
            }
            else if(kind=="update")
            {
                Keys(op,{"op","actor","before","properties"});
                auto path=op.at("actor").at("path").get<std::string>();
                if(!updates.insert(Fold(path)).second)throw std::runtime_error("Combine updates to the same actor.");
                if(!op.at("before").is_object())throw std::runtime_error("An update needs the exported before values.");
            }
            else if(kind=="link")
            {
                Keys(op,{"op","event","target","trigger","group","delay"});
                op.at("event").get<std::string>();op.at("target").get<std::string>();
                op.at("trigger").get<bool>();
                if(op.contains("delay")){Magic::Seconds(Magic::Number(op.at("delay").get<std::string>()));if(op.at("trigger")==true)throw std::runtime_error("Delay belongs on event actions, not trigger links.");}
                if(op.value("group",0)<0)throw std::runtime_error("Group must be nonnegative.");
                continue;
            }
            else throw std::runtime_error("Unsupported map operation: "+kind);
            if(!op.at("properties").is_object())throw std::runtime_error("Properties must be an object.");
        }
    }
    // New objects are referenced explicitly, never by replacing arbitrary text.
    template<class Resolver> Json Resolve(const Json& schema,const Json& value,Resolver resolve)
    {
        const auto kind=schema.at("kind").template get<std::string>();
        if((kind=="ObjectProperty" || kind=="ClassProperty") && value.is_object())
        {
            Keys(value,{"$ref"});return resolve(value.at("$ref").template get<std::string>(),schema);
        }
        Json result=value;
        if((kind=="ArrayProperty" || kind=="FixedArray") && value.is_array())
            for(size_t i=0;i<value.size();++i)result[i]=Resolve(schema.at("inner"),value[i],resolve);
        if(kind=="StructProperty" && value.is_object())
            for(auto it=value.begin();it!=value.end();++it)result[it.key()]=Resolve(schema.at("fields").at(it.key()),it.value(),resolve);
        Magic::Validate(schema,result);return result;
    }
}
