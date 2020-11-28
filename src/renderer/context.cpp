#include "context.h"

#include "model/model_manager.h"

Context::Context(entt::registry& reg) : vr(*this), win(*this), instance(*this), device(*this), transfer(*this), swap(*this) {

    vr.init();

    transfer.flush();

}

Context::~Context() {

    vr.finish();

}
