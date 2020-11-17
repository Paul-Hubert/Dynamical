#define INST_LOAD(name) \
PFN_##name name = (PFN_##name) ( ctx.instance->getProcAddr(#name)); \
assert( name != nullptr);

#define DEV_LOAD(name) \
PFN_##name name = (PFN_##name) ( ctx.device->getProcAddr(#name)); \
assert( name != nullptr);
