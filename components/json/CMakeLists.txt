# Compatibility name used by Squeezelite and cspot before IDF 6 moved cJSON
# into the component registry. Keep one implementation shared by all callers.
idf_component_register(REQUIRES espressif__cjson)
