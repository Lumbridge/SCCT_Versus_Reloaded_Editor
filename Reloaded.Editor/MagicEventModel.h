#pragma once
#include "WorkflowModel.h"
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace Workflow::Magic
{
    // Values retain every reflected struct field, including fields not represented
    // by the timeline. A timeline edit must never rebuild a group from a subset.
    inline std::string Text(const Json& value)
    {
        if(value.is_string()) return value.get<std::string>();
        std::string out="(";bool first=true;
        for(auto it=value.begin();it!=value.end();++it)
        {
            if(!first)out+=",";first=false;
            if(value.is_object())out+=it.key()+"=";
            out+=Text(it.value());
        }
        return out+")";
    }
    inline double Number(const std::string& text)
    {
        size_t consumed=0;double value=std::stod(text,&consumed);
        if(consumed!=text.size() || !std::isfinite(value))throw std::runtime_error("Enter a finite number.");
        return value;
    }
    inline std::string Seconds(double value)
    {
        if(!std::isfinite(value) || value<0 || value>86400)throw std::runtime_error("Delay must be between 0 and 86400 seconds.");
        std::ostringstream out;out<<std::setprecision(9)<<value;return out.str();
    }
    inline void Validate(const Json& schema,const Json& value)
    {
        auto kind=schema.at("kind").get<std::string>();
        if(kind=="ArrayProperty" || kind=="FixedArray")
        {
            if(!value.is_array() || value.size()>4096)throw std::runtime_error("Array exceeds 4096 entries.");
            if(kind=="FixedArray" && value.size()!=schema.at("dimension").get<size_t>())throw std::runtime_error("Fixed array size cannot change.");
            for(const auto& item:value)Validate(schema.at("inner"),item);
        }
        else if(kind=="StructProperty")
        {
            if(!value.is_object() || value.size()!=schema.at("fields").size())throw std::runtime_error("Structure fields changed. Refresh and retry.");
            for(auto it=schema.at("fields").begin();it!=schema.at("fields").end();++it)Validate(it.value(),value.at(it.key()));
        }
        else
        {
            if(!value.is_string())throw std::runtime_error("Expected a property value.");
            auto text=value.get<std::string>();
            if(text.size()>16384 || text.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("Property text is too long or contains a newline.");
            auto choices=schema.value("choices",Json::array());
            if(!choices.empty())
            {
                bool found=false;for(const auto& c:choices)if(Fold(c.get<std::string>())==Fold(text))found=true;
                if(!found)throw std::runtime_error("Choose one of the supported values.");
            }
            if(kind=="FloatProperty" || kind=="IntProperty" || (kind=="ByteProperty" && choices.empty()))
            {
                double n=Number(text);
                if(kind!="FloatProperty" && text.find_first_not_of("+-0123456789")!=std::string::npos)throw std::runtime_error("Enter an integer using digits.");
                if(kind=="FloatProperty" && std::abs(n)>3.4028234e38)throw std::runtime_error("Float is out of range.");
                if(kind!="FloatProperty" && (std::floor(n)!=n || n<-2147483648.0 || n>2147483647.0))throw std::runtime_error("Enter an integer in range.");
                if(kind=="ByteProperty" && (n<0 || n>255))throw std::runtime_error("Byte must be between 0 and 255.");
            }
            if(kind=="NameProperty" && (text.empty() || text.size()>63 || text.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos))throw std::runtime_error("Names use up to 63 letters, digits or underscores.");
            if(kind=="StrProperty")
            {
                auto decoded=Json::parse(text,nullptr,false);if(decoded.is_discarded() || !decoded.is_string())throw std::runtime_error("Text properties use a quoted string, for example \"Door opens\".");
            }
        }
    }
    inline Json Default(const Json& schema)
    {
        const auto kind=schema.at("kind").get<std::string>();
        if(kind=="ArrayProperty")return Json::array();
        if(kind=="FixedArray"){Json a=Json::array();for(int i=0;i<schema.at("dimension");++i)a.push_back(Default(schema.at("inner")));return a;}
        if(kind=="StructProperty"){Json o=Json::object();for(auto it=schema.at("fields").begin();it!=schema.at("fields").end();++it)o[it.key()]=Default(it.value());return o;}
        if(schema.contains("choices") && !schema.at("choices").empty())return schema.at("choices")[0];
        if(kind=="NameProperty" || kind=="ObjectProperty" || kind=="ClassProperty")return "None";
        if(kind=="StrProperty")return "\"\"";
        return "0";
    }
    inline bool JsonEventProperty(const std::string& name,const Json& schema)
    {
        return schema.value("category",std::string{})=="SMagicEvent" || name=="InitialState" || name=="Event";
    }
    inline Json ExportEvent(const Json& snapshot,const Json& actors,const std::string& map)
    {
        Json properties=Json::object(),schema=Json::object(),targets=Json::array();
        for(auto it=snapshot.at("schema").begin();it!=snapshot.at("schema").end();++it)
            if(JsonEventProperty(it.key(),it.value())){properties[it.key()]=snapshot.at("values").at(it.key());schema[it.key()]=it.value();}
        if(!properties.contains("Groups"))throw std::runtime_error("Open an SMagicEvent before exporting JSON.");
        for(const auto& actor:actors)if(actor.value("authorable",false))targets.push_back({{"path",actor.at("path")},{"class",actor.at("class")},{"tag",actor.at("tag")},{"event",actor.at("event")}});
        return {{"format","scct.smagic-event"},{"version",1},{"class",snapshot.at("actor").at("class")},
            {"properties",properties},{"schema",schema},{"context",{{"map",map},{"actor",snapshot.at("actor")},{"tag",snapshot.at("values").value("Tag",Json("None"))},{"actors",targets}}},
            {"instructions","Edit properties, then import into the event open in SMagicEvent Workbench. Property leaves are strings; keep every field in each group/action. Schema and context are reference information only. Import preserves the destination actor's Tag and transform and does not create or modify linked actors. Event fields refer to actor Tags; object references such as StopActor must exist in the destination map. Missing properties stay unchanged; supplied arrays replace their previous contents. Use New Event before importing to create a separate event."}};
    }
    inline Json ImportEventChanges(const Json& snapshot,const Json& document)
    {
        if(!document.is_object() || document.value("format",std::string{})!="scct.smagic-event" || document.value("version",0)!=1)
            throw std::runtime_error("This is not a supported SMagicEvent JSON file (version 1).");
        if(document.at("class")!=snapshot.at("actor").at("class"))throw std::runtime_error("The JSON event class does not match the open event.");
        const auto& properties=document.at("properties");
        if(!properties.is_object() || properties.empty())throw std::runtime_error("The JSON file has no event properties.");
        Json changes=Json::object();
        for(auto it=properties.begin();it!=properties.end();++it)
        {
            const auto& schema=snapshot.at("schema");
            if(!schema.contains(it.key()) || !JsonEventProperty(it.key(),schema.at(it.key())))throw std::runtime_error("Unsupported event property: "+it.key());
            Validate(schema.at(it.key()),it.value());
            if(it.value()!=snapshot.at("values").at(it.key()))changes[it.key()]=it.value();
        }
        return changes;
    }
    inline void Move(Json& entries,int index,int direction)
    {
        int target=index+direction;
        if(index<0 || target<0 || index>=static_cast<int>(entries.size()) || target>=static_cast<int>(entries.size()))return;
        std::swap(entries[index],entries[target]);
    }
    inline std::string Category(const std::string& type)
    {
        auto c=Fold(type);
        if(c.find("magic")!=std::string::npos)return "Events";
        if(c.find("sound")!=std::string::npos)return "Sounds";
        if(c.find("trigger")!=std::string::npos)return "Triggers";
        if(c.find("mover")!=std::string::npos)return "Movers";
        if(c.find("emitter")!=std::string::npos)return "Emitters";
        if(c.find("sound")!=std::string::npos)return "Sounds";
        if(c.find("volume")!=std::string::npos)return "Volumes";
        return "Other";
    }
    inline std::string BoxPolygons(double hx,double hy,double hz)
    {
        for(double h:{hx,hy,hz})if(!std::isfinite(h) || h<=0 || h>100000)throw std::runtime_error("Invalid volume extent.");
        const int faces[6][4][3]={{{1,-1,-1},{1,1,-1},{1,1,1},{1,-1,1}},{{-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1}},{{-1,1,-1},{-1,1,1},{1,1,1},{1,1,-1}},{{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}},{{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},{{-1,-1,-1},{-1,1,-1},{1,1,-1},{1,-1,-1}}};
        std::ostringstream out;out<<"Begin PolyList\r\n";
        for(const auto& face:faces){out<<"Begin Polygon Flags=0\r\n";for(const auto& p:face)out<<"Vertex "<<p[0]*hx<<","<<p[1]*hy<<","<<p[2]*hz<<"\r\n";out<<"End Polygon\r\n";}out<<"End PolyList\r\n";return out.str();
    }
    inline std::string BoxPolygons(double halfExtent=128)
    {
        if(!std::isfinite(halfExtent) || halfExtent<=0 || halfExtent>100000)throw std::runtime_error("Invalid volume extent.");
        const int faces[6][4][3]={{{1,-1,-1},{1,1,-1},{1,1,1},{1,-1,1}},{{-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1}},{{-1,1,-1},{-1,1,1},{1,1,1},{1,1,-1}},{{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}},{{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},{{-1,-1,-1},{-1,1,-1},{1,1,-1},{1,-1,-1}}};
        std::ostringstream out;out<<"Begin PolyList\r\n";
        for(const auto& face:faces){out<<"Begin Polygon Flags=0\r\n";for(const auto& p:face)out<<"Vertex "<<p[0]*halfExtent<<","<<p[1]*halfExtent<<","<<p[2]*halfExtent<<"\r\n";out<<"End Polygon\r\n";}out<<"End PolyList\r\n";return out.str();
    }
}
