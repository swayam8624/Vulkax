from pathlib import Path
import runpy

if not Path('include/vulkax/compute/gpu_dem.hpp').exists():
    p=Path('scripts/apply_gpu_dem_phase.py')
    if p.exists(): runpy.run_path(str(p),run_name='__main__')
if not Path('include/vulkax/execution/problem_runner.hpp').exists():
    p=Path('scripts/apply_problem_runner_migration.py')
    if p.exists(): runpy.run_path(str(p),run_name='__main__')
for stale in ['scripts/apply_gpu_dem_phase.py','.github/workflows/wip-gpu-dem-phase.yml','scripts/apply_verticals_phase.py','.github/workflows/wip-verticals-phase.yml','scripts/apply_problem_runner_migration.py','.github/workflows/wip-problem-runner-migration.yml']:
    p=Path(stale)
    if p.exists():p.unlink()

Path('include/vulkax/backend/opengl_probe.hpp').write_text(r'''#pragma once
#include "vulkax/backend/backend.hpp"
namespace vulkax::backend { [[nodiscard]] BackendCapabilities probeOpenGLBackend(); }
''')

Path('src/backend/opengl_probe_linux.cpp').write_text(r'''#include "vulkax/backend/opengl_probe.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
namespace vulkax::backend { namespace {
std::pair<int,int> version(const char*s){if(!s)return{0,0};while(*s&&!std::isdigit((unsigned char)*s))++s;int a=0,b=0;if(*s){a=*s-'0';const char*dot=std::strchr(s,'.');if(dot&&std::isdigit((unsigned char)dot[1]))b=dot[1]-'0';}return{a,b};}
bool software(const std::string&s){std::string l=s;std::transform(l.begin(),l.end(),l.begin(),[](unsigned char c){return(char)std::tolower(c);});return l.find("llvmpipe")!=std::string::npos||l.find("softpipe")!=std::string::npos||l.find("software")!=std::string::npos;}
}
BackendCapabilities probeOpenGLBackend(){BackendCapabilities out;out.kind=BackendKind::OpenGL;out.driverQuality=0.55;EGLDisplay display=EGL_NO_DISPLAY;using PFN=EGLDisplay(*)(EGLenum,void*,const EGLint*);auto getPlatform=(PFN)eglGetProcAddress("eglGetPlatformDisplayEXT");#ifdef EGL_PLATFORM_SURFACELESS_MESA
if(getPlatform)display=getPlatform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,nullptr);
#endif
if(display==EGL_NO_DISPLAY)display=eglGetDisplay(EGL_DEFAULT_DISPLAY);if(display==EGL_NO_DISPLAY)return out;EGLint maj=0,min=0;if(!eglInitialize(display,&maj,&min))return out;if(!eglBindAPI(EGL_OPENGL_API)){eglTerminate(display);return out;}EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};EGLConfig config{};EGLint n=0;if(!eglChooseConfig(display,attrs,&config,1,&n)||n<1){eglTerminate(display);return out;}EGLint pbufAttrs[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};EGLSurface surface=eglCreatePbufferSurface(display,config,pbufAttrs);if(surface==EGL_NO_SURFACE){eglTerminate(display);return out;}EGLContext context=EGL_NO_CONTEXT;EGLint ctx43[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE};context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctx43);if(context==EGL_NO_CONTEXT)context=eglCreateContext(display,config,EGL_NO_CONTEXT,nullptr);if(context==EGL_NO_CONTEXT||!eglMakeCurrent(display,surface,surface,context)){if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);return out;}const char*renderer=(const char*)glGetString(GL_RENDERER);const char*ver=(const char*)glGetString(GL_VERSION);out.available=renderer&&ver;out.deviceName=renderer?std::string(renderer):"OpenGL";auto [a,b]=version(ver);out.dedicatedGpu=!software(out.deviceName);out.driverQuality=out.dedicatedGpu?0.62:0.38;if(a>4||(a==4&&b>=3)){out.features.push_back(Feature::Compute);out.features.push_back(Feature::StorageBuffers);out.features.push_back(Feature::Atomics);}eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);return out;}
} // namespace vulkax::backend
'''.replace(';#ifdef',';\n#ifdef').replace('\nif(getPlatform)','\n    if(getPlatform)').replace('\n#endif','\n#endif'))

Path('src/backend/opengl_probe_macos.mm').write_text(r'''#include "vulkax/backend/opengl_probe.hpp"
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>
#include <string>
namespace vulkax::backend {
BackendCapabilities probeOpenGLBackend(){BackendCapabilities out;out.kind=BackendKind::OpenGL;out.driverQuality=0.45;CGLPixelFormatAttribute attrs[]={kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,kCGLPFAAccelerated,(CGLPixelFormatAttribute)0};CGLPixelFormatObj pf=nullptr;GLint np=0;if(CGLChoosePixelFormat(attrs,&pf,&np)!=kCGLNoError||!pf)return out;CGLContextObj ctx=nullptr;if(CGLCreateContext(pf,nullptr,&ctx)!=kCGLNoError||!ctx){CGLDestroyPixelFormat(pf);return out;}if(CGLSetCurrentContext(ctx)==kCGLNoError){const char*r=(const char*)glGetString(GL_RENDERER);const char*v=(const char*)glGetString(GL_VERSION);out.available=r&&v;out.deviceName=r?std::string(r):"Apple OpenGL";out.dedicatedGpu=false;/* macOS OpenGL tops out below compute-shader/SSBO core support; advertise graphics only. */}CGLSetCurrentContext(nullptr);CGLDestroyContext(ctx);CGLDestroyPixelFormat(pf);return out;}
}
''')

