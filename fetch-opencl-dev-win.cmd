REM call as fetch-opencl-dev-win x86|x86_64

git clone https://github.com/KhronosGroup/OpenCL-Headers
move OpenCL-Headers/opencl22 include

mkdir lib\%1
curl -L -o lib/%1/libOpenCL.a https://github.com/AMD-FirePro/SDK/raw/master/external/opencl-1.2/lib/%1/libOpenCL.a -o lib/%1/OpenCL.lib https://github.com/AMD-FirePro/SDK/raw/master/external/opencl-1.2/lib/%1/OpenCL.lib -o OpenCL.dll https://github.com/AMD-FirePro/SDK/raw/master/external/opencl-1.2/bin/%1/OpenCL.dll
