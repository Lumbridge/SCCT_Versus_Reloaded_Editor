d3d8to9
=======

[![GitHub Actions Status](https://github.com/crosire/d3d8to9/actions/workflows/build.yml/badge.svg)](https://github.com/crosire/d3d8to9/actions/workflows/build.yml)
[![AppVeyor Status](https://ci.appveyor.com/api/projects/status/aqupdda60ixgenyd?svg=true)](https://ci.appveyor.com/project/crosire/d3d8to9)

d3d8to9 translates Direct3D 8 API calls and low-level shaders to Direct3D 9. This lets Direct3D 8 games use Direct3D 9 modding tools, including [ReShade](http://reshade.me).

d3d8to9 translates the calls as requested by the game. Windows settings and GPU drivers can still produce different results from native Direct3D 8. For example, a game may request VSync but only get it when running through d3d8to9, making the frame rate appear lower. To override settings such as VSync, use [dxwrapper](https://github.com/elishacloud/dxwrapper), which adds configuration options around d3d8to9.

## Building

You'll need Visual Studio 2013 or higher to build d3d8to9. Install the old standalone DirectX end-user runtime for the D3DX libraries used to disassemble and assemble shaders.

Source files:

|File                                                      |Description                                                                      |
|----------------------------------------------------------|---------------------------------------------------------------------------------|
|[d3d8to9.cpp](source/d3d8to9.cpp)                         | Definition of the main D3D8 entry point `Direct3DCreate8`                       |
|[d3d8to9_base.cpp](source/d3d8to9_base.cpp)               | Implementation of the `IDirect3D8` interface, including device creation         |
|[d3d8to9_device.cpp](source/d3d8to9_device.cpp)           | Implementation of the `IDirect3DDevice8` interface, including shader conversion |
|[d3d8types.hpp](source/d3d8types.hpp)                     | Declaration of all used D3D8 types one would otherwise find in d3d8.h           |
|[interface_query.hpp](source/interface_query.hpp)         | Table to map D3D9 interface addresses to their matching D3D8 implementations    |

## Contributing

Submit changes through GitHub [pull requests](https://help.github.com/articles/using-pull-requests/).

Thanks to the [contributors](https://github.com/crosire/d3d8to9/graphs/contributors) who improved compatibility, especially [elishacloud](https://github.com/elishacloud).

## License

All source code in this repository is licensed under a [BSD 2-clause license](LICENSE.md).
