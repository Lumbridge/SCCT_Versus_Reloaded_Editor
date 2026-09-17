using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace R6VMapConverter;

public sealed record ConversionOptions(string Cooked, string Map, string Scct, string Output,
    string Package, string Python, string Umodel, string TextureCache);

public sealed class ConversionRunner(Action<string> log, Action<int, string> progress)
{
    readonly object logLock=new();
    void Emit(string text)
    {
        lock(logLock)
        {
            if(RunDirectory is not null)File.AppendAllText(Path.Combine(RunDirectory,"process.log"),text+Environment.NewLine);
            log(text.Length>2000?text[..2000]+" ... (full text in process.log)":text);
        }
    }
    public string? RunDirectory { get; private set; }
    static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };
    public static void Validate(ConversionOptions o, bool build)
    {
        if (!Directory.Exists(o.Cooked)) throw new InvalidOperationException("Select the R6V CookedPc directory.");
        if (!File.Exists(o.Map) || !o.Map.EndsWith(".rmpc", StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Select an R6V .rmpc map.");
        if (!Path.GetFullPath(o.Map).StartsWith(Path.GetFullPath(o.Cooked).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("The selected map must be inside CookedPc.");
        if (!Regex.IsMatch(o.Package, "^[A-Za-z][A-Za-z0-9_]{0,39}$"))
            throw new InvalidOperationException("Package name must start with a letter and contain up to 40 letters, digits or underscores.");
        if (string.IsNullOrWhiteSpace(o.Output)) throw new InvalidOperationException("Select an output folder.");
        if (o.TextureCache.Length > 0 && !File.Exists(o.TextureCache)) throw new InvalidOperationException("Texture cache manifest does not exist.");
        if (build && !File.Exists(o.Umodel)) throw new InvalidOperationException("Select the supplied UEViewer executable.");
        if (build && !File.Exists(Path.Combine(o.Scct,"System","ChaosTheory_Editor.exe")))
            throw new InvalidOperationException("Select the SCCT map editing folder containing System/ChaosTheory_Editor.exe.");
    }

    public async Task RunAsync(ConversionOptions o, bool build, CancellationToken ct)
    {
        Validate(o, build);
        if (build) await CheckEditorAsync(o.Scct, ct);
        // Legacy editor command parsing uses ANSI paths and fixed-size buffers.
        if (o.Output.Any(c => c > 127 || c == '"') || o.Output.Length > 100)
            throw new InvalidOperationException("Use a short output path with English characters (at most 100 characters), such as C:\\R6VExports.");
        RunDirectory = Path.Combine(Path.GetFullPath(o.Output), o.Package + "_" + DateTime.Now.ToString("yyyyMMdd_HHmmss") + "_" + Guid.NewGuid().ToString("N")[..6]);
        Directory.CreateDirectory(RunDirectory);
        Emit("Run folder: " + RunDirectory);
        var config = new { work = RunDirectory, cooked = Path.GetFullPath(o.Cooked), map = Path.GetFullPath(o.Map), package = o.Package,
            umodel = o.Umodel, textureCache = o.TextureCache };
        string configPath = Path.Combine(RunDirectory,"config.json");
        await File.WriteAllTextAsync(configPath, JsonSerializer.Serialize(config, JsonOptions), ct);
        await ProcessAsync(o.Python, ["-c", "import sys; from PIL import Image; print('Python '+sys.version.split()[0]+' / Pillow ready')"], ct);
        try
        {
            progress(5, build ? "Extracting and preparing assets" : "Inspecting map");
            await ProcessAsync(o.Python, ["-u", Path.Combine(AppContext.BaseDirectory,"Backend","pipeline.py"), build ? "prepare" : "scan", configPath], ct);
            if (!build) { progress(100,"Map inspection complete"); return; }
            progress(55,"Preparing isolated SCCT editor");
            string worker = Path.Combine(RunDirectory, "worker");
            await ProcessAsync("powershell.exe", ["-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
                Path.Combine(AppContext.BaseDirectory,"Native","PrepareEditor.ps1"), "-SourceRoot", o.Scct, "-WorkerRoot", worker], ct);
            string system = Path.Combine(worker,"System");
            foreach (string name in new[] { "ImportDriver.exe", "ImportWorker.dll" })
                File.Copy(Path.Combine(AppContext.BaseDirectory,"Native",name),Path.Combine(system,name));
            await File.WriteAllTextAsync(Path.Combine(system,"native_recovery_test.ini"),"[test]\nvisible_editor=0\n",ct);
            progress(65,"Building native mesh package");
            await NativeAsync(system, "build-job.json", ct);
            progress(78,"Placing meshes in the SCCT map");
            await NativeAsync(system, "map-job.json", ct);
            progress(90,"Reopening map and verifying placements");
            await NativeAsync(system, "reload-job.json", ct);
            await ProcessAsync(o.Python,["-u",Path.Combine(AppContext.BaseDirectory,"Backend","verify.py"),configPath],ct);
            await ProcessAsync(o.Python,["-u",Path.Combine(AppContext.BaseDirectory,"Backend","pipeline.py"),"report",configPath],ct);
            string deliver = Path.Combine(RunDirectory,"SCCT Packages");
            Directory.CreateDirectory(Path.Combine(deliver,"StaticMeshes"));
            Directory.CreateDirectory(Path.Combine(deliver,"MapsEd"));
            File.Copy(Path.Combine(RunDirectory,"assets",o.Package+".usx"),Path.Combine(deliver,"StaticMeshes",o.Package+".usx"));
            File.Copy(Path.Combine(RunDirectory,o.Package+"Placements.sdc"),Path.Combine(deliver,"MapsEd",o.Package+"Placements.sdc"));
            await File.WriteAllTextAsync(Path.Combine(RunDirectory,"README.txt"),
                "Copy the contents of SCCT Packages into your map editing folder's Packages directory.\r\n"+
                "Open MapsEd/"+o.Package+"Placements.sdc in the editor.\r\n"+
                "Review Catalogue.html and material-exceptions.json. Mauve checkers indicate unresolved colour.\r\n"+
                "This is a static scenery conversion. BSP, terrain, lighting and gameplay are not included.\r\n",ct);
            progress(100,"Conversion complete — ready to test");
            Emit("Native map reopened and placements verified. Packages are in: " + deliver);
        }
        catch (Exception ex)
        {
            await File.WriteAllTextAsync(Path.Combine(RunDirectory,"failure.txt"),ex.ToString());
            throw;
        }
    }

    async Task CheckEditorAsync(string root, CancellationToken ct)
    {
        var hashes = JsonSerializer.Deserialize<Dictionary<string,string>>(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory,"Native","compatibility.json"),ct))!;
        foreach (var pair in hashes)
        {
            string path=Path.Combine(root,"System",pair.Key);
            using var stream=File.OpenRead(path);
            string actual=Convert.ToHexString(await SHA256.HashDataAsync(stream,ct));
            if (!actual.Equals(pair.Value,StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Unsupported SCCT editor build: "+pair.Key+". This worker uses addresses verified for the supplied editing build; see compatibility.json.");
        }
    }

    async Task NativeAsync(string system,string job,CancellationToken ct)
    {
        File.Copy(Path.Combine(RunDirectory!,job),Path.Combine(system,"native-job.json"),true);
        string report=Path.Combine(system,"native_recovery_report.txt");
        for(int attempt=0;File.Exists(report);attempt++)
        {
            try{File.Delete(report);}
            catch(IOException) when(attempt<20){await Task.Delay(250,ct);}
        }
        string exe=Path.Combine(system,"ChaosTheory_Editor.exe");
        Process? editor=null;
        try
        {
            // Acquire ownership of the spawned PID before honouring cancellation.
            string output=await ProcessAsync(Path.Combine(system,"ImportDriver.exe"),[exe,Path.Combine(system,"Reloaded.Editor.dll"),Path.Combine(system,"ImportWorker.dll")],CancellationToken.None);
            if(!int.TryParse(output.Trim(),out int pid))throw new InvalidOperationException("Native worker returned no process ID.");
            editor=Process.GetProcessById(pid);
            using var timeout=new CancellationTokenSource(TimeSpan.FromMinutes(30));
            using var linked=CancellationTokenSource.CreateLinkedTokenSource(ct,timeout.Token);
            var watch=Stopwatch.StartNew();int lastHeartbeat=0;
            while(true)
            {
                linked.Token.ThrowIfCancellationRequested();
                string text="";
                if(File.Exists(report))
                {
                    using var file=new FileStream(report,FileMode.Open,FileAccess.Read,FileShare.ReadWrite);
                    using var reader=new StreamReader(file);text=await reader.ReadToEndAsync(linked.Token);
                }
                if(Regex.IsMatch(text,@"(?m)^PASS")) { Emit(job+": native checks passed"); File.WriteAllText(Path.Combine(RunDirectory!,job+".log"),text);break; }
                if(Regex.IsMatch(text,@"(?m)^FAIL") || editor.HasExited)
                    throw new InvalidOperationException("Native editor failed during "+job+". "+text[^Math.Min(text.Length,3000)..]);
                if(watch.Elapsed.TotalSeconds-lastHeartbeat>=20){lastHeartbeat=(int)watch.Elapsed.TotalSeconds;Emit(job+": editor working ("+lastHeartbeat+"s)");}
                await Task.Delay(500,linked.Token);
            }
        }
        finally
        {
            // Only terminate the isolated process created for this job, never a user's editor.
            if(editor is not null)
            {
                try { if(!editor.HasExited && string.Equals(editor.MainModule?.FileName,exe,StringComparison.OrdinalIgnoreCase)) { editor.Kill();await editor.WaitForExitAsync(); } }
                catch(InvalidOperationException) { }
                editor.Dispose();
            }
        }
    }

    public async Task<string> CaptureAsync(ConversionOptions o, CancellationToken ct)
    {
        Validate(o,false);
        var games=Process.GetProcessesByName("R6Vegas_Game");
        if(games.Length!=1) { foreach(var game in games)game.Dispose(); throw new InvalidOperationException("Run one copy of Rainbow Six Vegas and load the selected map before recovering textures."); }
        int pid=games[0].Id;games[0].Dispose();
        RunDirectory=Path.Combine(Path.GetFullPath(o.Output),"TextureCache_"+DateTime.Now.ToString("yyyyMMdd_HHmmss")+"_"+Guid.NewGuid().ToString("N")[..6]);
        Directory.CreateDirectory(RunDirectory);
        string config=Path.Combine(RunDirectory,"config.json");
        await File.WriteAllTextAsync(config,JsonSerializer.Serialize(new {work=RunDirectory,cooked=o.Cooked,map=o.Map,package=o.Package},JsonOptions),ct);
        progress(10,"Recovering streamed textures from the running map (read-only)");
        await ProcessAsync(o.Python,["-u",Path.Combine(AppContext.BaseDirectory,"Backend","capture.py"),config,pid.ToString()],ct);
        progress(100,"Texture cache ready — you can now convert the map");
        return Path.Combine(RunDirectory,"streamed","recovered-all.json");
    }

    async Task<string> ProcessAsync(string filename,IEnumerable<string> args,CancellationToken ct)
    {
        var info=new ProcessStartInfo(filename){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
        foreach(string arg in args)info.ArgumentList.Add(arg);
        using var process=Process.Start(info) ?? throw new InvalidOperationException("Could not start "+filename);
        var stdout=new System.Text.StringBuilder();
        async Task ReadAsync(StreamReader reader,bool capture)
        {
            while(await reader.ReadLineAsync() is { } line){if(capture)stdout.AppendLine(line);Emit(line);}
        }
        var reads=Task.WhenAll(ReadAsync(process.StandardOutput,true),ReadAsync(process.StandardError,false));
        try { await process.WaitForExitAsync(ct);await reads; }
        catch { if(!process.HasExited)process.Kill(entireProcessTree:true);await process.WaitForExitAsync();await reads;throw; }
        if(process.ExitCode!=0)throw new InvalidOperationException(Path.GetFileName(filename)+" exited with code "+process.ExitCode+". See the log for details.");
        return stdout.ToString();
    }
}
