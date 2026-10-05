#pragma once
#include "MagicEventModel.h"
#include "RecoveredPolygonImport.h"
#include <set>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace Workflow::Authoring
{
    // Version 2 limits. The engine only requires power-of-two texture sides; the
    // 32-bit editor and game address space is the practical ceiling.
    constexpr uint32_t kMaximumTextureSide=8192;
    constexpr size_t kMaximumBrushPolygons=16384;
    constexpr size_t kMaximumOperationsV2=20000;
    constexpr size_t kMaximumSurfaces=2000000;
    // Version 3 adds deletes. Each one names an exported actor with its complete exported values, and the file must
    // also say "allowDeletes": true, so neither a stray operation nor a generator bug can empty a map.
    constexpr int kLatestVersion=3;
    constexpr size_t kMaximumDeletes=1000;
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
    // Version 2 names packages, groups and assets with plain Unreal names.
    inline void AssetName(const std::string& value,const char* what)
    {
        if(value.empty() || value.size()>63 || value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos)
            throw std::runtime_error(std::string(what)+" names use 1-63 letters, digits and underscores: "+value);
    }
    // Package.Name or Package.Group.Name (up to two groups).
    inline void AssetPath(const std::string& value)
    {
        size_t parts=0,start=0;
        for(;;)
        {
            auto dot=value.find('.',start);AssetName(value.substr(start,dot==std::string::npos?std::string::npos:dot-start),"Asset path");++parts;
            if(dot==std::string::npos)break;start=dot+1;
        }
        if(parts<2 || parts>4)throw std::runtime_error("Use Package.Name or Package.Group.Name for materials: "+value);
    }
    inline std::string Extension(const std::string& file)
    {
        auto dot=file.find_last_of('.');if(dot==std::string::npos || file.find_first_of("/\\",dot)!=std::string::npos)return {};
        return Fold(file.substr(dot));
    }
    // The editor's importer reads these; it rejects sizes that are not powers of two.
    inline std::pair<uint32_t,uint32_t> ImageSize(const std::string& header,const std::string& extension)
    {
        auto u16=[&](size_t at){if(header.size()<at+2)throw std::runtime_error("Image header is truncated.");return static_cast<uint32_t>(static_cast<unsigned char>(header[at])|static_cast<unsigned char>(header[at+1])<<8);};
        auto u32=[&](size_t at){return u16(at)|u16(at+2)<<16;};
        uint32_t w=0,h=0;
        if(extension==".bmp"){if(header.compare(0,2,"BM"))throw std::runtime_error("Not a BMP file.");w=u32(18);auto rows=static_cast<int32_t>(u32(22));h=static_cast<uint32_t>(rows<0?-static_cast<int64_t>(rows):rows);}
        else if(extension==".tga"){w=u16(12);h=u16(14);}
        else if(extension==".pcx"){if(header.empty() || header[0]!=10)throw std::runtime_error("Not a PCX file.");w=u16(8)-u16(4)+1;h=u16(10)-u16(6)+1;}
        else if(extension==".dds"){if(header.compare(0,4,"DDS "))throw std::runtime_error("Not a DDS file.");h=u32(12);w=u32(16);}
        else throw std::runtime_error("Import textures from .bmp, .tga, .pcx or .dds files.");
        auto power=[](uint32_t v){return v && !(v&(v-1));};
        if(!power(w) || !power(h))throw std::runtime_error("Texture sides must be powers of two: "+std::to_string(w)+"x"+std::to_string(h));
        if(w>kMaximumTextureSide || h>kMaximumTextureSide)throw std::runtime_error("Texture sides are limited to "+std::to_string(kMaximumTextureSide)+": "+std::to_string(w)+"x"+std::to_string(h));
        return {w,h};
    }
    inline RecoveredPolygonImport::Vec3 Point(const Json& value,const char* what)
    {
        if(!value.is_array() || value.size()!=3)throw std::runtime_error(std::string(what)+" needs [X, Y, Z] numbers.");
        double c[3];
        for(size_t i=0;i<3;++i)
        {
            if(!value[i].is_number())throw std::runtime_error(std::string(what)+" needs [X, Y, Z] numbers.");
            c[i]=value[i].get<double>();
            if(!std::isfinite(c[i]) || std::fabs(c[i])>1048576)throw std::runtime_error(std::string(what)+" coordinates must be finite and within 1048576.");
            c[i]=static_cast<float>(c[i]); // The editor stores FPoly coordinates as floats.
        }
        return {c[0],c[1],c[2]};
    }
    inline std::string VectorText(const RecoveredPolygonImport::Vec3& v)
    {
        // Round-trip float text, as map recovery writes it.
        std::ostringstream out;out<<std::showpos<<std::scientific<<std::setprecision(std::numeric_limits<float>::max_digits10-1)<<static_cast<float>(v.x)<<','<<static_cast<float>(v.y)<<','<<static_cast<float>(v.z);
        return out.str();
    }
    // Native brush text for version 2 "polygons" geometry. The native T3D importer
    // ignores Normal and cleans each polygon; reject polygons it would drop or reshape.
    inline std::string BrushPolygons(const Json& polygons)
    {
        if(!polygons.is_array() || polygons.empty() || polygons.size()>kMaximumBrushPolygons)
            throw std::runtime_error("A polygon brush needs 1-"+std::to_string(kMaximumBrushPolygons)+" polygons.");
        std::ostringstream out;out<<"Begin PolyList\r\n";
        for(size_t index=0;index<polygons.size();++index)
        {
            const auto& polygon=polygons[index];const auto where=" (polygon "+std::to_string(index)+")";
            Keys(polygon,{"texture","flags","origin","textureU","textureV","pan","vertices"});
            std::string texture;
            if(polygon.contains("texture")){texture=polygon.at("texture").get<std::string>();AssetPath(texture);}
            int64_t flags=0;
            if(polygon.contains("flags")){if(!polygon.at("flags").is_number_integer())throw std::runtime_error("Polygon flags must be an integer"+where);flags=polygon.at("flags").get<int64_t>();}
            if(flags<0 || flags>0xFFFFFFFFLL)throw std::runtime_error("Polygon flags must be an unsigned 32-bit value"+where);
            const auto& vertices=polygon.at("vertices");
            if(!vertices.is_array() || vertices.size()<3 || vertices.size()>RecoveredPolygonImport::kMaximumVertices)
                throw std::runtime_error("A polygon needs 3-16 vertices; split larger faces"+where);
            std::vector<RecoveredPolygonImport::Vec3> points;for(const auto& v:vertices)points.push_back(Point(v,"A vertex"));
            const auto native=RecoveredPolygonImport::Prepare(points);
            if(!native.accepted())throw std::runtime_error("The editor would discard this polygon (degenerate, collapsed or too small)"+where);
            if(!RecoveredPolygonImport::PreservesOutline(points,native))throw std::runtime_error("The editor's polygon cleanup would change this polygon's outline"+where);
            out<<"Begin Polygon";if(!texture.empty())out<<" Texture=\""<<texture<<"\"";out<<" Flags="<<flags<<"\r\n";
            if(polygon.contains("origin"))out<<"Origin   "<<VectorText(Point(polygon.at("origin"),"origin"))<<"\r\n";
            if(polygon.contains("textureU"))out<<"TextureU "<<VectorText(Point(polygon.at("textureU"),"textureU"))<<"\r\n";
            if(polygon.contains("textureV"))out<<"TextureV "<<VectorText(Point(polygon.at("textureV"),"textureV"))<<"\r\n";
            if(polygon.contains("pan"))
            {
                const auto& pan=polygon.at("pan");
                if(!pan.is_array() || pan.size()!=2 || !pan[0].is_number_integer() || !pan[1].is_number_integer() || std::abs(pan[0].get<int64_t>())>65536 || std::abs(pan[1].get<int64_t>())>65536)
                    throw std::runtime_error("pan needs [U, V] integers within 65536"+where);
                out<<"Pan      U="<<pan[0].get<int64_t>()<<" V="<<pan[1].get<int64_t>()<<"\r\n";
            }
            for(const auto& p:points)out<<"Vertex   "<<VectorText(p)<<"\r\n";
            out<<"End Polygon\r\n";
        }
        out<<"End PolyList\r\n";return out.str();
    }
    inline std::string TexturePath(const Json& op)
    {
        return op.at("package").get<std::string>()+(op.contains("group")?"."+op.at("group").get<std::string>():std::string{})+"."+op.at("name").get<std::string>();
    }
    inline std::filesystem::path Utf8Path(const std::string& text){return std::filesystem::path(std::u8string(text.begin(),text.end()));}
    inline std::string Utf8Text(const std::filesystem::path& path){auto text=path.u8string();return std::string(text.begin(),text.end());}
    // Version 2 texture files may be relative to the change file.
    inline void Rebase(Json& document,const std::filesystem::path& base)
    {
        if(!document.is_object() || !document.contains("operations") || !document.at("operations").is_array())return;
        for(auto& op:document["operations"])
            if(op.is_object() && op.value("op",std::string{})=="texture" && op.contains("file") && op.at("file").is_string())
            {
                std::filesystem::path file=Utf8Path(op.at("file").get<std::string>());
                if(file.is_relative() && !base.empty())op["file"]=Utf8Text((base/file).lexically_normal());
            }
    }
    // Every "$ref" text inside a property value.
    inline void References(const Json& value,std::vector<std::string>& out)
    {
        if(value.is_object())
        {
            if(value.contains("$ref") && value.at("$ref").is_string())out.push_back(value.at("$ref").get<std::string>());
            for(const auto& item:value)References(item,out);
        }
        else if(value.is_array())for(const auto& item:value)References(item,out);
    }
    inline void Validate(const Json& document, const std::string& map)
    {
        Keys(document,{"format","version","map","description","operations","expect","allowDeletes"});
        const auto& version=document.at("version");
        // Version 3 is version 2 plus deletes; everything later versions add keeps working in them.
        const bool v3=version==3,v2=version==2 || v3;
        if(document.at("format")!="scct.map-changes" || !(version==1 || v2) || !(document.at("map")==map || (v2 && document.at("map")=="*")))
            throw std::runtime_error("Unsupported change file or wrong map. Export the destination map first.");
        if(document.contains("description"))document.at("description").get<std::string>();
        if(document.contains("expect") && !document.at("expect").is_object())throw std::runtime_error("expect must map existing object paths to their exported values.");
        if(document.contains("allowDeletes") && (!v3 || !document.at("allowDeletes").is_boolean()))throw std::runtime_error("allowDeletes is a version 3 setting and must be true or false.");
        const bool allowDeletes=document.value("allowDeletes",false);
        const auto& ops=document.at("operations");
        const size_t limit=v2?kMaximumOperationsV2:2000;
        if(!ops.is_array() || ops.empty() || ops.size()>limit)throw std::runtime_error("Supply between 1 and "+std::to_string(limit)+" operations.");
        std::set<std::string> ids,updates,packages,textures,deletes;
        for(const auto& op:ops)
        {
            auto kind=op.at("op").get<std::string>();
            if(v3 && kind=="delete")
            {
                Keys(op,{"op","actor","before"});Keys(op.at("actor"),{"path","class"});
                const auto path=op.at("actor").at("path").get<std::string>();op.at("actor").at("class").get<std::string>();
                if(path.find('.')==std::string::npos)throw std::runtime_error("Delete an existing actor by its exported path, not a creation ID: "+path);
                if(!allowDeletes)throw std::runtime_error("This file deletes "+path+". Deleting actors needs \"allowDeletes\": true at the top of the change file.");
                if(document.at("map")=="*")throw std::runtime_error("A file that deletes actors must name the exported map, not \"*\".");
                if(!op.at("before").is_object() || op.at("before").empty())throw std::runtime_error("A delete needs the actor's complete exported values in before: "+path);
                if(!deletes.insert(Fold(path)).second)throw std::runtime_error("Duplicate delete: "+path);
                if(deletes.size()>kMaximumDeletes)throw std::runtime_error("A file deletes at most "+std::to_string(kMaximumDeletes)+" actors.");
                continue;
            }
            if(kind=="create" || kind=="component")
            {
                if(kind=="create")Keys(op,v2?std::initializer_list<const char*>{"op","id","class","geometry","polygons","properties"}:std::initializer_list<const char*>{"op","id","class","geometry","properties"});
                else Keys(op,{"op","id","class","owner","properties"});
                auto id=op.at("id").get<std::string>();Identifier(id);
                if(!ids.insert(Fold(id)).second)throw std::runtime_error("Duplicate creation ID: "+id);
                if(!op.at("class").is_string())throw std::runtime_error("A creation needs a class.");
                if(kind=="component" && !op.at("owner").is_string())throw std::runtime_error("A component needs an owner.");
                if(kind=="create")
                {
                    auto geometry=op.value("geometry",std::string("point"));
                    if(geometry!="point" && geometry!="box" && !(v2 && geometry=="polygons"))throw std::runtime_error("Unsupported geometry: "+geometry);
                    if((geometry=="polygons")!=op.contains("polygons"))throw std::runtime_error("Supply polygons exactly when geometry is \"polygons\": "+id);
                    if(geometry=="polygons")
                    {
                        try{BrushPolygons(op.at("polygons"));}
                        catch(const std::exception& e){throw std::runtime_error(id+": "+e.what());}
                    }
                }
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
            else if(v2 && kind=="load")
            {
                Keys(op,{"op","package"});
                auto package=op.at("package").get<std::string>();AssetName(package,"Package");packages.insert(Fold(package));continue;
            }
            else if(v2 && kind=="texture")
            {
                Keys(op,{"op","package","group","name","file","format","mips","masked","alphaTexture","lodSet"});
                AssetName(op.at("package").get<std::string>(),"Package");AssetName(op.at("name").get<std::string>(),"Texture");
                if(op.contains("group"))AssetName(op.at("group").get<std::string>(),"Group");
                const auto file=op.at("file").get<std::string>();
                if(file.empty() || file.size()>1024 || file.find_first_of("\"\r\n")!=std::string::npos)throw std::runtime_error("Texture files need a path without quotes or line breaks.");
                const auto extension=Extension(file);
                if(extension!=".bmp" && extension!=".tga" && extension!=".pcx" && extension!=".dds")throw std::runtime_error("Import textures from .bmp, .tga, .pcx or .dds files: "+file);
                if(op.contains("format"))
                {
                    auto format=op.at("format").get<std::string>();
                    if(format!="DXT1" && format!="DXT3" && format!="DXT5" && format!="RGBA8")throw std::runtime_error("Texture format must be DXT1, DXT3, DXT5 or RGBA8.");
                    if(extension==".dds")throw std::runtime_error("A .dds file keeps its own format and mips; omit format: "+file);
                }
                for(const char* flag:{"mips","masked","alphaTexture"})if(op.contains(flag))op.at(flag).get<bool>();
                if(op.contains("lodSet") && (!op.at("lodSet").is_number_integer() || op.at("lodSet").get<int>()<0 || op.at("lodSet").get<int>()>15))throw std::runtime_error("lodSet must be an integer 0-15.");
                if(!textures.insert(Fold(TexturePath(op))).second)throw std::runtime_error("Duplicate texture import: "+TexturePath(op));
                packages.insert(Fold(op.at("package").get<std::string>()));continue;
            }
            else if(v2 && kind=="save")
            {
                Keys(op,{"op","package","overwrite"});
                auto package=op.at("package").get<std::string>();AssetName(package,"Package");
                if(op.contains("overwrite"))op.at("overwrite").get<bool>();
                // Only packages this file loads or imports into; never an unrelated stock package.
                if(!packages.count(Fold(package)))throw std::runtime_error("Save only a package that an earlier load or texture operation names: "+package);
                continue;
            }
            else if(v2 && kind=="surface")
            {
                Keys(op,{"op","surfaces","texture"});
                AssetPath(op.at("texture").get<std::string>());
                const auto& surfaces=op.at("surfaces");
                if(!surfaces.is_array() || surfaces.empty() || surfaces.size()>kMaximumSurfaces)throw std::runtime_error("surfaces needs 1-"+std::to_string(kMaximumSurfaces)+" BSP surface indices.");
                std::set<int64_t> seen;
                for(const auto& s:surfaces)if(!s.is_number_integer() || s.get<int64_t>()<0 || s.get<int64_t>()>=static_cast<int64_t>(kMaximumSurfaces) || !seen.insert(s.get<int64_t>()).second)throw std::runtime_error("Surface indices must be unique nonnegative integers.");
                continue;
            }
            else throw std::runtime_error("Unsupported map operation: "+kind+(kind=="delete"?" (deletes need version 3)":v2?std::string{}:" (load, texture, save, surface and polygon brushes need version 2)"));
            if(!op.at("properties").is_object())throw std::runtime_error("Properties must be an object.");
        }
        if(deletes.empty())return;
        // An actor the file deletes cannot also be changed, linked, given components or referenced by it.
        auto refuse=[&](const std::string& path,const std::string& use)
        {if(deletes.count(Fold(path)))throw std::runtime_error(path+" is deleted by this file, so it cannot also be "+use+".");};
        for(const auto& op:ops)
        {
            const auto kind=op.at("op").get<std::string>();
            if(kind=="update")refuse(op.at("actor").at("path").get<std::string>(),"updated");
            else if(kind=="link"){refuse(op.at("event").get<std::string>(),"linked");refuse(op.at("target").get<std::string>(),"linked");}
            else if(kind=="component")refuse(op.at("owner").get<std::string>(),"given a component");
            if(op.contains("properties"))
            {
                std::vector<std::string> refs;References(op.at("properties"),refs);
                for(const auto& ref:refs)refuse(ref,"referenced");
            }
        }
    }
    inline bool Deletes(const Json& document)
    {
        for(const auto& op:document.at("operations"))if(op.at("op")=="delete")return true;
        return false;
    }

    // Preview rows. "change" is Add, Modify, Move, Delete, Link or Package; "target" names the actor (creation ID or
    // path); "select" is the existing actor's identity a row click selects, or null for actors the file creates.
    inline Json Row(const std::string& change,const std::string& target,const Json& select,const std::string& property={},const std::string& before={},const std::string& after={})
    {
        return {{"change",change},{"target",target},{"select",select},{"property",property},{"before",before},{"after",after}};
    }
    // A leaf, or a struct of leaves (a vector, rotator or colour), reads best whole.
    inline bool Compact(const Json& value)
    {
        if(!value.is_object() && !value.is_array())return true;
        if(value.is_array())return false;
        return std::all_of(value.begin(),value.end(),[](const Json& v){return !v.is_object() && !v.is_array();});
    }
    // Property-by-property differences, descending into structs and equal-length arrays to the fields that change.
    inline void Differences(const Json& before,const Json& after,const std::string& name,Json& out)
    {
        if(before==after)return;
        if(!Compact(after) && before.is_object() && after.is_object())
        {
            bool same=before.size()==after.size();
            for(auto it=after.begin();same && it!=after.end();++it)same=before.contains(it.key());
            if(same){for(auto it=after.begin();it!=after.end();++it)Differences(before.at(it.key()),it.value(),name+"."+it.key(),out);return;}
        }
        if(before.is_array() && after.is_array() && before.size()==after.size())
        {
            for(size_t i=0;i<after.size();++i)Differences(before[i],after[i],name+"["+std::to_string(i)+"]",out);
            return;
        }
        out.push_back({{"property",name},{"before",before.is_null()?std::string{}:Magic::Text(before)},{"after",Magic::Text(after)}});
    }
    // Location and Rotation are moves; everything else is a modification.
    inline std::string ChangeOf(const std::string& property)
    {
        const auto root=property.substr(0,property.find_first_of(".["));
        return root=="Location" || root=="Rotation"?"Move":"Modify";
    }
    // An update: before is the actor's complete exported values, changes the resolved properties the file sets.
    inline Json UpdateRows(const std::string& target,const Json& select,const Json& before,const Json& changes)
    {
        Json rows=Json::array(),differences=Json::array();
        for(auto it=changes.begin();it!=changes.end();++it)Differences(before.contains(it.key())?before.at(it.key()):Json(),it.value(),it.key(),differences);
        for(const auto& d:differences)rows.push_back(Row(ChangeOf(d.at("property")),target,select,d.at("property"),d.at("before"),d.at("after")));
        if(rows.empty())rows.push_back(Row("Modify",target,select,"(no change)"));
        return rows;
    }
    inline Json CreateRows(const std::string& id,const std::string& type,const Json& properties)
    {
        Json rows=Json::array({Row("Add",id,nullptr,"class",{},type)});
        for(auto it=properties.begin();it!=properties.end();++it)rows.push_back(Row("Add",id,nullptr,it.key(),{},Magic::Text(it.value())));
        return rows;
    }
    inline Json DeleteRows(const Json& identity,const Json& before)
    {
        const auto path=identity.at("path").get<std::string>();
        Json rows=Json::array({Row("Delete",path,identity,"class",identity.at("class").get<std::string>(),{})});
        if(before.contains("Location"))rows.push_back(Row("Delete",path,identity,"Location",Magic::Text(before.at("Location")),{}));
        return rows;
    }
    // The preview's heading: distinct actors added, modified, moved and deleted, then links and other steps (one row each).
    inline std::string Tally(const Json& rows)
    {
        std::map<std::string,std::set<std::string>> seen;std::map<std::string,size_t> steps;
        for(const auto& row:rows)
        {
            const auto change=row.at("change").get<std::string>();
            seen[change].insert(Fold(row.at("target").get<std::string>()));++steps[change];
        }
        std::string text;
        auto part=[&](const char* change,const char* word,bool distinct)
        {const auto n=distinct?seen[change].size():steps[change];if(n){if(!text.empty())text+=", ";text+=std::to_string(n)+" "+word;}};
        part("Add","added",true);part("Modify","modified",true);part("Move","moved",true);part("Delete","deleted",true);
        part("Link","link(s)",false);part("Package","package step(s)",false);part("Surface","surface step(s)",false);
        return text.empty()?"No changes":text;
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
