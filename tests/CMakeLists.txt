# tests/CMakeLists.txt -- pruebas unitarias de los opcodes del SH-4.
#
# El binario enlaza los handlers reales del emulador y la tabla real de
# opcodes.c. Quedan fuera main.c, mem.c, graficos.c, intc.c, debug.c, iso.c,
# gui.cpp y BFont.c: son la parte que necesita SDL, OpenGL y una imagen de
# disco. tests/memoria_prueba.c y tests/dobles.c cubren lo que los handlers
# referencian de ahi.
#
# Las cabeceras de SDL 1.2 igual hacen falta para compilar: opcodes.h incluye
# main.h, que incluye <SDL/SDL.h> y <GL/gl.h>. No se enlaza ninguna funcion de
# SDL, asi que el ejecutable no necesita las DLL.

set(DCEMU_NUCLEO
	${CMAKE_SOURCE_DIR}/arith.c
	${CMAKE_SOURCE_DIR}/branch.c
	${CMAKE_SOURCE_DIR}/dcopcodes.c
	${CMAKE_SOURCE_DIR}/excepciones.c
	${CMAKE_SOURCE_DIR}/floatcontrol.c
	${CMAKE_SOURCE_DIR}/floatgraph.c
	${CMAKE_SOURCE_DIR}/floatsimple.c
	${CMAKE_SOURCE_DIR}/aica.c
	${CMAKE_SOURCE_DIR}/arm7.c
	${CMAKE_SOURCE_DIR}/g2dma.c
	${CMAKE_SOURCE_DIR}/gdrom.c
	${CMAKE_SOURCE_DIR}/log.c
	${CMAKE_SOURCE_DIR}/logic.c
	${CMAKE_SOURCE_DIR}/mmu.c
	${CMAKE_SOURCE_DIR}/mov.c
	${CMAKE_SOURCE_DIR}/opciones.c
	${CMAKE_SOURCE_DIR}/opcodes.c
	${CMAKE_SOURCE_DIR}/perf.c
	${CMAKE_SOURCE_DIR}/sh4emu.c
	${CMAKE_SOURCE_DIR}/shift.c
	${CMAKE_SOURCE_DIR}/simdx86_stub.c
	${CMAKE_SOURCE_DIR}/sistema.c
	${CMAKE_SOURCE_DIR}/syscontrol.c
	${CMAKE_SOURCE_DIR}/ta.c
	${CMAKE_SOURCE_DIR}/tmu.c
	${CMAKE_SOURCE_DIR}/ubc.c
	${CMAKE_SOURCE_DIR}/vram.c
	${CMAKE_SOURCE_DIR}/wdt.c
)

set(DCEMU_PRUEBAS
	arnes.c
	dctest.c
	dobles.c
	memoria_prueba.c
	principal.c
	test_arith.c
	test_branch.c
	test_cobertura.c
	test_decodificacion.c
	test_fpu_control.c
	test_fpu_excepciones.c
	test_fpu_graph.c
	test_fpu_simple.c
	test_aica.c
	test_arm7.c
	test_g2dma.c
	test_gdrom.c
	test_logic.c
	test_mmu.c
	test_mov.c
	test_shift.c
	test_sistema.c
	test_syscontrol.c
	test_ta.c
	test_tmu.c
	test_ubc.c
	test_vram.c
	test_wdt.c
)

add_executable(dcemu_tests ${DCEMU_NUCLEO} ${DCEMU_PRUEBAS})

target_include_directories(dcemu_tests PRIVATE
	${CMAKE_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}
	${SDL12_STAGED_INCLUDE}
)

target_compile_definitions(dcemu_tests PRIVATE
	WIN32
	_CRT_SECURE_NO_WARNINGS
	X86_OPT
)

target_compile_options(dcemu_tests PRIVATE
	/W3
	/wd4996		# funciones "inseguras" de la CRT
)

# Una prueba de CTest por suite: asi el reporte dice cual categoria se rompio
# sin tener que leer la salida entera.
foreach(_suite mov arith logic shift branch syscontrol fpu-simple fpu-control fpu-graph fpu-excepciones decodificacion)
	add_test(NAME sh4.${_suite} COMMAND dcemu_tests ${_suite})
endforeach()

# Las piezas del arranque por BIOS no son opcodes, asi que van con otro prefijo.
# La MMU tampoco lo es: es la ventana de control P4 (docs/mmu-plan.md, fase 1).
foreach(_suite sistema gdrom ta mmu wdt tmu vram ubc)
	add_test(NAME dc.${_suite} COMMAND dcemu_tests ${_suite})
endforeach()

# La corrida sin filtros, que ademas mide la cobertura de la tabla de opcodes:
# la suite de cobertura mira lo que ejecutaron las demas, asi que necesita que
# corran todas.
add_test(NAME sh4.todo COMMAND dcemu_tests)

# --------------------------------------------------------------------------
# El nucleo contra SingleStepTests/sh4 (233 codificaciones x 500 casos).
#
# Va en su propio ejecutable porque reemplaza el modelo de memoria: las pruebas
# ven un espacio plano de 32 bits, no el mapa del Dreamcast. Reusa arnes.c,
# dctest.c, dobles.c y memoria_prueba.c -- de este ultimo solo la reserva de
# los bloques y el enlace de los registros; las dos tablas de despacho las
# vuelve a escribir singlestep.c entero.
#
# Los datos no van en el repositorio (92 MB). Ver tests/README.md.
add_executable(dcemu_sh4json
	${DCEMU_NUCLEO}
	arnes.c
	dctest.c
	dobles.c
	memoria_prueba.c
	singlestep.c
)

target_include_directories(dcemu_sh4json PRIVATE
	${CMAKE_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}
	${SDL12_STAGED_INCLUDE}
)

target_compile_definitions(dcemu_sh4json PRIVATE
	WIN32
	_CRT_SECURE_NO_WARNINGS
	X86_OPT
)

target_compile_options(dcemu_sh4json PRIVATE
	/W3
	/wd4996
)

# La ruta a los datos: -DDCEMU_SH4_JSON=... al configurar, o la variable de
# entorno del mismo nombre al correr. Sin ninguna de las dos, el binario sale
# con 77 y CTest marca la prueba como omitida en vez de fallada.
set(DCEMU_SH4_JSON "" CACHE PATH
	"Ruta al clon de https://github.com/SingleStepTests/sh4 (233 .json.bin)")

if(DCEMU_SH4_JSON)
	add_test(NAME sh4.singlestep COMMAND dcemu_sh4json --dir=${DCEMU_SH4_JSON})
else()
	add_test(NAME sh4.singlestep COMMAND dcemu_sh4json)
endif()

set_tests_properties(sh4.singlestep PROPERTIES SKIP_RETURN_CODE 77)
