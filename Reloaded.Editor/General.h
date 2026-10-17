#pragma once

namespace ViewportKeys { struct Chord; }

class General
{
public:
	static void Initialize();
	// Brush > Rebuild Builder Brush as Default Cube; throws when the editor refuses it.
	static void RebuildBuilderBrushAsDefaultCube();
	// Edit > Duplicate (ACTOR DUPLICATE), for Ctrl+D in a viewport.
	static void DuplicateSelection();
	// A key press a viewport saw, which the Ctrl+D and J key-ups go by.
	static void ViewportKeyPressed(const ViewportKeys::Chord& press);
};
