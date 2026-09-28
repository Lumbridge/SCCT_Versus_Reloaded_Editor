#include "../Reloaded.Editor/MapAuthoringModel.h"
#include <iostream>
#include <filesystem>
using namespace Workflow;
void Require(bool value){if(!value)throw std::runtime_error("Map authoring assertion failed");}
template<class F> void Reject(F operation){bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}Require(rejected);}
int main()
{
    Json document={{"format","scct.map-changes"},{"version",1},{"map","fixture"},{"operations",Json::array({
        {{"op","create"},{"id","Steam"},{"class","SBase.SSpawnableEmitter"},{"properties",Json::object()}},
        {{"op","component"},{"id","Particles"},{"owner","Steam"},{"class","Engine.SpriteEmitter"},{"properties",Json::object()}}
    })}};
    Authoring::Validate(document,"fixture");
    Reject([&]{Authoring::Validate(document,"other");});
    auto bad=document;bad["version"]=3;Reject([&]{Authoring::Validate(bad,"fixture");});
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
    std::cout<<"Map authoring model tests passed\n";
}
