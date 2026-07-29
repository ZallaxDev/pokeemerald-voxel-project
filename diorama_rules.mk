DIORAMA_RULE_JSON := $(sort $(wildcard data/diorama/*.json data/diorama/*/*.json))
DIORAMA_RULE_DIRS := data/diorama data/diorama/maps data/diorama/tilesets data/diorama/buildings
DIORAMA_RULE_CATALOGS := $(sort $(wildcard data/maps/*/map.json)) \
 data/tilesets/primary/general/metatile_attributes.bin \
 data/tilesets/secondary/petalburg/metatile_attributes.bin
DIORAMA_RULE_COMPILER := tools/diorama_rules/compile_rules.py
DIORAMA_RULE_SOURCE := src/data/diorama/diorama_rules.generated.c
DIORAMA_RULE_HEADER := include/diorama/rules.generated.h

AUTO_GEN_TARGETS += $(DIORAMA_RULE_SOURCE) $(DIORAMA_RULE_HEADER)

$(DIORAMA_RULE_SOURCE) $(DIORAMA_RULE_HEADER) &: $(DIORAMA_RULE_JSON) $(DIORAMA_RULE_DIRS) \
 $(DIORAMA_RULE_CATALOGS) $(DIORAMA_RULE_COMPILER) \
 data/maps/map_groups.json data/layouts/layouts.json include/constants/metatile_behaviors.h
	python3 $(DIORAMA_RULE_COMPILER)
