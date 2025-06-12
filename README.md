# My Piccolo Engine

**[Piccolo Engine](https://github.com/BoomingTech/Piccolo)** is a tiny game engine used for the [GAMES104](https://games104.boomingtech.com) course.

TODO

先构建好环境，作业还一点没做。

- 一些可参考的其它引擎
  1. Lumos Engine https://github.com/jmorton06/Lumos?tab=readme-ov-file#screenshots
  2. Noob Renderer https://github.com/shuaibo919/NoobRenderer?tab=readme-ov-file
  3. Forker Renderer https://github.com/forkercat/ForkerRenderer
  4. Carbon Render https://github.com/carbonsunsu/CarbonRender
  5. Filament Engine https://github.com/google/filament
  7. Anne Engine https://github.com/SZU-WenjieHuang/Anne-Engine
  8. Explosion Engine https://github.com/ExplosionEngine/Explosion

## Build (on Windows)
Or you can use the following command to generate the **Visual Studio** project firstly, then open the solution in the build directory and build it manually.
```
cmake -S . -B build
```

To generate compilation database:
``` powershell
cmake -DCMAKE_TRY_COMPILE_TARGET_TYPE="STATIC_LIBRARY" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S . -B compile_db_temp -G "Unix Makefiles"
copy compile_db_temp\compile_commands.json .
```

Note:
1. Please clean the build directory before regenerating the solution. We've encountered building problems in regenerating directly with previous CMakeCache.
2. Physics Debug Renderer will run when you start PiccoloEditor. We've synced the camera position between both scenes. But the initial camera mode in Physics Debug Renderer is wrong. Scrolling down the mouse wheel once will change the camera of Physics Debug Renderer to the correct mode.
