#pragma once
#include <windows.h>
#include <string>

// English glosses for French asset names in the browsers: "Porte_Metal01
// (door metal 01)" and searches that match either language. Assets are
// never renamed. The dictionary is built in (tools/asset_names/fr-en.txt) and
// extended by System\ReloadedEditor\Translations\fr-en.txt; the
// "Show English for French asset names" option in RE+ Options turns the
// glosses on and off ([AssetNames] ShowEnglish in Reloaded_Editor.ini).
namespace AssetNameGloss
{
// Loads the dictionaries and hooks the Texture Browser's labels, caption and
// Filter box.
void Initialize();

bool Enabled();
// Saves the option and repaints the attached lists.
void SetEnabled(bool enabled);

// The English for an asset name, "" when none or when the option is off.
std::string GlossFor(const char* name);
// "Porte_Metal01 (door metal 01)", or the name as it is.
std::string LabelFor(const char* name);
// A search box's test: the query against the name or (with the option on)
// its English. Every space-separated word of the query must match.
bool MatchesQuery(const char* name, const char* query);

// Draws each row's English after its text in a ListBox, ListView, TreeView
// or ComboBox (its drop-down list and its face), without changing the items:
// the browsers keep reading the original names. Safe to call again.
void AttachList(HWND control);
// AttachList for every list in a browser window, now and as they appear.
void AttachBrowser(HWND window);
}
