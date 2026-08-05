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

# Las dos formas de despachar del interprete. Ver opcodes.h: la expandida
# indexa 512 KB de punteros con la palabra de instruccion cruda, la compacta
# 128 KB de indices de 16 bits mas un arreglo de 240 punteros. Es una opcion y
# no un #define en options.h porque lo que decide es correr los dos binarios en
# la misma tanda, y para eso tienen que poder existir a la vez.
option(DCEMU_DESPACHO_COMPACTO "Despachar por indice de 16 bits en vez de por puntero" OFF)

# La sonda de forma de ejecucion del guest: longitud de los bloques basicos,
# cuantos distintos hay y con que reincidencia. Es lo que decide si un cache de
# bloques o un recompilador pueden pagar (docs/interprete-plan.md).
#
# **Es la unica sonda del arbol que no vive en el binario normal**, y la
# excepcion esta medida: el gancho es una rama por despacho y cuesta 4,4 % en
# Crazy Taxi -- 1,50x contra 1,55-1,57x, alternando los dos binarios en una
# tanda. Puede compilarse aparte sin romper nada porque lo que mide son cuentas
# de bloques **del guest**, iguales en cualquier compilacion; la regla de
# mantener las sondas en un solo binario existe para las medidas de tiempo, que
# es donde la disposicion del binario entra como variable. Ver perf.h.
option(DCEMU_FORMA "Compilar la sonda de forma de ejecucion (cuesta 4,4 %)" OFF)

# El cache de bloques predecodificados (bloques.c). Es el paso 1.3 del plan y
# **ya dio su respuesta: no sirve**. Reemplaza la busqueda de la palabra y la de
# la tabla de 65536 punteros por un recorrido secuencial, y en el banco de Crazy
# Taxi en juego da 125 634 / 125 353 ms contra 125 148 / 125 234 del camino de
# siempre, o sea ruido; en los menus pierde 3,3 %.
#
# Se queda en el arbol, apagado, por lo mismo que se quedo --hilos: el codigo es
# correcto, esta medido y la proxima persona que crea que el despacho es el
# costo puede volver a correr el A/B sin reconstruir la idea. Compilado fuera no
# cuesta nada; compilado adentro, DCEMU_SONDA_BLOQUES=1 lo enciende y el A/B se
# hace contra el mismo binario con la variable en 0.
option(DCEMU_BLOQUES "Compilar el cache de bloques predecodificados (medido: no sirve)" OFF)

# Los diez manejadores mas frecuentes en linea dentro del bucle, sin llamada
# indirecta. Ver despacho_inline() en main.c: es lo unico del cuerpo de
# main_loop() que quedaba sin medir, y a la vez la optimizacion si rinde.
option(DCEMU_INLINE "Despachar en linea los manejadores mas frecuentes" OFF)

# Optimizacion de todo el programa (/GL al compilar, /LTCG al enlazar). Es la
# fase 1.2 de docs/rendimiento-plan.md y aqui importa mas de lo normal: este
# arbol tiene los bucles calientes en una unidad de traduccion y las funciones
# que llaman en otra --arm7.c llama a aica_fiq_pendiente() de aica.c en CADA
# instruccion del ARM, y main.c despacha a los manejadores de mov.c, arith.c,
# logic.c...--, asi que sin esto el compilador no puede inlinear ninguna.
#
# Es una opcion y no un flag fijo por la misma razon que el despacho compacto:
# lo que decide es alternar los dos binarios en la misma tanda, y para eso
# tienen que poder existir a la vez. Las barandas para validarlo son las de
# siempre --las 21 suites, SingleStepTests bit a bit, el barrido de 150 demos--.
#
# Viene en ON: medido en Virtua Tennis vale 6,6 % de la corrida y 14 % del ARM,
# con la salida identica byte a byte. Sigue siendo alternable con OFF, que es
# para lo que existe la opcion.
option(DCEMU_LTCG "Optimizacion de todo el programa (/GL + /LTCG) en Release" ON)

