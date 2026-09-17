using System.Text;
using System.Text.Json;

namespace SCDAMapConverter;

record MapProbe(string File, long Bytes, string Variant, string Magic, string Status, string[] CompanionFiles, Dictionary<string, int> ClassCounts, string[] ObjectNames);

static class Program
{
    [STAThread]
    static void Main() { ApplicationConfiguration.Initialize(); Application.Run(new MainForm()); }
}

sealed class MainForm : Form
{
    readonly TextBox source = new(), output = new();
    readonly ComboBox map = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    readonly Button scan = new() { Text = "Scan maps" }, inspect = new() { Text = "Inspect map" }, convert = new() { Text = "Prepare conversion" };
    readonly RichTextBox log = new() { ReadOnly = true, Dock = DockStyle.Fill, BackColor = Color.FromArgb(18, 25, 35), ForeColor = Color.FromArgb(210, 225, 235), Font = new Font("Consolas", 9) };
    readonly Label status = new() { AutoSize = true, Text = "Choose the Double Agent MapsPC folder." };
    List<string> maps = [];

    public MainForm()
    {
        Text = "SCDA Map Converter v0.1.0"; Width = 940; Height = 680; MinimumSize = new Size(760, 560); StartPosition = FormStartPosition.CenterScreen;
        var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(24), ColumnCount = 1, RowCount = 5 };
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 80)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 120)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 52)); layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100)); layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
        layout.Controls.Add(new Label { Text = "SCDA → SCCT", Font = new Font("Segoe UI", 25, FontStyle.Bold), AutoSize = true }, 0, 0);
        var fields = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, RowCount = 3 }; fields.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 180)); fields.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        AddField(fields, 0, "Double Agent MapsPC", source); AddField(fields, 1, "Map", map); AddField(fields, 2, "Output folder", output); layout.Controls.Add(fields, 0, 1);
        var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill }; foreach (var b in new[] { scan, inspect, convert }) { b.AutoSize = true; b.Height = 36; buttons.Controls.Add(b); } layout.Controls.Add(buttons, 0, 2);
        layout.Controls.Add(log, 0, 3); layout.Controls.Add(status, 0, 4); Controls.Add(layout);
        source.Text = @"C:\Users\ryans\Desktop\Enhanced SCDA Online 2.2\Packages\_Common\MapsPC"; output.Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "SCDAExports");
        scan.Click += (_, _) => Scan(); inspect.Click += (_, _) => Inspect(); convert.Click += (_, _) => Prepare(); Shown += (_, _) => Scan();
    }
    static void AddField(TableLayoutPanel t, int row, string label, Control control) { t.Controls.Add(new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left }, 0, row); control.Dock = DockStyle.Fill; control.Margin = new Padding(0, 3, 0, 3); t.Controls.Add(control, 1, row); }
    void Scan()
    {
        maps = Directory.Exists(source.Text) ? Directory.EnumerateFiles(source.Text, "*.sds", SearchOption.TopDirectoryOnly).OrderBy(Path.GetFileName).ToList() : [];
        map.Items.Clear(); map.Items.AddRange(maps.Select(Path.GetFileName).Cast<object>().ToArray()); if (map.Items.Count > 0) map.SelectedIndex = 0;
        Log($"Found {maps.Count} .sds map packages in {source.Text}"); status.Text = maps.Count == 0 ? "No .sds maps found." : "Select a map and inspect it.";
    }
    MapProbe Probe(string path)
    {
        using var stream = File.OpenRead(path); Span<byte> header = stackalloc byte[8]; stream.ReadExactly(header);
        var magic = Convert.ToHexString(header); var name = Path.GetFileNameWithoutExtension(path); var companions = Directory.EnumerateFiles(Path.GetDirectoryName(path)!, name + "*", SearchOption.TopDirectoryOnly).Select(Path.GetFileName).Where(n => n is not null).Cast<string>().ToArray();
        var bytes = File.ReadAllBytes(path); var strings = ExtractStrings(bytes); var classes = strings.Where(IsClassName).GroupBy(s => s, StringComparer.OrdinalIgnoreCase).ToDictionary(g => g.Key, g => g.Count(), StringComparer.OrdinalIgnoreCase);
        return new(path, stream.Length, name.EndsWith("_VS", StringComparison.OrdinalIgnoreCase) ? "Versus" : "Offline", magic, magic.StartsWith("C1832A9E", StringComparison.OrdinalIgnoreCase) ? "Recognized SCDA compiled map container; object inventory extracted; geometry reconstruction is the next stage." : "Unknown map container.", companions, classes, strings.Where(s => s.Contains("Actor", StringComparison.OrdinalIgnoreCase) || s.Contains("Mesh", StringComparison.OrdinalIgnoreCase) || s.Contains("Brush", StringComparison.OrdinalIgnoreCase)).Take(500).ToArray());
    }
    static IEnumerable<string> ExtractStrings(byte[] bytes)
    {
        var current = new StringBuilder();
        foreach (var b in bytes)
        {
            if (b is >= 32 and <= 126) current.Append((char)b);
            else { if (current.Length >= 4) yield return current.ToString(); current.Clear(); }
        }
        if (current.Length >= 4) yield return current.ToString();
    }
    static bool IsClassName(string value) => value is "Actor" or "Brush" or "StaticMesh" or "StaticMeshActor" or "Texture" or "Light" or "Trigger" or "Mover" || value.EndsWith("Actor", StringComparison.OrdinalIgnoreCase);
    void Inspect()
    {
        if (map.SelectedIndex < 0) { Scan(); if (map.SelectedIndex < 0) return; }
        var p = Probe(maps[map.SelectedIndex]); Log(JsonSerializer.Serialize(p, new JsonSerializerOptions { WriteIndented = true })); status.Text = p.Status;
    }
    void Prepare()
    {
        if (map.SelectedIndex < 0) { Inspect(); return; }
        var p = Probe(maps[map.SelectedIndex]); var folder = Path.Combine(output.Text, Path.GetFileNameWithoutExtension(p.File) + "_SCCT"); Directory.CreateDirectory(folder);
        File.WriteAllText(Path.Combine(folder, "source-map.json"), JsonSerializer.Serialize(p, new JsonSerializerOptions { WriteIndented = true }));
        File.WriteAllText(Path.Combine(folder, "conversion-status.md"), $"# {Path.GetFileNameWithoutExtension(p.File)}\n\nSCDA container: `{p.Magic}`\n\nStatus: {p.Status}\n\nCompanion files:\n" + string.Join("\n", p.CompanionFiles.Select(f => "- " + f)));
        Log("Prepared diagnostic workspace: " + folder); status.Text = "Diagnostic workspace prepared; extraction work remains.";
    }
    void Log(string text) { log.AppendText(text + Environment.NewLine); log.SelectionStart = log.TextLength; log.ScrollToCaret(); }
}
