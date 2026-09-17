# SCDA Map Converter

This is the initial Double Agent conversion frontend. It scans the PC `Packages/_Common/MapsPC` folder, identifies compiled `.sds` maps and their companion files, and prepares a separate diagnostic workspace for each selected map.

The installed files use the `C1 83 2A 9E` SCDA compiled-map container. Geometry, materials, collision and gameplay extraction are deliberately reported as unsupported until the container schema is reverse-engineered; the tool never modifies the source installation.

Build with `dotnet build SCDAMapConverter.csproj -c Release`.
