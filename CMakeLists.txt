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

# El backend .chd (chdman, de MAME), sobre libchdr, que viene vendorizado en
# deps/libchdr con sus tres dependencias (lzma, miniz, zstd) porque sus
# binarios publicados son de MinGW y aqui se compila con MSVC. Es el commit
# 1d40b6ee (2026-09-04) de https://github.com/rtissera/libchdr, sin cambios
# salvo quitar su .github/ (lzma 26.02, miniz 3.1.2, zstd 1.5.7; antes fue
# 6cde5348 con lzma 25.01 y miniz 3.1.1 -- la actualizacion salio byte a byte
# identica en las ocho imagenes CHD del banco). Apagado, chd.c se compila igual
# pero como stub que informa que esta compilacion no lo trae, igual que la ruta
# libcdio.
option(DCEMU_USE_CHD "Compilar el backend .chd (libchdr, en deps/)" ON)
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

# El prototipo desechable de la fase 4 de docs/rendimiento-plan-2.md: el lazo
# mas caliente de Crazy Taxi traducido a mano como C fusionado, para medir el
# techo del recompilador con un numero de este arbol. Dentro del binario lo
# enciende DCEMU_FUSION=1, asi el A/B corre sobre una sola imagen.
option(DCEMU_FUSION "Compilar el prototipo de bloques fusionados (sonda)" OFF)

# El recompilador dinamico (docs/recompilador-plan.md). Detras de una opcion
# por la misma razon que la fusion: el A/B corre sobre una sola imagen y la
# disposicion del binario no entra como variable. Compilar con DCEMU_FUSION=ON
# y DCEMU_JIT=ON a la vez da la comparacion de tres --interprete, C fusionado,
# JIT-- en un solo binario.
#
# Viene en ON desde la adopcion (fase F.2 de jit-sota-plan.md, 2026-08-20):
# el binario que se entrega lleva el traductor adentro y encendido por
# omision -- DCEMU_JIT=0 en el ambiente es la palanca que deja al interprete
# solo. La F.1 lo avala entera: 131/131 demos con piso 0 y senal 0, y 46/48
# juegos exactos (los dos restantes, nombrados en el plan). OFF sigue
# existiendo para el binario de control sin recompilador.
option(DCEMU_JIT "Compilar el recompilador dinamico" ON)

# La sonda de conservacion de avances de URC (el expediente de la compuerta
# MMU+FPU): un contador en el unico cuerpo C del avance, otro emitido junto al
# avance en linea del traductor, y la virtual del ultimo avance emitido. Los
# imprime el punto de control (DCEMU_CP_MS). Detras de una opcion de
# compilacion como DCEMU_FORMA y por lo mismo: el contador C vive en el macro
# mas caliente del arbol, y lo que la sonda compara son CONTEOS, que salen
# identicos en cualquier compilacion -- la disposicion contamina al tiempo,
# no a la cuenta.
option(DCEMU_SONDA_URC "Compilar la sonda de conservacion de avances de URC" OFF)

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

# clang-cl (fase G de docs/jit-sota-plan.md): mismo frontend de banderas que
# MSVC -- CMake deja MSVC en TRUE tambien con clang-cl -- asi que la
# distincion va por el ID del compilador. LTO y PGO tienen su rama propia mas
# abajo; el resto de las banderas /X las entiende igual.
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
	set(DCEMU_CLANG TRUE)
else()
	set(DCEMU_CLANG FALSE)
endif()

# La contraccion a FMA (fusionar a*b+c) puede mover el interprete FPU al bit,
# y la conformidad del nucleo se compara bit a bit contra SingleStepTests. El
# /fp:precise de MSVC no contrae; el de clang SI contrae dentro de una
# sentencia, asi que se apaga explicitamente. Aca arriba y no en el target por
# lo mismo que /arch: tests/ compara coma flotante contra el binario que se
# mide, y tiene que compilar con la misma semantica.
if(DCEMU_CLANG)
	add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/clang:-ffp-contract=off>)

	# Ambos makefiles compilan con -fno-strict-aliasing y el comentario de mas
	# abajo ya nombra el riesgo: el type-punning de mem.c y floatsimple.c. MSVC
	# no hace analisis de aliasing agresivo asi que alli la pregunta no existe;
	# clang si lo hace, y aca se apaga explicito en vez de confiar en el default
	# de clang-cl.
	add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/clang:-fno-strict-aliasing>)
