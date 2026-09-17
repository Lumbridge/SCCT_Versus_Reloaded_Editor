using Microsoft.Win32;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace R6VMapConverter;

public sealed record MapChoice(string Path)
{
    public string Label => System.IO.Path.GetFileNameWithoutExtension(Path)+"  —  "+new DirectoryInfo(System.IO.Path.GetDirectoryName(Path)!).Name;
}
public sealed record CacheChoice(string Path, string Map, DateTime Modified);
public sealed record DiscoveredPaths(string? Cooked, List<MapChoice> Maps, List<CacheChoice> Caches);

public static class PathDiscovery
{
    public static bool SamePath(string a,string b)
    {
        if(string.IsNullOrWhiteSpace(a)||string.IsNullOrWhiteSpace(b))return false;
        try{return string.Equals(System.IO.Path.GetFullPath(a).TrimEnd('\\','/'),System.IO.Path.GetFullPath(b).TrimEnd('\\','/'),StringComparison.OrdinalIgnoreCase);}
        catch(ArgumentException){return false;}
    }

    public static string? CookedUnder(string root)
    {
        if(string.IsNullOrWhiteSpace(root))return null;
        foreach(string candidate in new[]{root,System.IO.Path.Combine(root,"KellerGame","Content","CookedPc")})
            if(System.IO.Path.GetFileName(candidate.TrimEnd('\\','/')).Equals("CookedPc",StringComparison.OrdinalIgnoreCase) && Directory.Exists(System.IO.Path.Combine(candidate,"Maps")))return System.IO.Path.GetFullPath(candidate);
        return null;
    }

    // Steam libraryfolders.vdf has both modern "path" entries and legacy numbered entries.
    public static IEnumerable<string> SteamLibraries(string steamRoot)
    {
        yield return steamRoot;
        string file=System.IO.Path.Combine(steamRoot,"steamapps","libraryfolders.vdf");
        string text;
        try{text=File.ReadAllText(file);}catch(IOException){yield break;}catch(UnauthorizedAccessException){yield break;}
        foreach(Match match in Regex.Matches(text,"\"(?:path|[0-9]+)\"\\s*\"((?:\\\\.|[^\"\\\\])*)\"",RegexOptions.IgnoreCase))
        {
            string path=match.Groups[1].Value.Replace("\\\\","\\").Replace("\\\"","\"");
            if(System.IO.Path.IsPathFullyQualified(path))yield return path;
        }
    }