# Conjunto de instrucciones del anfitrion. Sin esto MSVC compila para SSE2, que
# es de 2001 -- y **medido, eso es lo que conviene**.
#
# /arch:AVX2 cuesta 2,1 % en Crazy Taxi: 106 619 ms contra 104 442, cuatro pares
# de cuatro en contra con las dos ordenaciones y rangos disjuntos. La conformidad
# no se rompe --113 191 ok / 0 fallan bit a bit, o sea que no hubo contraccion a
# FMA-- asi que no es un problema de correccion: sencillamente pierde.
#
# El motivo mas probable es el mismo que ya midio el experimento de inlinear
# manejadores (19 % de costo, docs/interprete-plan.md): **en este arbol el tamano
# del codigo caliente es un factor de primer orden**. Un interprete de SH-4 es
# trabajo entero escalar y llamadas indirectas -- no hay nada que vectorizar --
# y la codificacion VEX es mas larga, asi que el bucle engorda sin ganar nada.
#
# Queda como opcion, no como flag borrado, para que el A/B se pueda repetir sin
# rehacer el andamiaje. Y si alguna vez se agrega trabajo vectorizable de verdad
# --decodificadores de textura, el mezclador del AICA-- conviene medirlo por
# archivo y no encendiendolo para todo el arbol.
#
# **/fp:precise se mantiene y no es negociable**: fpu_aplicar_redondeo() toca el
# modo de redondeo del anfitrion en tiempo de ejecucion y la conformidad del
# nucleo se compara bit a bit contra SingleStepTests. /fp:fast rompe las dos.
#
# El otro precio de encenderlo seria portabilidad: un binario AVX2 no arranca en
# un procesador anterior a Haswell (2013).
set(DCEMU_ARCH "OFF" CACHE STRING
	"Conjunto de instrucciones: AVX2, AVX, SSE2 u OFF (lo que traiga el compilador). Medido: AVX2 cuesta 2,1 %")
set_property(CACHE DCEMU_ARCH PROPERTY STRINGS OFF SSE2 AVX AVX2)

# add_compile_options y no target_compile_options, y **aca arriba y no junto al
# resto de las banderas del target**: solo alcanza a los objetivos creados
# despues, y tienen que quedar adentro tanto dcemu como tests/, que compara
# resultados de coma flotante bit a bit contra los del binario que se mide. Si
# solo lo llevara uno de los dos, la baranda estaria validando otro codigo.
if(MSVC AND NOT DCEMU_ARCH STREQUAL "OFF")
	add_compile_options(/arch:${DCEMU_ARCH})
	message(STATUS "dcemu: /arch:${DCEMU_ARCH}")
endif()

# Optimizacion guiada por perfil. Tres estados, y el orden importa:
#
#   cmake -S . -B build -DDCEMU_PGO=GEN     # binario instrumentado
#   cmake --build build --config Release --target dcemu
#   herramientas\pgo.ps1 -Paso entrenar     # corre el banco fijo y junta los .pgc
#   cmake -S . -B build -DDCEMU_PGO=USE     # binario final, con el perfil puesto
#   cmake --build build --config Release --target dcemu
#
# Que resuelve. En este arbol **la disposicion del binario mueve la cifra tanto
# como una optimizacion**: agregar codigo a mmu.c que un guest sin MMU no
# ejecuta le costo un 8 % a Crazy Taxi, dos veces, y una vez se recupero entero
# revirtiendo un cambio que no ejecutaba nada. El orden de enlace es hoy quien
# decide donde cae el codigo caliente del interprete, y eso no es una variable
# que se pueda dejar suelta si lo que se quiere es medir optimizaciones.
#
# PGO la fija: ordena las funciones y las ramas por lo que hizo una corrida
# real, no por el orden en que estan los .obj. Ver docs/rendimiento-plan.md,
# fase 6, "Pero la regresion volvio".
#
# El entrenamiento **tiene que ser el banco fijo** --Crazy Taxi en juego, Virtua
# Tennis y DCDoom-- porque un perfil sacado de los menus ordena el codigo del
# menu. Y el binario instrumentado corre del orden de tres veces mas lento, asi
# que pgo.ps1 usa segundos emulados mas cortos: lo que importa es que pase por
# los mismos caminos, no cuantas veces.
set(DCEMU_PGO "OFF" CACHE STRING "Optimizacion guiada por perfil: OFF, GEN o USE")
set_property(CACHE DCEMU_PGO PROPERTY STRINGS OFF GEN USE)

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
	gdi.c
	mando.c
	log.c
	logic.c
	main.c
	mem.c
	mmu.c
	mov.c
	opciones.c
	opcodes.c
	perf.c
	hilo.c
	hilo_aica.c
	reg.c
	scramble.c
	sh4emu.c
	shift.c
	simdx86_stub.c
	sistema.c
	ta.c
	tmu.c
	ubc.c
	vram.c
	aica.c
	arm7.c
	audio.c
	g2dma.c
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

if(DCEMU_DESPACHO_COMPACTO)
	target_compile_definitions(dcemu PRIVATE DESPACHO_COMPACTO)
endif()

if(DCEMU_FORMA)
	target_compile_definitions(dcemu PRIVATE DCEMU_FORMA)
endif()

