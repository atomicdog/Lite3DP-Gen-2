idf_component_register(
    SRCS
        "src/pngdec.c"
        "src/adler32.c"
        "src/crc32.c"
        "src/infback.c"
        "src/inffast.c"
        "src/inflate.c"
        "src/inftrees.c"
        "src/zutil.c"
    INCLUDE_DIRS "include" "src"
)

# Suppress warnings in third-party zlib code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-implicit-fallthrough -Wno-shift-negative-value
    -Wno-unused-function -Wno-missing-prototypes -Wno-missing-declarations
    -Wno-shadow -Wno-sign-compare -Wno-old-style-declaration
)
