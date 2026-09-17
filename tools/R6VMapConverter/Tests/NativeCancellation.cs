using System.Diagnostics;
using System.Reflection;
using R6VMapConverter;

// Integration check against an explicitly supplied disposable, completed run.
// A read-only reload job is cancelled while its isolated editor is starting.
if(args.Length!=1)throw new ArgumentException("Supply a disposable conversion run directory.");
string run=Path.GetFullPath(args[0]),system=Path.Combine(run,"worker","System");
if(!File.Exists(Path.Combine(run,"config.json")) || !File.Exists(Path.Combine(run,"reload-job.json")))throw new ArgumentException("Not a conversion run.");
var existing=Process.GetProcessesByName("ChaosTheory_Editor").Where(p=>!(p.MainModule?.FileName??"").Contains("\\worker\\System\\",StringComparison.OrdinalIgnoreCase)).Select(p=>p.Id).ToArray();
var runner=new ConversionRunner(Console.WriteLine,(_,_)=>{});
typeof(ConversionRunner).GetProperty("RunDirectory")!.SetValue(runner,run);
using var cancellation=new CancellationTokenSource(TimeSpan.FromSeconds(8));
var method=typeof(ConversionRunner).GetMethod("NativeAsync",BindingFlags.Instance|BindingFlags.NonPublic)!;
bool cancelled=false;
try{await (Task)method.Invoke(runner,[system,"reload-job.json",cancellation.Token])!;}
catch(OperationCanceledException){cancelled=true;}
if(!cancelled)throw new Exception("The native job did not cancel.");
// Windows may enumerate a terminated process briefly while its last handles close.
await Task.Delay(1500);
foreach(var p in Process.GetProcessesByName("ChaosTheory_Editor"))
{
    using(p)if(!p.HasExited && string.Equals(p.MainModule?.FileName,Path.Combine(system,"ChaosTheory_Editor.exe"),StringComparison.OrdinalIgnoreCase))throw new Exception("Isolated editor survived cancellation.");
}
foreach(int pid in existing){using var p=Process.GetProcessById(pid);if(p.HasExited)throw new Exception("An existing editor was terminated.");}
Console.WriteLine("PASS: cancellation closed its isolated editor and preserved all pre-existing editors.");
