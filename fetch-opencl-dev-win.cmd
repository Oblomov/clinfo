REM call as fetch-opencl-dev-win x86|x86_64|x64

git clone https://github.com/KhronosGroup/OpenCL-Headers include

set sub=%1

if /i "%sub%" == "x64" set sub=x86_64

mkdir lib\%sub%
curl -L -o lib/%sub%/libOpenCL.a https://github.com/AMD-FirePro/SDK/raw/master/external/opencl-1.2/lib/%sub%/libOpenCL.a -o lib/%sub%/OpenCL.lib https://github.com/AMD-FirePro/SDK/raw/master/external/opencl-1.2/lib/%sub%/OpenCL.lib -o OpenCL.dll https://github.com/AMD-FirePro/SDK/raw/master/external/opencl-1.2/bin/%sub%/OpenCL.dll
