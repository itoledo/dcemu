# CMakeLists.txt -- compilacion de dcemu con MSVC (Visual Studio 2026).
#
# dcemu.vcproj es formato de Visual Studio 2003 y referencia sh4.c, el nucleo
# viejo. Visual Studio no convierte proyectos anteriores a 2010, asi que el build
# nuevo va por aqui: VS abre esta carpeta directamente, sin generar solucion.
#
# Ver docs/msvc-build-plan.md. Los makefiles siguen siendo la ruta de MinGW/Linux.

cmake_minimum_required(VERSION 3.21)

project(dcemu C CXX)

if(NOT WIN32)
	message(FATAL_ERROR "Este CMakeLists es la ruta de Windows/MSVC. En Linux usa Makefile.linux.")
endif()

enable_language(RC)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)

# El primer hito deja fuera la ventana de log (guichan) y el backend libcdio
# (bin/cue y lectora fisica). Ver fase 5 del plan.
option(DCEMU_USE_GUICHAN "Compilar la ventana de log con guichan" OFF)
option(DCEMU_USE_LIBCDIO "Compilar el backend libcdio (bin/cue, lectora fisica)" OFF)
option(DCEMU_CONSOLE "Dejar la consola abierta para ver los mensajes a stderr" ON)
option(DCEMU_BUILD_TESTS "Compilar las pruebas unitarias de opcodes (tests/)" ON)

# ---------------------------------------------------------------------------
# SDL 1.2, via sdl12-compat
#
# SDL 1.2.15 original ya no compila limpio; sdl12-compat implementa la misma API
# sobre SDL2 y publica un paquete de desarrollo para MSVC con cabeceras, .lib y
# las DLL necesarias. Se descarga en el directorio de compilacion.
# ---------------------------------------------------------------------------

set(SDL12_VERSION "1.2.76")
set(SDL12_SHA256 "ee9da73c003cabcdefd18edf4a5126604602b381429b3b15684fb8c576afb008")

set(SDL12_DIR "" CACHE PATH "Directorio de sdl12-compat-devel ya descomprimido (vacio: se descarga)")

if(SDL12_DIR STREQUAL "")
	set(_sdl12_zip "${CMAKE_BINARY_DIR}/deps/sdl12-compat-devel-${SDL12_VERSION}-VC.zip")
	set(_sdl12_root "${CMAKE_BINARY_DIR}/deps/sdl12-compat-${SDL12_VERSION}")

	if(NOT EXISTS "${_sdl12_root}/include/SDL.h")
		message(STATUS "Descargando sdl12-compat ${SDL12_VERSION}")
		file(DOWNLOAD
			"https://github.com/libsdl-org/sdl12-compat/releases/download/release-${SDL12_VERSION}/sdl12-compat-devel-${SDL12_VERSION}-VC.zip"
			"${_sdl12_zip}"
			EXPECTED_HASH SHA256=${SDL12_SHA256}
			SHOW_PROGRESS)
		file(ARCHIVE_EXTRACT INPUT "${_sdl12_zip}" DESTINATION "${CMAKE_BINARY_DIR}/deps")
	endif()

	set(SDL12_DIR "${_sdl12_root}")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(SDL12_LIBDIR "${SDL12_DIR}/lib/x64")
else()
	set(SDL12_LIBDIR "${SDL12_DIR}/lib/x86")
endif()

# El paquete deja las cabeceras planas (include/SDL.h) pero el codigo incluye
# <SDL/SDL.h>, como en el arbol de MinGW. Se replican bajo un subdirectorio SDL.
set(SDL12_STAGED_INCLUDE "${CMAKE_BINARY_DIR}/deps/include")
file(COPY "${SDL12_DIR}/include/" DESTINATION "${SDL12_STAGED_INCLUDE}/SDL")

# ---------------------------------------------------------------------------
# Fuentes
#
# La lista sale de Makefile.linux, que es la unica al dia (usa sh4emu.c, no
# sh4.c). glops.c ya no participa del render pero se sigue enlazando.
# ---------------------------------------------------------------------------

