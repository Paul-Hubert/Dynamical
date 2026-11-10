# Copies resources/ next to the build, files that changed only.
# The vegetation library is left out: it is generated (tools/speedtree_import.py), holds gigabytes of textures,
# and the engine reads it from the source tree (vegetationDirectory()).
file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}" PATTERN "vegetation" EXCLUDE)
