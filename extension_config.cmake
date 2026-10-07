# This file is included by DuckDB's build system. It specifies which extension to load

duckdb_extension_load(datalake
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    EXTENSION_VERSION v0.1.0
)

duckdb_extension_load(json)
