/* List of OpenCL context properties used to interoperate with a different API */

#ifndef CTX_PROP
#define CTX_PROP

/* cl_khr_gl_sharing */
#define CL_GL_CONTEXT_KHR			0x2008
#define CL_EGL_DISPLAY_KHR			0x2009
#define CL_GLX_DISPLAY_KHR			0x200A
#define CL_WGL_HDC_KHR				0x200B
#define CL_CGL_SHAREGROUP_KHR			0x200C

/* cl_khr_dx9_media_sharing */
#define CL_CONTEXT_ADAPTER_D3D9_KHR		0x2025
#define CL_CONTEXT_ADAPTER_D3D9EX_KHR		0x2026
#define CL_CONTEXT_ADAPTER_DXVA_KHR		0x2027

/* cl_khr_d3d10_sharing */
#define CL_CONTEXT_D3D10_DEVICE_KHR		0x4014

/* cl_khr_d3d11_sharing */
#define CL_CONTEXT_D3D11_DEVICE_KHR		0x401D

/* cl_intel_dx9_media_sharing */
#define CL_CONTEXT_D3D9_DEVICE_INTEL		0x4026
#define CL_CONTEXT_D3D9EX_DEVICE_INTEL		0x4072
#define CL_CONTEXT_DXVA_DEVICE_INTEL		0x4073

/* cl_intel_va_api_media_sharing */
#define CL_CONTEXT_VA_API_DISPLAY_INTEL		0x4097

#endif
