# CMakeLists.txt to register custom cjson component
idf_component_register(SRCS "cJSON.c"
                       INCLUDE_DIRS "."
                       REQUIRES)
