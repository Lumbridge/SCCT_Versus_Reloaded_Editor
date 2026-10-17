#pragma once

class General
{
public:
	static void Initialize();
	// Brush > Rebuild Builder Brush as Default Cube; throws when the editor refuses it.
	static void RebuildBuilderBrushAsDefaultCube();
	// Edit > Duplicate (ACTOR DUPLICATE), for Ctrl+D in a viewport.
	static void DuplicateSelection();
};