    public static List<string> InstallationCandidates()
    {
        var roots=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var steam=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach(var hive in new[]{RegistryHive.CurrentUser,RegistryHive.LocalMachine})
        foreach(var view in new[]{RegistryView.Registry32,RegistryView.Registry64})
        {
            try
            {
                using var key=RegistryKey.OpenBaseKey(hive,view);
                using var valve=key.OpenSubKey(@"Software\Valve\Steam");
                foreach(string name in new[]{"SteamPath","InstallPath"})if(valve?.GetValue(name) is string path)steam.Add(path);
                using var uninstall=key.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Uninstall");
                foreach(string name in uninstall?.GetSubKeyNames()??[])
                {
                    using var app=uninstall!.OpenSubKey(name);
                    string title=app?.GetValue("DisplayName") as string??"";
                    if(title.Contains("Rainbow Six",StringComparison.OrdinalIgnoreCase)&&title.Contains("Vegas",StringComparison.OrdinalIgnoreCase)&&!Regex.IsMatch(title,@"Vegas\s*2",RegexOptions.IgnoreCase)&&app?.GetValue("InstallLocation") is string path)roots.Add(path);
                }
            }
            catch(System.Security.SecurityException){}catch(UnauthorizedAccessException){}catch(IOException){}
        }
        steam.Add(System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),"Steam"));
        foreach(var drive in DriveInfo.GetDrives())
        {
            if(drive.DriveType!=DriveType.Fixed||!drive.IsReady)continue;
            foreach(string folder in new[]{"SteamLibrary","Steam","Games/Steam"})steam.Add(System.IO.Path.Combine(drive.RootDirectory.FullName,folder));
        }
        foreach(string library in steam.SelectMany(SteamLibraries).Distinct(StringComparer.OrdinalIgnoreCase))
        {
            string common=System.IO.Path.Combine(library,"steamapps","common");
            foreach(string folder in Directories(common))
                if(System.IO.Path.GetFileName(folder).Contains("Vegas",StringComparison.OrdinalIgnoreCase)&&!Regex.IsMatch(System.IO.Path.GetFileName(folder),@"Vegas\s*2",RegexOptions.IgnoreCase))roots.Add(folder);
        }
        return roots.ToList();
    }

    public static IEnumerable<string> Directories(string root)
    {
        try{return Directory.GetDirectories(root).Where(p=>(File.GetAttributes(p)&FileAttributes.ReparsePoint)==0).ToArray();}
        catch(IOException){return [];}catch(UnauthorizedAccessException){return [];}
    }

    public static List<MapChoice> FindMaps(string? cooked)
    {
        if(cooked is null)return [];
        var options=new EnumerationOptions(){RecurseSubdirectories=true,IgnoreInaccessible=true,AttributesToSkip=FileAttributes.ReparsePoint};
        try{return Directory.EnumerateFiles(System.IO.Path.Combine(cooked,"Maps"),"*.rmpc",options)
            .OrderByDescending(p=>System.IO.Path.GetFileName(p).StartsWith("MP_",StringComparison.OrdinalIgnoreCase)).ThenBy(p=>p,StringComparer.OrdinalIgnoreCase).Select(p=>new MapChoice(p)).ToList();}
        catch(IOException){return [];}catch(UnauthorizedAccessException){return [];}
    }

    public static bool UsableCache(string manifest)
    {
        try
        {
            using var json=JsonDocument.Parse(File.ReadAllText(manifest));
            if(json.RootElement.ValueKind!=JsonValueKind.Array)return false;
            return json.RootElement.EnumerateArray().Any(row=>row.ValueKind==JsonValueKind.Object && row.TryGetProperty("status",out var state)&&state.GetString()=="recovered"&&row.TryGetProperty("file",out var file)&&file.ValueKind==JsonValueKind.String&&File.Exists(System.IO.Path.Combine(System.IO.Path.GetDirectoryName(manifest)!,file.GetString()!)));
        }
        catch(IOException){return false;}catch(UnauthorizedAccessException){return false;}catch(JsonException){return false;}catch(ArgumentException){return false;}catch(InvalidOperationException){return false;}
    }

    public static List<CacheChoice> FindCaches(IEnumerable<string> outputRoots)
    {
        var found=new List<CacheChoice>();
        foreach(string root in outputRoots.Where(Directory.Exists).Distinct(StringComparer.OrdinalIgnoreCase))
        foreach(string run in Directories(root).Prepend(root))
        {
            // Only known run layouts are read: no recursive scan of game files or entire drives.
            string config=System.IO.Path.Combine(run,"config.json");
            try
            {
                using var json=JsonDocument.Parse(File.ReadAllText(config));
                if(!json.RootElement.TryGetProperty("map",out var map)||map.ValueKind!=JsonValueKind.String)continue;
                foreach(string manifest in new[]{System.IO.Path.Combine(run,"streamed","recovered-all.json"),System.IO.Path.Combine(run,"recovered-all.json")})
                    if(UsableCache(manifest))found.Add(new(manifest,map.GetString()!,File.GetLastWriteTimeUtc(manifest)));
            }
            catch(IOException){}catch(UnauthorizedAccessException){}catch(JsonException){}catch(InvalidOperationException){}
        }
        return found.OrderByDescending(c=>c.Modified).ToList();
    }

    public static DiscoveredPaths Find(string savedCooked,string output,IEnumerable<string>? candidates=null)
    {
        string? cooked=CookedUnder(savedCooked) ?? (candidates??InstallationCandidates()).Select(CookedUnder).FirstOrDefault(p=>p is not null);
        var caches=FindCaches(new[]{output,System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),"R6VExports"),System.IO.Path.Combine(System.IO.Path.GetPathRoot(Environment.SystemDirectory)!,"R6VExports")});
        return new(cooked,FindMaps(cooked),caches);
    }
}