endif()

# Optimizacion guiada por perfil. Tres estados, y el orden importa:
#
#   cmake -S . -B build -DDCEMU_PGO=GEN     # binario instrumentado
#   cmake --build build --config Release --target dcemu
#   herramientas\pgo.ps1                    # corre el banco fijo y funde los .pgc
#                                           # (-Jit para el binario del recompilador)
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
# SDL3
#
# Hasta el 2026-09-05 el emulador hablaba SDL 1.2 a traves de sdl12-compat, que
# cargaba sdl2-compat, que cargaba SDL3: tres DLL para llegar a la biblioteca
# que de verdad hacia el trabajo. Ahora habla SDL3 directo (ventana, contexto
# GL, eventos, audio, hilos y superficies). El paquete de desarrollo oficial
# para MSVC trae cabeceras, .lib y la DLL; se descarga en el directorio de
# compilacion. Sirve igual para clang-cl: es la misma ABI.
# ---------------------------------------------------------------------------

set(SDL3_VERSION "3.4.16")
set(SDL3_SHA256 "1a784cb2a5c64d56fe7a62090fe9d242d9865f235e4ea9678f1a6ba4e693e7de")

set(SDL3_RAIZ "" CACHE PATH "Directorio de SDL3-devel ya descomprimido (vacio: se descarga)")

if(SDL3_RAIZ STREQUAL "")
	set(_sdl3_zip "${CMAKE_BINARY_DIR}/deps/SDL3-devel-${SDL3_VERSION}-VC.zip")
	set(_sdl3_root "${CMAKE_BINARY_DIR}/deps/SDL3-${SDL3_VERSION}")

	if(NOT EXISTS "${_sdl3_root}/include/SDL3/SDL.h")
		message(STATUS "Descargando SDL3 ${SDL3_VERSION}")
		file(DOWNLOAD
			"https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VERSION}/SDL3-devel-${SDL3_VERSION}-VC.zip"
			"${_sdl3_zip}"
			EXPECTED_HASH SHA256=${SDL3_SHA256}
			SHOW_PROGRESS)
		file(ARCHIVE_EXTRACT INPUT "${_sdl3_zip}" DESTINATION "${CMAKE_BINARY_DIR}/deps")
	endif()

	set(SDL3_RAIZ "${_sdl3_root}")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(SDL3_LIBDIR "${SDL3_RAIZ}/lib/x64")
else()
	set(SDL3_LIBDIR "${SDL3_RAIZ}/lib/x86")
endif()

# Las cabeceras vienen como include/SDL3/SDL.h, que es como el codigo las
# incluye: no hay que replicar nada, como habia que hacer con sdl12-compat.
set(SDL3_INCLUDE "${SDL3_RAIZ}/include")

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
	cdda.c
	iso9660_min.c
	cdi.c
	gdi.c
	chd.c
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
	glmoderno.c
	ubc.c
	vmu.c
	vram.c
	aica.c
	aicadsp.c
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
	${SDL3_INCLUDE}
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

if(DCEMU_USE_CHD)
	# EXCLUDE_FROM_ALL deja fuera lo que libchdr define y dcemu no enlaza (su
	# benchmark de tests/, la variante compartida); chdr-static se compila por
	# ser dependencia. Sin /GL: mezclar objetos con y sin el en el enlace LTCG
	# es valido, y asi el codigo de terceros queda fuera del perfil de PGO.
	set(BUILD_SHARED_LIBS OFF)
	add_subdirectory(deps/libchdr EXCLUDE_FROM_ALL)

	target_compile_definitions(dcemu PRIVATE DCEMU_USE_CHD)
	target_link_libraries(dcemu PRIVATE chdr-static)
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

