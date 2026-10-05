#include "../Reloaded.Editor/MapAuthoringModel.h"
#include <iostream>
#include <filesystem>
#include <source_location>
using namespace Workflow;
void Require(bool value,std::source_location where=std::source_location::current()){if(!value){std::cerr<<"Map authoring assertion failed at line "<<where.line()<<std::endl;throw std::runtime_error("Map authoring assertion failed");}}
template<class F> void Reject(F operation,std::source_location where=std::source_location::current()){bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}Require(rejected,where);}
int main()
{
    Json document={{"format","scct.map-changes"},{"version",1},{"map","fixture"},{"operations",Json::array({
        {{"op","create"},{"id","Steam"},{"class","SBase.SSpawnableEmitter"},{"properties",Json::object()}},
        {{"op","component"},{"id","Particles"},{"owner","Steam"},{"class","Engine.SpriteEmitter"},{"properties",Json::object()}}
    })}};
    Authoring::Validate(document,"fixture");
    Reject([&]{Authoring::Validate(document,"other");});
    auto bad=document;bad["version"]=4;Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=document;bad["operations"][1]["id"]="steam";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=document;bad["operations"][0]["id"]="Steam\nEnd Actor";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=document;bad["operations"][0]["command"]="MAP NEW";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=document;bad["operations"][0]["op"]="delete";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=document;bad["operations"]=Json::array();Reject([&]{Authoring::Validate(bad,"fixture");});
    Json field={{"kind","ObjectProperty"},{"type","Engine.Actor"}};
    Json schema={{"kind","ArrayProperty"},{"inner",{{"kind","StructProperty"},{"fields",{{"Target",field},{"Delay",{{"kind","FloatProperty"}}}}}}}};
    Json values=Json::array({{{"Target",{{"$ref","Steam"}}},{"Delay","1.5"}}});
    auto resolve=[](const std::string& id,const Json&)->Json{if(id!="Steam")throw std::runtime_error("Unknown reference");return "SBase.SSpawnableEmitter'MyLevel.Steam_2'";};
    auto resolved=Authoring::Resolve(schema,values,resolve);
    Require(resolved[0]["Target"]=="SBase.SSpawnableEmitter'MyLevel.Steam_2'" && values[0]["Target"].is_object());
    values[0]["Target"]["$ref"]="missing";Reject([&]{Authoring::Resolve(schema,values,resolve);});
    values[0]["Target"]={{"$ref","Steam"},{"extra",true}};Reject([&]{Authoring::Resolve(schema,values,resolve);});
    values[0]["Target"]="None";values[0]["Delay"]="nan";Reject([&]{Authoring::Resolve(schema,values,resolve);});
    auto link=Json{{"op","link"},{"event","Event"},{"target","Steam"},{"trigger",false},{"delay","2.5"}};
    document["operations"].push_back(link);Authoring::Validate(document,"fixture");
    document["operations"].back()["delay"]="-1";Reject([&]{Authoring::Validate(document,"fixture");});
    document["operations"].back()["delay"]="2";document["operations"].back()["trigger"]=true;Reject([&]{Authoring::Validate(document,"fixture");});
    // Version 2: package loads, texture imports, package saves, polygon brushes and surface textures.
    Json quad={{"texture","CisternHR.Walls.Brick"},{"flags",0},{"textureU",{1,0,0}},{"textureV",{0,1,0}},{"pan",{0,0}},
        {"vertices",{{-64,-64,0},{64,-64,0},{64,64,0},{-64,64,0}}}};
    Json v2={{"format","scct.map-changes"},{"version",2},{"map","*"},{"operations",Json::array({
        {{"op","load"},{"package","CisternHR"}},
        {{"op","texture"},{"package","CisternHR"},{"group","Walls"},{"name","Brick"},{"file","brick.tga"},{"format","DXT5"},{"mips",true}},
        {{"op","save"},{"package","CisternHR"},{"overwrite",true}},
        {{"op","create"},{"id","Floor"},{"class","Engine.Brush"},{"geometry","polygons"},{"polygons",Json::array({quad})},{"properties",{{"Location",{{"X","0"},{"Y","0"},{"Z","0"}}}}}},
        {{"op","surface"},{"surfaces",{0,3}},{"texture","CisternHR.Walls.Brick"}}
    })}};
    Authoring::Validate(v2,"fixture");
    auto text=Authoring::BrushPolygons(v2["operations"][3]["polygons"]);
    Require(text.find("Begin Polygon Texture=\"CisternHR.Walls.Brick\" Flags=0")!=std::string::npos && text.find("Pan      U=0 V=0")!=std::string::npos && text.find("Vertex   -6.4")!=std::string::npos);
    bad=v2;bad["version"]=1;bad["map"]="fixture";Reject([&]{Authoring::Validate(bad,"fixture");});                  // v2 ops need version 2
    bad=document;bad["map"]="*";Reject([&]{Authoring::Validate(bad,"fixture");});                                    // v1 keeps its map binding
    bad=v2;bad["operations"][1]["file"]="brick.png";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v2;bad["operations"][1]["file"]="brick.dds";Reject([&]{Authoring::Validate(bad,"fixture");});                // DDS keeps its own format
    bad=v2;bad["operations"][1]["format"]="DXT2";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v2;bad["operations"][1]["package"]="Cistern\"HR";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v2;bad["operations"][2]["package"]="Engine";Reject([&]{Authoring::Validate(bad,"fixture");});                // only packages this file names
    bad=v2;bad["operations"][3].erase("polygons");Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v2;bad["operations"][3]["polygons"][0]["vertices"]={{0,0,0},{1,0,0}};Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v2;bad["operations"][3]["polygons"][0]["vertices"]={{0,0,0},{0.0005,0,0},{0.001,0,0}};Reject([&]{Authoring::Validate(bad,"fixture");}); // collapses natively
    bad=v2;bad["operations"][3]["polygons"][0]["vertices"]={{0,0,0},{1,0,0},{2,0,0}};Reject([&]{Authoring::Validate(bad,"fixture");});         // no normal
    bad=v2;bad["operations"][3]["polygons"][0]["texture"]="Brick";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v2;bad["operations"][3]["polygons"][0]["flags"]=-1;Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v2;bad["operations"][3]["polygons"][0]["shader"]="x";Reject([&]{Authoring::Validate(bad,"fixture");});
    Json many=Json::array();for(int i=0;i<17;++i)many.push_back({std::cos(i*0.36)*64,std::sin(i*0.36)*64,0});
    bad=v2;bad["operations"][3]["polygons"][0]["vertices"]=many;Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v2;bad["operations"][4]["surfaces"]={1,1};Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v2;bad["operations"][4]["surfaces"]={-1};Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v2;bad["operations"].push_back(bad["operations"][1]);Reject([&]{Authoring::Validate(bad,"fixture");});     // duplicate texture
    // Relative texture files resolve beside the change file.
    auto rebased=v2;Authoring::Rebase(rebased,std::filesystem::path("C:/maps"));
    Require(Authoring::Utf8Path(rebased["operations"][1]["file"].get<std::string>())==std::filesystem::path("C:/maps/brick.tga").lexically_normal());
    auto header=[](std::initializer_list<int> bytes,size_t size){std::string h(size,'\0');size_t i=0;for(int b:bytes)h[i++]=static_cast<char>(b);return h;};
    auto tga=header({0,0,2,0,0,0,0,0,0,0,0,0,0x00,0x10,0x00,0x08},18);
    Require(Authoring::ImageSize(tga,".tga")==std::make_pair(4096u,2048u));
    tga[12]=0x00;tga[13]=0x03;Reject([&]{Authoring::ImageSize(tga,".tga");});                                         // 768 is not a power of two
    tga[12]=0x00;tga[13]=0x40;Reject([&]{Authoring::ImageSize(tga,".tga");});                                         // 16384 exceeds the limit
    auto dds=header({'D','D','S',' ',124,0,0,0,0,0,0,0,0,4,0,0,0,8,0,0},32);
    Require(Authoring::ImageSize(dds,".dds")==std::make_pair(2048u,1024u));
    auto bmp=header({'B','M'},54);bmp[18]=0;bmp[19]=2;bmp[22]=0;bmp[23]=static_cast<char>(0xfe);bmp[24]=static_cast<char>(0xff);bmp[25]=static_cast<char>(0xff); // top-down 512x512
    Require(Authoring::ImageSize(bmp,".bmp")==std::make_pair(512u,512u));
    // Version 3: explicit deletes. Each names an exported actor with its complete values; the file opts in.
    Json light={{"path","MyLevel.Light3"},{"class","Engine.Light"}};
    Json lightValues={{"LightBrightness","64"},{"Location",{{"X","0"},{"Y","0"},{"Z","0"}}},{"Tag","Light"}};
    Json v3={{"format","scct.map-changes"},{"version",3},{"map","fixture"},{"allowDeletes",true},{"operations",Json::array({
        {{"op","delete"},{"actor",light},{"before",lightValues}},
        {{"op","create"},{"id","Lamp"},{"class","Engine.Light"},{"properties",{{"Location",{{"X","8"},{"Y","0"},{"Z","0"}}}}}}
    })}};
    Authoring::Validate(v3,"fixture");Require(Authoring::Deletes(v3));
    auto oldFile=v2;oldFile["version"]=3;Authoring::Validate(oldFile,"fixture");Require(!Authoring::Deletes(oldFile));   // v3 keeps every v2 operation
    auto v1=document;v1["operations"].back()["trigger"]=false;Authoring::Validate(v1,"fixture");                                                                            // v1 files still import
    bad=v3;bad.erase("allowDeletes");Reject([&]{Authoring::Validate(bad,"fixture");});                                 // no opt-in, no delete
    bad=v3;bad["allowDeletes"]=false;Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v3;bad["allowDeletes"]="true";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v3;bad["version"]=2;Reject([&]{Authoring::Validate(bad,"fixture");});                                          // deletes need version 3
    bad=v1;bad["allowDeletes"]=true;Reject([&]{Authoring::Validate(bad,"fixture");});                            // and so does the flag
    bad=v3;bad["map"]="*";Reject([&]{Authoring::Validate(bad,"fixture");});                                            // only the exported map
    bad=v3;bad["operations"][0].erase("before");Reject([&]{Authoring::Validate(bad,"fixture");});                      // complete exported values
    bad=v3;bad["operations"][0]["before"]=Json::object();Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v3;bad["operations"][0]["actor"]["path"]="Lamp";Reject([&]{Authoring::Validate(bad,"fixture");});              // not a creation ID
    bad=v3;bad["operations"][0]["actor"]["name"]="x";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v3;bad["operations"][0]["all"]=true;Reject([&]{Authoring::Validate(bad,"fixture");});                          // no wildcard or mode field
    bad=v3;bad["operations"].push_back(bad["operations"][0]);bad["operations"].back()["actor"]["path"]="mylevel.LIGHT3";Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v3;bad["operations"].push_back({{"op","update"},{"actor",light},{"before",lightValues},{"properties",{{"LightBrightness","9"}}}});Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v3;bad["operations"].push_back({{"op","link"},{"event","MyLevel.Light3"},{"target","Lamp"},{"trigger",false}});Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v3;bad["operations"].push_back({{"op","component"},{"id","Bits"},{"owner","MyLevel.Light3"},{"class","Engine.SpriteEmitter"},{"properties",Json::object()}});Reject([&]{Authoring::Validate(bad,"fixture");});
    bad=v3;bad["operations"][1]["properties"]["Owner"]={{"$ref","MyLevel.Light3"}};Reject([&]{Authoring::Validate(bad,"fixture");}); // referenced by the same file
    {
        auto crowd=v3;crowd["operations"]=Json::array();
        for(size_t i=0;i<=Authoring::kMaximumDeletes;++i)crowd["operations"].push_back({{"op","delete"},{"actor",{{"path","MyLevel.A"+std::to_string(i)},{"class","Engine.Light"}}},{"before",lightValues}});
        Reject([&]{Authoring::Validate(crowd,"fixture");});
        crowd["operations"].erase(crowd["operations"].size()-1);Authoring::Validate(crowd,"fixture");
    }
    // Preview rows: property-by-property differences, moves apart from modifications.
    Json differences=Json::array();
    Authoring::Differences(Json{{"X","0"},{"Y","0"},{"Z","0"}},Json{{"X","128"},{"Y","0"},{"Z","0"}},"Location",differences);
    Require(differences.size()==1 && differences[0]["property"]=="Location" && differences[0]["before"]=="(X=0,Y=0,Z=0)" && differences[0]["after"]=="(X=128,Y=0,Z=0)");
    Json groups=Json::array({{{"EventGroup",Json::array({{{"Delay","0"},{"Event","A"}},{{"Delay","1"},{"Event","B"}}})},{"Repeat","False"}}});
    auto changedGroups=groups;changedGroups[0]["EventGroup"][1]["Delay"]="2.5";
    differences=Json::array();Authoring::Differences(groups,changedGroups,"Groups",differences);
    Require(differences.size()==1 && differences[0]["property"]=="Groups[0].EventGroup[1]" && differences[0]["after"]=="(Delay=2.5,Event=B)");
    changedGroups[0]["EventGroup"].push_back({{"Delay","3"},{"Event","C"}});
    differences=Json::array();Authoring::Differences(groups,changedGroups,"Groups",differences);
    Require(differences.size()==1 && differences[0]["property"]=="Groups[0].EventGroup");                                // a new array length shows whole
    differences=Json::array();Authoring::Differences(groups,groups,"Groups",differences);Require(differences.empty());
    auto rows=Authoring::UpdateRows("MyLevel.Light3",light,lightValues,{{"LightBrightness","96"},{"Location",{{"X","0"},{"Y","32"},{"Z","0"}}},{"Tag","Light"}});
    Require(rows.size()==2 && rows[0]["change"]=="Modify" && rows[0]["property"]=="LightBrightness" && rows[0]["before"]=="64" && rows[0]["after"]=="96" && rows[0]["select"]==light);
    Require(rows[1]["change"]=="Move" && rows[1]["property"]=="Location");                                                // Tag is unchanged, so not listed
    Require(Authoring::ChangeOf("Rotation")=="Move" && Authoring::ChangeOf("Location.X")=="Move" && Authoring::ChangeOf("LocationName")=="Modify");
    Require(Authoring::UpdateRows("MyLevel.Light3",light,lightValues,{{"Tag","Light"}})[0]["property"]=="(no change)");
    auto added=Authoring::CreateRows("Lamp","Engine.Light",{{"Location",{{"X","8"},{"Y","0"},{"Z","0"}}}});
    Require(added.size()==2 && added[0]["change"]=="Add" && added[0]["after"]=="Engine.Light" && added[0]["select"].is_null() && added[1]["after"]=="(X=8,Y=0,Z=0)");
    auto removed=Authoring::DeleteRows(light,lightValues);
    Require(removed.size()==2 && removed[0]["change"]=="Delete" && removed[0]["select"]==light && removed[1]["before"]=="(X=0,Y=0,Z=0)");
    Json all=Json::array();for(const auto* set:{&rows,&added,&removed})for(const auto& r:*set)all.push_back(r);
    all.push_back(Authoring::Row("Link","Event",nullptr,"action (group 0)",{},"Lamp"));
    Require(Authoring::Tally(all)=="1 added, 1 modified, 1 moved, 1 deleted, 1 link(s)");
    Require(Authoring::Tally(Json::array())=="No changes");
    std::cout<<"Map authoring model tests passed\n";
}