if(DCEMU_BLOQUES)
	target_compile_definitions(dcemu PRIVATE DCEMU_BLOQUES)
	target_sources(dcemu PRIVATE bloques.c)
endif()

if(DCEMU_INLINE)
	target_compile_definitions(dcemu PRIVATE DCEMU_INLINE)
endif()

# Release con simbolos. Los flags por omision de CMake para Release son
# /O2 /Ob2 /DNDEBUG, **sin /Zi**, asi que no deja PDB -- y sin PDB un perfil de
# muestreo atribuye cero muestras al emulador y las manda todas a
# ***unknown***. Paso: el primer perfil de Release salio vacio.
#
# /Zi no cambia el codigo generado. /DEBUG en el enlace si apaga /OPT:REF y
# /OPT:ICF, que Release trae puestos, asi que se reponen: el binario perfilado
# tiene que ser el mismo que se mide.
target_compile_options(dcemu PRIVATE $<$<CONFIG:Release>:/Zi>)
target_link_options(dcemu PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)

if(DCEMU_LTCG)
	target_compile_options(dcemu PRIVATE $<$<CONFIG:Release>:/GL>)
	target_link_options(dcemu PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

# PGO. Exige /GL, que es lo que hace LTCG: sin el, el enlazador no tiene el
# codigo intermedio sobre el que aplicar el perfil.
if(NOT DCEMU_PGO STREQUAL "OFF")
	if(NOT DCEMU_LTCG)
		message(FATAL_ERROR "DCEMU_PGO necesita DCEMU_LTCG=ON (/GL)")
	endif()

	# El .pgd vive fuera del directorio de build a proposito: sobrevive a un
	# borrado de build/ y a cambiar de GEN a USE, que es todo el punto de
	# entrenar una vez y compilar despues.
	set(DCEMU_PGD "${CMAKE_SOURCE_DIR}/build-pgo/dcemu.pgd")
	file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/build-pgo")

	if(DCEMU_PGO STREQUAL "GEN")
		target_link_options(dcemu PRIVATE
			$<$<CONFIG:Release>:/GENPROFILE:PGD=${DCEMU_PGD}>)
		message(STATUS "dcemu: PGO instrumentado, perfil a ${DCEMU_PGD}")
	elseif(DCEMU_PGO STREQUAL "USE")
		if(NOT EXISTS "${DCEMU_PGD}")
			message(FATAL_ERROR
				"DCEMU_PGO=USE pero no existe ${DCEMU_PGD}: falta entrenar")
		endif()

		target_link_options(dcemu PRIVATE
			$<$<CONFIG:Release>:/USEPROFILE:PGD=${DCEMU_PGD}>)
		message(STATUS "dcemu: PGO aplicado desde ${DCEMU_PGD}")
	else()
		message(FATAL_ERROR "DCEMU_PGO tiene que ser OFF, GEN o USE")
	endif()
endif()

# glu32 y winmm estan en los makefiles pero no se usan: la unica llamada a GLU
# esta comentada (graficos.c) y no hay ninguna a winmm.
target_link_libraries(dcemu PRIVATE
	"${SDL12_LIBDIR}/SDL.lib"
	"${SDL12_LIBDIR}/SDLmain.lib"
	opengl32
	# dbghelp: la pila simbolizada del informe de caida (traza.c). Release lleva
	# /Zi, asi que el PDB queda al lado del .exe y no hay nada mas que hacer.
	dbghelp
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
	font.bmp			# BFont, debug.c -- la vista F12
	fixedfont.bmp		# guichan, gui.cpp
)

add_custom_command(TARGET dcemu POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:dcemu>/logs"
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:dcemu>/bios"
	VERBATIM)

# Se copia lo que exista y se avisa de lo que no, en vez de romper la
# compilacion: `copy_if_different` sobre un archivo ausente devuelve error y se
# lleva el enlace entero por delante. Paso una vez -- font.bmp y fixedfont.bmp
# se borraron junto con los BMP de capturas -- y el sintoma, un MSB3073 con
# cuatro pantallas de eco de MSBuild, no se parece en nada a "falta un archivo".
foreach(_f ${DCEMU_RUNTIME_FILES})
	if(EXISTS "${CMAKE_SOURCE_DIR}/${_f}")
		add_custom_command(TARGET dcemu POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${CMAKE_SOURCE_DIR}/${_f}" "$<TARGET_FILE_DIR:dcemu>/"
			VERBATIM)
	else()
		message(WARNING "falta ${_f}: el emulador compila, pero lo que lo abre "
			"se queda sin fuente en tiempo de ejecucion")
	endif()
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