if(DCEMU_FUSION)
	target_compile_definitions(dcemu PRIVATE DCEMU_FUSION)
	target_sources(dcemu PRIVATE fusion.c)
endif()

if(DCEMU_JIT)
	target_compile_definitions(dcemu PRIVATE DCEMU_JIT)
	target_sources(dcemu PRIVATE jit.c jit_x64.c arm7jit.c aicadspjit.c)
endif()

if(DCEMU_SONDA_URC)
	target_compile_definitions(dcemu PRIVATE DCEMU_SONDA_URC)
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

# /OPT:ICF pliega funciones byte-identicas en una sola direccion, y este arbol
# **depende de que los manejadores de opcodes tengan direcciones distintas**:
# jit_plantilla_de() clasifica cada palabra comparando su puntero de manejador
# (el riesgo que la propia documentacion de /OPT:ICF advierte). El ICF de
# lld-link pliega nop con NOIMP y shal91 con shll94 -- ninguno de los cuales
# pliega MSVC -- y con eso el traductor clasificaba palabras que no son
# instrucciones como NOP: exacto de casualidad (solo se pliega codigo
# identico), pero las trazas cambiaban de forma y los conteos dejaban de ser
# invariantes entre compiladores. Medido: 30,2 contra 26,4 instrucciones por
# bloque en Crazy Taxi con el mismo guest exacto. La rama clang enlaza con
# /OPT:NOICF; MSVC conserva su configuracion canonica, que hoy no pliega
# ningun manejador con plantilla. La guarda de jit_iniciar() lo nombra si
# algun dia vuelve.
# Y MSVC tambien desde el 2026-09-03: el toolset 14.51 (VS 18) SI pliega --
# la guarda de jit_iniciar() salto en el primer arranque del binario MSVC de
# este arbol--, asi que la regla deja de ser por compilador: nadie pliega.
target_link_options(dcemu PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:NOICF>)

# Sanitizers (fase G de jit-sota-plan.md, la mitad barata): una corrida de
# las compuertas de exactitud bajo ASan o UBSan como red de correccion para
# jit.c y el manejo del arena -- nunca cronometro. Solo en la rama clang, que
# es la que trae los runtimes. Compilar con DCEMU_LTCG=OFF y DCEMU_PGO=OFF:
# este binario no se mide, se escucha.
set(DCEMU_SAN "OFF" CACHE STRING "Sanitizer: OFF, ASAN o UBSAN (solo clang-cl)")
set_property(CACHE DCEMU_SAN PROPERTY STRINGS OFF ASAN UBSAN)

if(NOT DCEMU_SAN STREQUAL "OFF")
	if(NOT DCEMU_CLANG)
		message(FATAL_ERROR "DCEMU_SAN necesita clang-cl (los runtimes son de LLVM)")
	endif()

	get_filename_component(_llvm_bin_san "${CMAKE_C_COMPILER}" DIRECTORY)
	file(GLOB _clang_rt_san "${_llvm_bin_san}/../lib/clang/*/lib/windows")
	if(NOT _clang_rt_san)
		message(FATAL_ERROR "no encuentro lib/clang/*/lib/windows junto a ${CMAKE_C_COMPILER}")
	endif()
	target_link_directories(dcemu PRIVATE ${_clang_rt_san})

	if(DCEMU_SAN STREQUAL "ASAN")
		target_compile_options(dcemu PRIVATE -fsanitize=address)
		# Enlazando con lld-link directo (sin el driver) el runtime se nombra a
		# mano; la DLL tiene que quedar junto al ejecutable (post-build abajo).
		target_link_libraries(dcemu PRIVATE clang_rt.asan_dynamic-x86_64.lib
			clang_rt.asan_dynamic_runtime_thunk-x86_64.lib)
		add_custom_command(TARGET dcemu POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${_clang_rt_san}/clang_rt.asan_dynamic-x86_64.dll"
				"$<TARGET_FILE_DIR:dcemu>/"
			VERBATIM)
	elseif(DCEMU_SAN STREQUAL "UBSAN")
		target_compile_options(dcemu PRIVATE -fsanitize=undefined)
		target_link_libraries(dcemu PRIVATE clang_rt.ubsan_standalone-x86_64.lib
			clang_rt.ubsan_standalone_cxx-x86_64.lib)
	else()
		message(FATAL_ERROR "DCEMU_SAN tiene que ser OFF, ASAN o UBSAN")
	endif()
	message(STATUS "dcemu: sanitizer ${DCEMU_SAN}")