Path('src/backend/opengl_probe_windows.cpp').write_text(r'''#include "vulkax/backend/opengl_probe.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
namespace vulkax::backend { namespace { LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}std::pair<int,int>ver(const char*s){if(!s)return{0,0};while(*s&&!std::isdigit((unsigned char)*s))++s;int a=*s?*s-'0':0,b=0;auto*d=std::strchr(s,'.');if(d&&std::isdigit((unsigned char)d[1]))b=d[1]-'0';return{a,b};}}
BackendCapabilities probeOpenGLBackend(){BackendCapabilities out;out.kind=BackendKind::OpenGL;out.driverQuality=0.5;HINSTANCE inst=GetModuleHandleW(nullptr);const wchar_t*cls=L"VulkaxOpenGLProbe";WNDCLASSW wc{};wc.style=CS_OWNDC;wc.lpfnWndProc=proc;wc.hInstance=inst;wc.lpszClassName=cls;RegisterClassW(&wc);HWND wnd=CreateWindowW(cls,L"",WS_OVERLAPPED,0,0,1,1,nullptr,nullptr,inst,nullptr);if(!wnd)return out;HDC dc=GetDC(wnd);PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=24;int pf=ChoosePixelFormat(dc,&pfd);if(!pf||!SetPixelFormat(dc,pf,&pfd)){ReleaseDC(wnd,dc);DestroyWindow(wnd);return out;}HGLRC rc=wglCreateContext(dc);if(rc&&wglMakeCurrent(dc,rc)){const char*r=(const char*)glGetString(GL_RENDERER);const char*v=(const char*)glGetString(GL_VERSION);out.available=r&&v;out.deviceName=r?std::string(r):"Windows OpenGL";auto[a,b]=ver(v);if(a>4||(a==4&&b>=3)){out.features={Feature::Compute,Feature::StorageBuffers,Feature::Atomics};}out.dedicatedGpu=true;}wglMakeCurrent(nullptr,nullptr);if(rc)wglDeleteContext(rc);ReleaseDC(wnd,dc);DestroyWindow(wnd);UnregisterClassW(cls,inst);return out;}
}
''')

Path('tests/opengl_probe_tests.cpp').write_text(r'''#include "vulkax/backend/opengl_probe.hpp"
#include <cassert>
int main(){const auto gl=vulkax::backend::probeOpenGLBackend();assert(gl.kind==vulkax::backend::BackendKind::OpenGL);if(gl.available)assert(!gl.deviceName.empty());return 0;}
''')

# Wire the probe into aggregate discovery without assuming exact existing function formatting.
p=Path('src/backend/probe.cpp');t=p.read_text()
if 'opengl_probe.hpp' not in t:t=t.replace('#include "vulkax/backend/probe.hpp"','#include "vulkax/backend/probe.hpp"\n#include "vulkax/backend/opengl_probe.hpp"')
if 'probeOpenGLBackend()' not in t:
    marker='return result;'
    idx=t.rfind(marker)
    if idx<0: raise RuntimeError('probe.cpp return marker missing')
    t=t[:idx]+'const auto openGL = probeOpenGLBackend();\n    if (openGL.available) result.push_back(openGL);\n    '+t[idx:]
p.write_text(t)

p=Path('CMakeLists.txt');t=p.read_text();t=t.replace('project(Vulkax VERSION 0.20.0 LANGUAGES CXX)','project(Vulkax VERSION 0.21.0 LANGUAGES CXX)').replace('project(Vulkax VERSION 0.19.0 LANGUAGES CXX)','project(Vulkax VERSION 0.21.0 LANGUAGES CXX)').replace('project(Vulkax VERSION 0.18.0 LANGUAGES CXX)','project(Vulkax VERSION 0.21.0 LANGUAGES CXX)')
if 'opengl_probe_linux.cpp' not in t:
    insertion='''\nif(APPLE)\n    target_sources(vulkax_core PRIVATE src/backend/opengl_probe_macos.mm)\n    find_library(VULKAX_OPENGL_FRAMEWORK OpenGL REQUIRED)\n    target_link_libraries(vulkax_core PRIVATE ${VULKAX_OPENGL_FRAMEWORK})\nelseif(WIN32)\n    target_sources(vulkax_core PRIVATE src/backend/opengl_probe_windows.cpp)\n    target_link_libraries(vulkax_core PRIVATE opengl32)\nelse()\n    target_sources(vulkax_core PRIVATE src/backend/opengl_probe_linux.cpp)\n    find_library(VULKAX_EGL_LIBRARY EGL REQUIRED)\n    find_library(VULKAX_OPENGL_LIBRARY GL REQUIRED)\n    target_link_libraries(vulkax_core PRIVATE ${VULKAX_EGL_LIBRARY} ${VULKAX_OPENGL_LIBRARY})\nendif()\n'''
    pos=t.find('\nif(MSVC)')
    if pos<0:raise RuntimeError('CMake compiler-options anchor missing')
    t=t[:pos]+insertion+t[pos:]
if 'opengl_probe)' not in t:t=t.replace('gpu_dem)','gpu_dem opengl_probe)')
p.write_text(t)

# Ensure Linux has EGL/GL development libraries; Windows/macOS use platform frameworks.
ci=Path('.github/workflows/ci.yml').read_text()
ci=ci.replace('sudo apt-get install -y libvulkan-dev mesa-vulkan-drivers glslang-tools','sudo apt-get install -y libvulkan-dev mesa-vulkan-drivers glslang-tools libgl1-mesa-dev libegl1-mesa-dev')
Path('.github/workflows/ci.yml').write_text(ci)
