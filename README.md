# OpenGL-Sandbox
My sandbox for experimenting with 3D graphics programming techniques, such as
- Meshlet Culling
- Two Pass Occlusion Culling
- Cascaded Shadow Maps
- Reflective Shadow Maps

## Building
Build with Visual Studio. Simply open the solution (.sln) file and compile the project!

## Usage
Requires a GPU with GL_NV_mesh_shader support.
The assets folder must contain a "Bistro1.glb". The content of this file doesn't matter, provided that there is some opaque geometry and some alpha-blended geometry.

Use WASDEQ to move. Rotate with the arrow keys. 

## Credits
- OpenGL & various extensions
- SDL2
- Glad
- glm
- fastgltf
- Dear ImGui
- stb
- meshoptimizer
- simdjson

## License
[MIT](https://choosealicense.com/licenses/mit/)
