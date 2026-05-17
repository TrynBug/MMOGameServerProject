# 빌드에 사용할 toolset을 v143으로 지정
set(VCPKG_PLATFORM_TOOLSET v143)

# 타겟 아키텍처를 x64로 지정
set(VCPKG_TARGET_ARCHITECTURE x64)

# C/C++ 런타임(CRT)은 시스템의 DLL을 공유하여 사용하도록 설정
# Visual Studio 프로젝트 설정의 /MD (Multi-threaded DLL) 옵션에 해당함
set(VCPKG_CRT_LINKAGE dynamic)

# 라이브러리를 .lib(정적 라이브러리) 형태로 빌드함
set(VCPKG_LIBRARY_LINKAGE static)

# SDK 버전 지정
set(VCPKG_WINDOWS_SDK_VERSION 10.0.26100.0)