set(DCEMU_SOURCES
	arith.c
	BFont.c
	branch.c
	dcopcodes.c
	debug.c
	excepciones.c
	floatcontrol.c
	floatgraph.c
	floatsimple.c
	gdrom.c
	glops.c
	graficos.c
	intc.c
	iso.c
	iso9660_min.c
	cdi.c
	log.c
	logic.c
	main.c
	mem.c
	mmu.c
	mov.c
	opciones.c
	opcodes.c
	reg.c
	scramble.c
	sh4emu.c
	shift.c
	simdx86_stub.c
	sistema.c
	tmu.c
	wdt.c
	syscontrol.c
	traza.c
	gui.cpp
	dcemu_private.rc
)

add_executable(dcemu ${DCEMU_SOURCES})

target_include_directories(dcemu PRIVATE
	${CMAKE_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/include
	${SDL12_STAGED_INCLUDE}
)

# POSX no se define: pese al nombre, ambos makefiles lo definen tambien en
# Windows, y eso hace que main.h tome la rama POSIX, que incluye <unistd.h>.
target_compile_definitions(dcemu PRIVATE
	WIN32
	_CRT_SECURE_NO_WARNINGS
	X86_OPT
)

if(DCEMU_USE_GUICHAN)
	target_compile_definitions(dcemu PRIVATE USE_GUICHAN)
endif()

if(DCEMU_USE_LIBCDIO)
	target_compile_definitions(dcemu PRIVATE USE_LIBCDIO)
endif()

# glu32 y winmm estan en los makefiles pero no se usan: la unica llamada a GLU
# esta comentada (graficos.c) y no hay ninguna a winmm.
target_link_libraries(dcemu PRIVATE
	"${SDL12_LIBDIR}/SDL.lib"
	"${SDL12_LIBDIR}/SDLmain.lib"
	opengl32
)

# SDL_main.h hace "#define main SDL_main", asi que el punto de entrada real lo
# aporta SDLmain.lib como WinMain. Con subsistema de consola hay que decirselo
# al linker explicitamente; a cambio, los fprintf(stderr) quedan a la vista.
if(DCEMU_CONSOLE)
	target_link_options(dcemu PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:WinMainCRTStartup)
else()
	set_target_properties(dcemu PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# MSVC no tiene equivalente a -fno-strict-aliasing, que ambos makefiles pasan. En
# la practica MSVC no hace analisis de aliasing agresivo, pero el type-punning de
# mem.c y floatsimple.c es justo lo que podria romperse en silencio: conviene
# validar primero el binario sin optimizar.
target_compile_options(dcemu PRIVATE
	/W3
	/wd4996		# funciones "inseguras" de la CRT
)

# ---------------------------------------------------------------------------
# Archivos de apoyo junto al ejecutable
#
# inicializar_logs() aborta el arranque si no puede crear logs/*.txt, y las
# fuentes se abren por ruta relativa al directorio de trabajo.
# ---------------------------------------------------------------------------

set(DCEMU_RUNTIME_FILES
	font.png			# BFont, main.c
	font.bmp			# BFont, debug.c
	fixedfont.bmp		# guichan
)

add_custom_command(TARGET dcemu POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:dcemu>/logs"
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:dcemu>/bios"
	VERBATIM)

foreach(_f ${DCEMU_RUNTIME_FILES})
	add_custom_command(TARGET dcemu POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_SOURCE_DIR}/${_f}" "$<TARGET_FILE_DIR:dcemu>/"
		VERBATIM)
endforeach()

# La cadena completa: SDL.dll es sdl12-compat, que carga SDL2.dll (que a su vez
# es sdl2-compat, no SDL2 de verdad), que carga SDL3.dll. Si falta alguna, el
# proceso muere antes del main con STATUS_DLL_INIT_FAILED (0xC0000142).
foreach(_dll SDL.dll SDL2.dll SDL3.dll)
	add_custom_command(TARGET dcemu POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${SDL12_LIBDIR}/${_dll}" "$<TARGET_FILE_DIR:dcemu>/"
		VERBATIM)
endforeach()

# ---------------------------------------------------------------------------
# Pruebas unitarias
#
# Enlazan los handlers de opcodes sin SDL ni OpenGL. Ver tests/README.md.
#   cmake --build build --target dcemu_tests
#   ctest --test-dir build -C Debug --output-on-failure
# ---------------------------------------------------------------------------

if(DCEMU_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
