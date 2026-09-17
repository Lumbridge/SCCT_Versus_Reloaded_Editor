using System.Diagnostics;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace R6VMapConverter;

static class Program
{
    [STAThread]
    static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();
        if(args.Length==2 && args[0]=="--render-ui")
        {
            using var form=new MainForm(){ShowInTaskbar=false,Opacity=0};
            form.Show();Application.DoEvents();
            var deadline=Stopwatch.StartNew();
            while(form.DiscoveryPending&&deadline.Elapsed<TimeSpan.FromSeconds(15)){Application.DoEvents();Thread.Sleep(10);}
            using var bitmap=new Bitmap(form.Width,form.Height);
            form.DrawToBitmap(bitmap,new Rectangle(0,0,form.Width,form.Height));
            bitmap.Save(args[1]);return;
        }
        if(args.Length==2 && args[0]=="--run")
        {
            var options=JsonSerializer.Deserialize<ConversionOptions>(File.ReadAllText(args[1]))!;
            string logFile=args[1]+".log";
            var runner=new ConversionRunner(s=>File.AppendAllText(logFile,s+Environment.NewLine),(n,s)=>File.AppendAllText(logFile,$"{n}% {s}\n"));
            try{runner.RunAsync(options,true,CancellationToken.None).GetAwaiter().GetResult();Environment.ExitCode=0;}
            catch(Exception ex){File.AppendAllText(logFile,ex.ToString());Environment.ExitCode=1;}
            return;
        }
        Application.Run(new MainForm());
    }
}

