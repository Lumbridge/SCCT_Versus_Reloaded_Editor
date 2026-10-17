#pragma once
// Copy Properties / Paste Properties on the actor menu: which of the copied
// actor's properties are offered, which start ticked, which targets take a
// property, and the words for the dialog and the result. No engine and no
// Windows, so the tests compile it alone (with WorkflowModel.cpp).
//
// A copied property, as Editor::CopyActorProperties lists it:
// {name, category, schema (MagicSchema, without the category), value (as
// InspectActor reads it), lineage: {class path: [its class names, most
// derived first]} for the declared class of every object reference in the
// schema}.
#include "MagicEventModel.h"
#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace Workflow::PropertyPaste
{
    // An object reference may point at an asset or an actor, which stands on
    // its own. Any other class (a brush's Model, an emitter's particle
    // emitters, Karma parameters) is an object its actor owns, and two actors
    // must never share one.
    inline bool SharedClass(const Json& lineage)
    {
        static const std::set<std::string> shared{"material","staticmesh","mesh","sound","class","actor"};
        if(!lineage.is_array())return false;
        for(const auto& name:lineage)if(name.is_string() && shared.count(Fold(name.get<std::string>())))return true;
        return false;
    }
    namespace Detail
    {
        // The declared class path of every object reference in a schema. A class
        // reference (class<...>) never names an owned object.
        inline void ReferenceTypes(const Json& schema,std::vector<std::string>& out)
        {
            if(!schema.is_object())return;
            if(schema.value("kind",std::string())=="ObjectProperty")out.push_back(schema.value("type",std::string()));
            if(schema.contains("inner"))ReferenceTypes(schema.at("inner"),out);
            if(schema.contains("fields"))for(const auto& field:schema.at("fields"))ReferenceTypes(field,out);
        }
        inline bool Derives(const Json& entry,const std::string& type,const char* base)
        {
            const auto lineage=entry.value("lineage",Json::object());
            if(!lineage.contains(type) || !lineage.at(type).is_array())return false;
            for(const auto& name:lineage.at(type))if(name.is_string() && Fold(name.get<std::string>())==base)return true;
            return false;
        }
        // The path in Class'Package.Name', or the text itself.
        inline std::string ReferencePath(const std::string& text)
        {
            const auto first=text.find('\''),last=text.rfind('\'');
            auto path=first!=std::string::npos && last>first?text.substr(first+1,last-first-1):text;
            if(path.size()>=2 && path.front()=='"' && path.back()=='"')path=path.substr(1,path.size()-2);
            return path;
        }
        inline bool InMap(const Json& entry,const Json& schema,const Json& value)
        {
            if(!schema.is_object())return false;
            if(value.is_array())
            {
                if(schema.contains("inner"))for(const auto& v:value)if(InMap(entry,schema.at("inner"),v))return true;
                return false;
            }
            if(value.is_object())
            {
                if(schema.contains("fields"))for(auto it=value.begin();it!=value.end();++it)
                    if(schema.at("fields").contains(it.key()) && InMap(entry,schema.at("fields").at(it.key()),it.value()))return true;
                return false;
            }
            const auto kind=schema.value("kind",std::string());
            if((kind!="ObjectProperty" && kind!="ClassProperty") || !value.is_string())return false;
            const auto path=Fold(ReferencePath(value.get<std::string>()));
            if(path.empty() || path=="none")return false;
            return path.rfind("mylevel.",0)==0 || (kind=="ObjectProperty" && Derives(entry,schema.value("type",std::string()),"actor"));
        }
    }
    // Offered unless a reference in it may point at an owned object, or its
    // declared class is unknown.
    inline bool Offered(const Json& entry)
    {
        std::vector<std::string> types;Detail::ReferenceTypes(entry.at("schema"),types);
        const auto lineage=entry.value("lineage",Json::object());
        for(const auto& type:types)if(!lineage.contains(type) || !SharedClass(lineage.at(type)))return false;
        return true;
    }
    inline bool ActorReference(const Json& entry)
    {
        std::vector<std::string> types;Detail::ReferenceTypes(entry.at("schema"),types);
        for(const auto& type:types)if(Detail::Derives(entry,type,"actor"))return true;
        return false;
    }
    // Whether the value names an actor, or anything stored in the map
    // (MyLevel): both are found by path, so once another map is open it would
    // name whatever there has the same name.
    inline bool NamesMapObject(const Json& entry){return Detail::InMap(entry,entry.at("schema"),entry.at("value"));}
    // Unticked to start with: where the actor is (Location, Rotation and the
    // brush transform PrePivot, MainScale, PostScale), what it is drawn as
    // (StaticMesh, DrawType), the names that wire and group it (Tag, Event,
    // Group, AttachTag, and an SMagicEvent's actions, Groups), its editor
    // flags, and every reference to another actor.
    inline bool CheckedByDefault(const Json& entry)
    {
        static const std::set<std::string> own{"location","rotation","staticmesh","drawtype","tag","event","group","attachtag","groups",
                                               "blocklocation","bhiddened","bhiddenedgroup","prepivot","mainscale","postscale"};
        if(own.count(Fold(entry.at("name").get<std::string>())))return false;
        return !ActorReference(entry);
    }
    // The offered properties, by category and then name, as the dialog lists them.
    inline Json Offer(const Json& properties)
    {
        std::vector<Json> offered;
        for(const auto& entry:properties)if(Offered(entry))offered.push_back(entry);
        std::stable_sort(offered.begin(),offered.end(),[](const Json& a,const Json& b){
            const auto ca=Fold(a.value("category",std::string())),cb=Fold(b.value("category",std::string()));
            return ca!=cb?ca<cb:Fold(a.at("name").get<std::string>())<Fold(b.at("name").get<std::string>());});
        return Json(offered);
    }
    // A target takes a value only through a property with the same schema: the
    // same kind, enum choices, struct fields and reference class, so an actor of
    // another class takes it when the property is the same. The Properties
    // category is not part of it.
    inline bool Applicable(const Json& entry,const Json& schema)
    {
        auto plain=[](Json value){if(value.is_object())value.erase("category");return value;};
        return plain(entry.at("schema"))==plain(schema);
    }
    // A value as the dialog shows it, cut short when long.
    inline std::string ValueText(const Json& value,size_t limit=160)
    {
        auto text=Magic::Text(value);
        if(text.size()>limit)text=text.substr(0,limit)+"...";
        return text;
    }
    inline std::string Count(size_t n,const char* one,const char* many){return std::to_string(n)+" "+(n==1?one:many);}
    // The dialog's line under the list.
    inline std::string ChoiceSummary(size_t ticked,size_t offered,size_t targets)
    {
        return std::to_string(ticked)+" of "+Count(offered,"property","properties")+" ticked, for "+Count(targets,"selected actor","selected actors")
              +". Each actor takes the ones it has with the same name and type, all in one Undo step.";
    }
    // What a paste did: actors changed, values written, and the values left out
    // where an actor has no property of that name and type, or is a brush or
    // mover, which never takes StaticMesh, DrawType or DrawScale(3D).
    inline std::string Summary(size_t actors,size_t values,size_t skipped)
    {
        if(!actors)return "None of the selected actors can take these properties, so nothing was pasted.";
        auto text="Pasted "+Count(values,"value","values")+" onto "+Count(actors,"actor","actors")+".";
        if(skipped)text+=" "+Count(skipped,"value was","values were")
                         +" left out where an actor has no property of the same name and type, or is a brush or mover, which keeps its own mesh, draw type and scale.";
        return text;
    }
}
