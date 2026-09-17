using System.Text.Json;
using R6VMapConverter;

if(args.Contains("--machine"))
{
    Console.WriteLine(JsonSerializer.Serialize(PathDiscovery.Find("",@"C:\R6VExports"),new JsonSerializerOptions(){WriteIndented=true}));return;
}
string root=Path.Combine(Path.GetTempPath(),"r6v-discovery-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
void Check(bool test,string message){if(!test)throw new Exception(message);Console.WriteLine("PASS: "+message);}
try
{
    string game=Path.Combine(root,"Steam library","steamapps","common","Rainbow Six Vegas"),cooked=Path.Combine(game,"KellerGame","Content","CookedPc");
    Directory.CreateDirectory(Path.Combine(cooked,"Maps","MP_Test"));
    string a=Path.Combine(cooked,"Maps","MP_Test","MP_Test_01.rmpc"),b=Path.Combine(cooked,"Maps","MP_Test","MP_Test_02.rmpc");
    File.WriteAllText(a,"");File.WriteAllText(b,"");
    Check(PathDiscovery.CookedUnder(game)==cooked,"game root resolves to CookedPc");
    Check(PathDiscovery.CookedUnder(cooked)==cooked,"CookedPc itself is accepted");
    Check(PathDiscovery.CookedUnder(root)==null,"unrelated directory is rejected");
    Check(PathDiscovery.FindMaps(cooked).Count==2,"source maps discovered recursively");
    Check(PathDiscovery.Find("missing",root,[game]).Cooked==cooked,"missing saved installation falls back to discovery");
    Check(PathDiscovery.Find(cooked,root,["missing"]).Cooked==cooked,"valid manual installation is preserved");

    string steam=Path.Combine(root,"Steam"),modern=Path.Combine(root,"Modern library"),legacy=Path.Combine(root,"Legacy library");Directory.CreateDirectory(Path.Combine(steam,"steamapps"));
    File.WriteAllText(Path.Combine(steam,"steamapps","libraryfolders.vdf"),"\"libraryfolders\" { \"0\" { \"path\" "+JsonSerializer.Serialize(modern)+" } \"1\" "+JsonSerializer.Serialize(legacy)+" }");
    var libraries=PathDiscovery.SteamLibraries(steam).ToList();
    Check(libraries.Contains(modern)&&libraries.Contains(legacy),"modern and legacy Steam libraries with escaped paths are parsed");

    string outputs=Path.Combine(root,"outputs");Directory.CreateDirectory(outputs);
    string MakeCache(string name,string map,bool missing=false)
    {
        string run=Path.Combine(outputs,name),dir=Path.Combine(run,"streamed");Directory.CreateDirectory(dir);
        File.WriteAllText(Path.Combine(run,"config.json"),JsonSerializer.Serialize(new{map}));
        if(!missing)File.WriteAllText(Path.Combine(dir,"image.bmp"),"fixture");
        string manifest=Path.Combine(dir,"recovered-all.json");File.WriteAllText(manifest,JsonSerializer.Serialize(new[]{new{source="Texture",status="recovered",file="image.bmp"}}));return manifest;
    }
    string first=MakeCache("first",a),second=MakeCache("second",b),broken=MakeCache("broken",a,true);
    File.SetLastWriteTimeUtc(first,DateTime.UtcNow.AddDays(-1));
    var caches=PathDiscovery.FindCaches([outputs]);
    Check(caches.Count==2,"missing-image caches are excluded");
    Check(caches[0].Path==second,"newest recovered cache is listed first");
    Check(caches.Single(c=>PathDiscovery.SamePath(c.Map,a)).Path==first,"cache association uses its source map, not just recency");
    Check(!PathDiscovery.SamePath(a,b),"different source maps do not match");
    string malformed=Path.Combine(root,"malformed.json");File.WriteAllText(malformed,"{}");
    Check(!PathDiscovery.UsableCache(malformed),"unrelated JSON is not accepted as a texture cache");
}
finally
{
    string resolved=Path.GetFullPath(root),temp=Path.GetFullPath(Path.GetTempPath()).TrimEnd('\\')+"\\";
    if(!resolved.StartsWith(temp,StringComparison.OrdinalIgnoreCase)||!Path.GetFileName(resolved).StartsWith("r6v-discovery-"))throw new Exception("Unexpected cleanup path");
    Directory.Delete(resolved,true);
}
