DIORAMA_RULE_JSON := $(sort $(wildcard data/diorama/*.json data/diorama/*/*.json))
DIORAMA_RULE_DIRS := data/diorama data/diorama/maps data/diorama/tilesets data/diorama/buildings
DIORAMA_RULE_CATALOGS := $(sort $(wildcard data/maps/*/map.json)) \
	$(sort $(wildcard data/layouts/*/map.bin)) \
	 $(sort $(wildcard data/tilesets/*/*/metatile_attributes.bin \
	                       data/tilesets/*/*/metatiles.bin \
	                       data/tilesets/*/*/tiles.png \
	                       data/tilesets/*/*/palettes/*.gbapal))
DIORAMA_RULE_COMPILER := tools/diorama_rules/compile_rules.py
DIORAMA_RULE_COMPILER_DEPS := tools/diorama_rules/building_profiles.py \
	tools/diorama_rules/catalog.py \
	tools/diorama_rules/emerald_compositor.py \
	tools/diorama_rules/occupancy_model.py \
	tools/diorama_rules/terrain_volumes.py
DIORAMA_RULE_SOURCE := src/data/diorama/diorama_rules.generated.c
DIORAMA_RULE_HEADER := include/diorama/rules.generated.h

AUTO_GEN_TARGETS += $(DIORAMA_RULE_SOURCE) $(DIORAMA_RULE_HEADER)

$(DIORAMA_RULE_SOURCE) $(DIORAMA_RULE_HEADER) &: $(DIORAMA_RULE_JSON) $(DIORAMA_RULE_DIRS) \
 $(DIORAMA_RULE_CATALOGS) $(DIORAMA_RULE_COMPILER) $(DIORAMA_RULE_COMPILER_DEPS) \
	 data/maps/map_groups.json data/layouts/layouts.json include/constants/metatile_behaviors.h \
	 src/data/tilesets/headers.h src/data/tilesets/graphics.h \
	 src/data/tilesets/metatiles.h src/graphics.c
	python3 $(DIORAMA_RULE_COMPILER)