sealed class MainForm : Form
{
    readonly ComboBox map=new(){DisplayMember="Label",DropDownStyle=ComboBoxStyle.DropDown,DropDownWidth=650,MaxDropDownItems=20,IntegralHeight=false};
    readonly TextBox cooked=new(),scct=new(),output=new(),package=new(),python=new(),umodel=new(),cache=new();
    readonly Button inspect=new(){Text="Inspect map"},convert=new(){Text="Convert to SCCT"},cancel=new(){Text="Cancel",Enabled=false},open=new(){Text="Open output",Enabled=false};
    readonly Button recover=new(){Text="Recover textures"},detect=new(){Text="Find paths"};
    readonly Dictionary<string,string> mapCaches=new(StringComparer.OrdinalIgnoreCase);
    List<CacheChoice> discoveredCaches=[];
    bool detecting,updatingPaths;
    public bool DiscoveryPending=>detecting;
    string MapPath=>(map.SelectedItem as MapChoice)?.Path??map.Text.Trim();
    string CacheHistoryPath=>Path.Combine(Path.GetDirectoryName(settingsPath)!,"map-caches.json");
    readonly RichTextBox log=new(){ReadOnly=true,BorderStyle=BorderStyle.None,BackColor=Color.FromArgb(18,25,35),ForeColor=Color.FromArgb(202,218,232),Font=new Font("Consolas",9),Dock=DockStyle.Fill};
    readonly Label status=new(){Text="Choose a map to get started",AutoSize=true};
    readonly ProgressBar progress=new(){Minimum=0,Maximum=100,Dock=DockStyle.Fill,Height=12};
    readonly TableLayoutPanel form=new(){Dock=DockStyle.Fill,ColumnCount=3,AutoSize=true};
    readonly string settingsPath=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"R6VMapConverter","settings.json");
    CancellationTokenSource? cancellation;
    string? lastRun;
    bool closeAfterCancel;
    readonly List<Control> inputs=[];

    public MainForm()
    {
        Text="R6V Map Converter";Width=1060;Height=920;MinimumSize=new Size(850,790);StartPosition=FormStartPosition.CenterScreen;
        Font=new Font("Segoe UI",10);BackColor=Color.FromArgb(238,242,247);AutoScaleMode=AutoScaleMode.Dpi;
        var layout=new TableLayoutPanel(){Dock=DockStyle.Fill,ColumnCount=1,RowCount=5,Padding=new Padding(24)};
        layout.RowStyles.Add(new(SizeType.Absolute,100));layout.RowStyles.Add(new(SizeType.AutoSize));layout.RowStyles.Add(new(SizeType.Absolute,55));layout.RowStyles.Add(new(SizeType.Percent,100));layout.RowStyles.Add(new(SizeType.Absolute,55));Controls.Add(layout);
        var heading=new FlowLayoutPanel(){Dock=DockStyle.Fill,FlowDirection=FlowDirection.TopDown,WrapContents=false};
        heading.Controls.Add(new Label(){Text="R6V → SCCT",Font=new Font("Segoe UI",25,FontStyle.Bold),AutoSize=true,ForeColor=Color.FromArgb(24,83,94)});
        heading.Controls.Add(new Label(){Text="Convert a Vegas map’s static meshes, textures and placements into editor-ready packages.",AutoSize=true});layout.Controls.Add(heading,0,0);
        form.ColumnStyles.Add(new(SizeType.Absolute,175));form.ColumnStyles.Add(new(SizeType.Percent,100));form.ColumnStyles.Add(new(SizeType.Absolute,105));
        AddField("R6V CookedPc",cooked,()=>{ChooseFolder(cooked);_=DetectPathsAsync();});
        AddField("Source map",map,ChooseMap);
        AddField("SCCT folder",scct,()=>ChooseFolder(scct));
        AddField("Output folder",output,()=>ChooseFolder(output));
        AddField("Package name",package,null);
        AddField("Texture cache",cache,()=>{ChooseFile(cache,"Texture cache manifest|*.json");RememberCache();});
        AddField("Python + Pillow",python,()=>ChooseFile(python,"Python executable|python.exe|Executable|*.exe"));
        AddField("UEViewer",umodel,()=>ChooseFile(umodel,"UEViewer executable|*.exe"));
        var note=new Label(){AutoSize=true,MaximumSize=new Size(850,0),Text="For streamed textures, load this map in Vegas and click Recover textures. Offline conversion uses resident textures and an optional cache. Missing colours get a mauve checker. BSP, terrain, lighting and gameplay are not included.",ForeColor=Color.FromArgb(72,86,105),Margin=new Padding(0,9,0,12)};
        form.Controls.Add(note,0,form.RowCount);form.SetColumnSpan(note,3);form.RowCount++;
        layout.Controls.Add(form,0,1);
        var buttons=new FlowLayoutPanel(){Dock=DockStyle.Fill,WrapContents=false};
        foreach(var b in new[]{detect,inspect,recover,convert,cancel,open}){b.AutoSize=true;b.Height=36;b.Padding=new Padding(10,3,10,3);b.Margin=new Padding(0,5,12,5);buttons.Controls.Add(b);}
        convert.BackColor=Color.FromArgb(25,104,112);convert.ForeColor=Color.White;convert.FlatStyle=FlatStyle.Flat;
        layout.Controls.Add(buttons,0,2);layout.Controls.Add(log,0,3);
        var footer=new TableLayoutPanel(){Dock=DockStyle.Fill,ColumnCount=1,RowCount=2,Padding=new Padding(0,10,0,0)};footer.Controls.Add(status);footer.Controls.Add(progress);layout.Controls.Add(footer,0,4);
        inspect.Click+=async (_,_)=>await Run(false);convert.Click+=async (_,_)=>await Run(true);cancel.Click+=(_,_)=>{cancel.Enabled=false;cancellation?.Cancel();status.Text="Cancelling and closing the isolated worker…";};
        recover.Click+=async (_,_)=>await Run(false,true);
        open.Click+=(_,_)=>{if(lastRun is not null)Process.Start(new ProcessStartInfo(lastRun){UseShellExecute=true});};
        map.TextChanged+=(_,_)=>{if(cancellation is null&&!updatingPaths)UpdatePackageName();};
        map.SelectionChangeCommitted+=(_,_)=>{UpdatePackageName();SelectMapCache();};
        map.Leave+=(_,_)=>{if(!updatingPaths&&cancellation is null)SelectMapCache();};
        cache.Leave+=(_,_)=>RememberCache();
        cooked.Leave+=async (_,_)=>{if(!updatingPaths&&cancellation is null)await DetectPathsAsync();};
        output.Leave+=async (_,_)=>{if(!updatingPaths&&cancellation is null)await DetectPathsAsync();};
        detect.Click+=async (_,_)=>await DetectPathsAsync();
        Shown+=async (_,_)=>await DetectPathsAsync();
        FormClosing+=(_,e)=>{if(cancellation is not null){e.Cancel=true;closeAfterCancel=true;cancellation.Cancel();}else SaveSettings();};
        updatingPaths=true;
        LoadSettings();
        try{if(File.Exists(CacheHistoryPath))foreach(var pair in JsonSerializer.Deserialize<Dictionary<string,string>>(File.ReadAllText(CacheHistoryPath))!)mapCaches[pair.Key]=pair.Value;}catch(Exception ex){Append("Could not load cache history: "+ex.Message);}
        RememberCache();
        updatingPaths=false;
    }

    void SetMapPath(string path)
    {
        var item=map.Items.Cast<MapChoice>().FirstOrDefault(m=>PathDiscovery.SamePath(m.Path,path));
        if(item is not null)map.SelectedItem=item;else {map.SelectedIndex=-1;map.Text=path;}
    }
    void UpdatePackageName()
    {
        string name=Regex.Replace(Path.GetFileNameWithoutExtension(MapPath),"[^A-Za-z0-9_]","_");
        package.Text="R6V_"+name[..Math.Min(30,name.Length)];
    }
    void RememberCache()
    {
        if(!File.Exists(MapPath)||!PathDiscovery.UsableCache(cache.Text))return;
        mapCaches[Path.GetFullPath(MapPath)]=cache.Text;
        try{Directory.CreateDirectory(Path.GetDirectoryName(CacheHistoryPath)!);File.WriteAllText(CacheHistoryPath,JsonSerializer.Serialize(mapCaches,new JsonSerializerOptions(){WriteIndented=true}));}
        catch(Exception ex){Append("Could not save cache association: "+ex.Message);}
    }
    void SelectMapCache()
    {
        if(detecting||cancellation is not null||!File.Exists(MapPath))return;
        string key=Path.GetFullPath(MapPath);
        string? remembered=mapCaches.GetValueOrDefault(key);
        string? selected=remembered is not null&&PathDiscovery.UsableCache(remembered)?remembered:
            discoveredCaches.FirstOrDefault(c=>PathDiscovery.SamePath(c.Map,key)&&PathDiscovery.UsableCache(c.Path))?.Path;
        cache.Text=selected??"";
        status.Text=selected is null?"No recovered cache found for this map — recover textures or convert offline":"Recovered texture cache selected for "+Path.GetFileNameWithoutExtension(key);
    }
    async Task DetectPathsAsync()
    {
        if(detecting||cancellation is not null||IsDisposed)return;
        detecting=true;detect.Enabled=false;status.Text="Looking for Vegas, maps and recovered texture caches...";
        string priorCooked=cooked.Text,priorMap=MapPath,priorOutput=output.Text,priorPackage=package.Text;
        try
        {
            var found=await Task.Run(()=>PathDiscovery.Find(priorCooked,priorOutput));
            if(IsDisposed||cancellation is not null)return;
            // Discard stale discovery results if the user changed an input while searching.
            if(cooked.Text!=priorCooked||MapPath!=priorMap||output.Text!=priorOutput)return;
            updatingPaths=true;
            if(found.Cooked is not null)cooked.Text=found.Cooked;
            map.BeginUpdate();map.Items.Clear();map.Items.AddRange(found.Maps.Cast<object>().ToArray());
            var previous=found.Maps.FirstOrDefault(m=>PathDiscovery.SamePath(m.Path,priorMap));
            map.SelectedItem=previous??found.Maps.FirstOrDefault();
            if(found.Maps.Count==0){map.SelectedIndex=-1;map.Text=File.Exists(priorMap)?priorMap:"";}
            map.EndUpdate();
            if(previous is not null)package.Text=priorPackage;else UpdatePackageName();
            discoveredCaches=found.Caches;
            updatingPaths=false;detecting=false;SelectMapCache();
            Append(found.Cooked is null?"Vegas was not found. Browse to its CookedPc folder.":"Detected Vegas: "+found.Cooked);
            Append($"Found {found.Maps.Count} source maps and {found.Caches.Count} caches in previous output folders. Choose a source map from the dropdown.");
            if(cache.Text.Length>0)Append("Matched texture cache: "+cache.Text);
            if(found.Cooked is null)status.Text="Vegas not found — use Browse to select CookedPc";
            else if(found.Maps.Count==0)status.Text="No .rmpc maps found in the selected CookedPc folder";
            SaveSettings();
        }
        catch(Exception ex){Append("Path detection: "+ex.Message);status.Text="Automatic detection could not finish — Browse is available";}
        finally{updatingPaths=false;detecting=false;if(!IsDisposed)detect.Enabled=cancellation is null;}
    }

    void AddField(string label,Control box,Action? browse)
    {
        int row=form.RowCount++;form.RowStyles.Add(new(SizeType.Absolute,38));
        form.Controls.Add(new Label(){Text=label,AutoSize=true,Anchor=AnchorStyles.Left},0,row);
        box.Dock=DockStyle.Fill;box.Margin=new Padding(0,4,10,4);form.Controls.Add(box,1,row);inputs.Add(box);
        if(browse is not null){var b=new Button(){Text="Browse…",Dock=DockStyle.Fill,Margin=new Padding(0,2,0,4)};b.Click+=(_,_)=>browse();form.Controls.Add(b,2,row);inputs.Add(b);}
    }
    void ChooseFolder(TextBox box){using var d=new FolderBrowserDialog(){SelectedPath=Directory.Exists(box.Text)?box.Text:""};if(d.ShowDialog(this)==DialogResult.OK)box.Text=d.SelectedPath;}
    void ChooseFile(TextBox box,string filter){using var d=new OpenFileDialog(){Filter=filter,CheckFileExists=true};if(d.ShowDialog(this)==DialogResult.OK)box.Text=d.FileName;}
    void ChooseMap(){using var d=new OpenFileDialog(){Filter="Rainbow Six Vegas maps|*.rmpc",InitialDirectory=Directory.Exists(cooked.Text)?cooked.Text:""};if(d.ShowDialog(this)==DialogResult.OK){SetMapPath(d.FileName);var parent=Directory.GetParent(d.FileName);while(parent is not null){if(parent.Name.Equals("CookedPc",StringComparison.OrdinalIgnoreCase)){cooked.Text=parent.FullName;_=DetectPathsAsync();break;}parent=parent.Parent;}}}
    ConversionOptions Options()=>new(cooked.Text.Trim(),MapPath,scct.Text.Trim(),output.Text.Trim(),package.Text.Trim(),python.Text.Trim(),umodel.Text.Trim(),cache.Text.Trim());
    void LoadSettings()
    {
        output.Text=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),"R6VExports");package.Text="R6V_Map";
        python.Text=File.Exists(Path.Combine(AppContext.BaseDirectory,"Python","python.exe"))?Path.Combine(AppContext.BaseDirectory,"Python","python.exe"):"python";
        umodel.Text=Path.Combine(AppContext.BaseDirectory,"Dependencies","umodel-research.exe");
        try{if(File.Exists(settingsPath)){var o=JsonSerializer.Deserialize<ConversionOptions>(File.ReadAllText(settingsPath))!;cooked.Text=o.Cooked;map.Text=o.Map;scct.Text=o.Scct;output.Text=o.Output;package.Text=o.Package;cache.Text=o.TextureCache;if(File.Exists(o.Python))python.Text=o.Python;if(File.Exists(o.Umodel))umodel.Text=o.Umodel;}}
        catch(Exception ex){Append("Could not load previous settings: "+ex.Message);}
    }
    void SaveSettings(){RememberCache();try{Directory.CreateDirectory(Path.GetDirectoryName(settingsPath)!);File.WriteAllText(settingsPath,JsonSerializer.Serialize(Options(),new JsonSerializerOptions(){WriteIndented=true}));}catch(Exception ex){Append("Could not save settings: "+ex.Message);}}
    void Append(string text){if(InvokeRequired){BeginInvoke(()=>Append(text));return;}log.AppendText(text+Environment.NewLine);log.SelectionStart=log.TextLength;log.ScrollToCaret();if(lastRun is not null)try{File.AppendAllText(Path.Combine(lastRun,"app.log"),text+Environment.NewLine);}catch(IOException){}}
    void Progress(int n,string text){if(InvokeRequired){BeginInvoke(()=>Progress(n,text));return;}progress.Value=n;status.Text=text;Append(text);}
    async Task Run(bool build,bool capture=false)
    {
        try{ConversionRunner.Validate(Options(),build);}catch(Exception ex){MessageBox.Show(this,ex.Message,"Check settings",MessageBoxButtons.OK,MessageBoxIcon.Information);return;}
        SaveSettings();log.Clear();lastRun=null;open.Enabled=false;detect.Enabled=inspect.Enabled=convert.Enabled=recover.Enabled=false;foreach(var c in inputs)c.Enabled=false;cancel.Enabled=true;cancellation=new();
        var runner=new ConversionRunner(Append,Progress);
        try{if(capture){cache.Text=await runner.CaptureAsync(Options(),cancellation.Token);RememberCache();}else await runner.RunAsync(Options(),build,cancellation.Token);lastRun=runner.RunDirectory;}
        catch(OperationCanceledException){status.Text="Cancelled — partial output retained";Append(status.Text);}
        catch(Exception ex){status.Text="Conversion stopped — see log";Append(ex.Message);}
        finally{lastRun=runner.RunDirectory;open.Enabled=lastRun is not null;cancel.Enabled=false;detect.Enabled=inspect.Enabled=convert.Enabled=recover.Enabled=true;foreach(var c in inputs)c.Enabled=true;cancellation.Dispose();cancellation=null;if(closeAfterCancel)Close();}
    }
}
