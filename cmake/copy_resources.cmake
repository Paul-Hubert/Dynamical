# Copies resources/ next to the build, files that changed only.
# The vegetation and animal libraries are left out: they are generated (tools/speedtree_import.py,
# tools/animal_import.py), hold gigabytes of textures, and the engine reads them from the source tree
# (vegetationDirectory(), animalsDirectory()).
file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}" PATTERN "vegetation" EXCLUDE PATTERN "animals" EXCLUDE)