endif()

if(DCEMU_LTCG)
	if(DCEMU_CLANG)
		# El equivalente LLVM de /GL+/LTCG es ThinLTO. Exige enlazar con
		# lld-link: link.exe no lee bitcode, y el sintoma seria un error de
		# "invalid or corrupt file" sobre cada objeto, no algo que lo nombre.
		if(NOT CMAKE_LINKER MATCHES "lld")
			message(FATAL_ERROR
				"DCEMU_LTCG con clang-cl necesita lld-link: configura con -DCMAKE_LINKER=lld-link")
		endif()
		target_compile_options(dcemu PRIVATE $<$<CONFIG:Release>:-flto=thin>)
	else()
		target_compile_options(dcemu PRIVATE $<$<CONFIG:Release>:/GL>)
		target_link_options(dcemu PRIVATE $<$<CONFIG:Release>:/LTCG>)
	endif()
endif()

# PGO. Exige /GL, que es lo que hace LTCG: sin el, el enlazador no tiene el
# codigo intermedio sobre el que aplicar el perfil.
if(NOT DCEMU_PGO STREQUAL "OFF")
	if(NOT DCEMU_LTCG)
		message(FATAL_ERROR "DCEMU_PGO necesita DCEMU_LTCG=ON (/GL)")
	endif()

	# La rama clang: el esquema de LLVM es -fprofile-generate (deja un .profraw
	# por corrida), llvm-profdata merge con los mismos pesos que pgomgr
	# (herramientas\pgo.ps1 -Clang), y -fprofile-use al compilar. El perfil es
	# propio -- ni el .pgd de MSVC ni el .profdata de clang se leen entre si --
	# y va aparte por binario, como los .pgd y por lo mismo.
	if(DCEMU_CLANG)
		if(DCEMU_JIT)
			set(DCEMU_PROFDATA "${CMAKE_SOURCE_DIR}/build-pgo/dcemu-jit-clang.profdata")
			set(DCEMU_PROFRAW "${CMAKE_SOURCE_DIR}/build-pgo/clang-jit-raw")
		else()
			set(DCEMU_PROFDATA "${CMAKE_SOURCE_DIR}/build-pgo/dcemu-clang.profdata")
			set(DCEMU_PROFRAW "${CMAKE_SOURCE_DIR}/build-pgo/clang-raw")
		endif()
		file(MAKE_DIRECTORY "${DCEMU_PROFRAW}")

		if(DCEMU_PGO STREQUAL "GEN")
			# clang-cl deja en cada objeto la marca /DEFAULTLIB del runtime del
			# perfil, pero enlazando con lld-link directo (sin el driver) nadie
			# le da la ruta: se agrega el directorio del compiler-rt a mano.
			get_filename_component(_llvm_bin "${CMAKE_C_COMPILER}" DIRECTORY)
			file(GLOB _clang_rt_dir "${_llvm_bin}/../lib/clang/*/lib/windows")
			if(NOT _clang_rt_dir)
				message(FATAL_ERROR "no encuentro lib/clang/*/lib/windows junto a ${CMAKE_C_COMPILER}")
			endif()

			target_compile_options(dcemu PRIVATE
				$<$<CONFIG:Release>:-fprofile-generate=${DCEMU_PROFRAW}>)
			target_link_directories(dcemu PRIVATE ${_clang_rt_dir})
			target_link_libraries(dcemu PRIVATE clang_rt.profile-x86_64.lib)
			message(STATUS "dcemu: PGO clang instrumentado, .profraw a ${DCEMU_PROFRAW}")
		elseif(DCEMU_PGO STREQUAL "USE")
			if(NOT EXISTS "${DCEMU_PROFDATA}")
				message(FATAL_ERROR
					"DCEMU_PGO=USE pero no existe ${DCEMU_PROFDATA}: falta entrenar (pgo.ps1 -Clang)")
			endif()

			# Los dos -Wno: sin ellos cada funcion que el banco no piso es una
			# advertencia, y el enlace ThinLTO repite el aviso por modulo.
			target_compile_options(dcemu PRIVATE
				$<$<CONFIG:Release>:-fprofile-use=${DCEMU_PROFDATA}>
				$<$<CONFIG:Release>:/clang:-Wno-profile-instr-unprofiled>
				$<$<CONFIG:Release>:/clang:-Wno-profile-instr-out-of-date>)
			message(STATUS "dcemu: PGO clang aplicado desde ${DCEMU_PROFDATA}")
		else()
			message(FATAL_ERROR "DCEMU_PGO tiene que ser OFF, GEN o USE")
		endif()

	else()

	# El .pgd vive fuera del directorio de build a proposito: sobrevive a un
	# borrado de build/ y a cambiar de GEN a USE, que es todo el punto de
	# entrenar una vez y compilar despues.
	#
	# El binario con el recompilador lleva **su propio** .pgd: es otro programa
	# --los ganchos de epoca en memwrite y UpdateSR son codigo real, jit.c entero
	# esta dentro-- y ademas su banco de entrenamiento es otro (cada guest corre
	# dos veces, una por forma; pgo.ps1 -Jit). Fundir sus corridas sobre el .pgd
	# del arbol degradaria el binario por omision sin que nadie lo pida.
	if(DCEMU_JIT)
		set(DCEMU_PGD "${CMAKE_SOURCE_DIR}/build-pgo/dcemu-jit.pgd")
	else()
		set(DCEMU_PGD "${CMAKE_SOURCE_DIR}/build-pgo/dcemu.pgd")
	endif()
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

	endif()	# DCEMU_CLANG / MSVC
endif()

# glu32 y winmm estan en los makefiles pero no se usan: la unica llamada a GLU
# esta comentada (graficos.c) y no hay ninguna a winmm.
target_link_libraries(dcemu PRIVATE
	"${SDL3_LIBDIR}/SDL3.lib"
	opengl32
	# dbghelp: la pila simbolizada del informe de caida (traza.c). Release lleva
	# /Zi, asi que el PDB queda al lado del .exe y no hay nada mas que hacer.
	dbghelp
)

# SDL3 no redefine main ni trae SDLmain.lib: el punto de entrada es el main()
# de main.c, con el arranque normal del CRT. La redireccion de stdout/stderr a
# stdout.txt/stderr.txt junto al ejecutable, que antes hacia SDLmain, la hace
# main.c (salida_redirigir). Sin consola hay que decirle al linker que el
# arranque sigue siendo el de main, no WinMain.
if(DCEMU_CONSOLE)
	target_link_options(dcemu PRIVATE /SUBSYSTEM:CONSOLE)
else()
	set_target_properties(dcemu PROPERTIES WIN32_EXECUTABLE TRUE)
	target_link_options(dcemu PRIVATE /ENTRY:mainCRTStartup)
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

# Una sola DLL desde SDL3 (antes eran tres: SDL.dll -> SDL2.dll -> SDL3.dll).
# Si falta, el proceso muere antes del main con STATUS_DLL_NOT_FOUND
# (0xC0000135).
add_custom_command(TARGET dcemu POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${SDL3_LIBDIR}/SDL3.dll" "$<TARGET_FILE_DIR:dcemu>/"
	VERBATIM)

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
